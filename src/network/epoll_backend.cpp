//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "network/epoll_backend.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/eventfd.h>

namespace hce {
namespace detail {
namespace network {
namespace backend {

namespace {

std::error_code make_ec(int err) {
    return std::error_code(err, std::system_category());
}

} // anonymous

epoll_backend::epoll_backend(size_t thread_count)
    : thread_count_(thread_count == 0 ? 1 : thread_count)
{
    threads_.reserve(thread_count_);
    for(size_t i = 0; i < thread_count_; ++i) {
        threads_.push_back(std::make_unique<thread_state>());
    }
}

epoll_backend::~epoll_backend() {
    stop();
}

void epoll_backend::start() {
    if(started_.exchange(true)) {
        return;
    }

    for(size_t i = 0; i < thread_count_; ++i) {
        auto& ts = *threads_[i];

        ts.epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
        if(ts.epoll_fd < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_create1");
        }

        ts.wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if(ts.wake_fd < 0) {
            ::close(ts.epoll_fd);
            throw std::system_error(errno, std::system_category(),
                                    "eventfd");
        }

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = ts.wake_fd;
        if(::epoll_ctl(ts.epoll_fd, EPOLL_CTL_ADD, ts.wake_fd, &ev) < 0) {
            ::close(ts.wake_fd);
            ::close(ts.epoll_fd);
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl wake");
        }

        ts.running = true;
        ts.thread = std::thread(&epoll_backend::io_thread_main, this, i);
    }
}

void epoll_backend::stop() {
    if(stopped_.exchange(true) || !started_) {
        return;
    }

    for(auto& ts_ptr : threads_) {
        auto& ts = *ts_ptr;
        if(ts.running) {
            ts.running = false;
            wake(ts);
            if(ts.thread.joinable()) {
                ts.thread.join();
            }
        }
        if(ts.wake_fd >= 0) {
            ::close(ts.wake_fd);
            ts.wake_fd = -1;
        }
        if(ts.epoll_fd >= 0) {
            ::close(ts.epoll_fd);
            ts.epoll_fd = -1;
        }
    }
}

void epoll_backend::assign(int fd) {
    // Affinity is determined by hash; this is a no-op placeholder
    // that forces the mapping to be created if desired later.
    (void)thread_for(fd);
}

epoll_backend::thread_state& epoll_backend::thread_for(int fd) {
    size_t idx = static_cast<size_t>(fd) % thread_count_;
    return *threads_[idx];
}

void epoll_backend::wake(thread_state& ts) {
    if(ts.wake_fd >= 0) {
        uint64_t one = 1;
        ::write(ts.wake_fd, &one, sizeof(one));
    }
}

void epoll_backend::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if(flags < 0) {
        return;
    }
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

op_id epoll_backend::register_op(int fd,
                                 interest i,
                                 void* user,
                                 completion_fn complete)
{
    if(fd < 0 || !complete) {
        return 0;
    }

    set_nonblocking(fd);

    auto& ts = thread_for(fd);
    auto op_ptr = std::make_shared<op>();
    op_ptr->id = next_id_.fetch_add(1, std::memory_order_relaxed);
    op_ptr->fd = fd;
    op_ptr->interest_ = i;
    op_ptr->user = user;
    op_ptr->complete = std::move(complete);

    {
        std::lock_guard<std::mutex> lk(ts.mutex);
        ts.ops_by_id[op_ptr->id] = op_ptr;
        ts.ops_by_fd[fd] = op_ptr;
    }

    epoll_event ev{};
    ev.events = static_cast<uint32_t>(i) | EPOLLET | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;

    int rc = ::epoll_ctl(ts.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    if(rc < 0 && errno == EEXIST) {
        rc = ::epoll_ctl(ts.epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }

    if(rc < 0) {
        std::lock_guard<std::mutex> lk(ts.mutex);
        ts.ops_by_id.erase(op_ptr->id);
        ts.ops_by_fd.erase(fd);
        return 0;
    }

    return op_ptr->id;
}

bool epoll_backend::cancel(op_id id) {
    if(id == 0) {
        return false;
    }

    // Linear search across threads (rare path).
    for(auto& ts_ptr : threads_) {
        auto& ts = *ts_ptr;
        std::shared_ptr<op> op_ptr;

        {
            std::lock_guard<std::mutex> lk(ts.mutex);
            auto it = ts.ops_by_id.find(id);
            if(it == ts.ops_by_id.end()) {
                continue;
            }
            op_ptr = it->second;
            op_ptr->cancelled = true;
            ts.ops_by_id.erase(it);
            ts.ops_by_fd.erase(op_ptr->fd);
        }

        // Remove from epoll (best effort).
        ::epoll_ctl(ts.epoll_fd, EPOLL_CTL_DEL, op_ptr->fd, nullptr);

        // Deliver cancellation on the owning thread by waking it.
        // The IO thread will notice the op is gone and we complete here
        // directly so the caller sees a deterministic result.
        if(op_ptr->complete) {
            op_ptr->complete(op_ptr->fd,
                             make_ec(ECANCELED),
                             0,
                             op_ptr->user);
        }

        return true;
    }

    return false;
}

void epoll_backend::io_thread_main(size_t index) {
    auto& ts = *threads_[index];
    constexpr int max_events = 64;
    epoll_event events[max_events];

    while(ts.running) {
        int n = ::epoll_wait(ts.epoll_fd, events, max_events, -1);
        if(n < 0) {
            if(errno == EINTR) {
                continue;
            }
            break;
        }

        for(int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if(fd == ts.wake_fd) {
                // Drain the eventfd.
                uint64_t val;
                while(::read(ts.wake_fd, &val, sizeof(val)) > 0) {}
                continue;
            }

            std::shared_ptr<op> op_ptr;
            {
                std::lock_guard<std::mutex> lk(ts.mutex);
                auto it = ts.ops_by_fd.find(fd);
                if(it == ts.ops_by_fd.end()) {
                    continue;
                }
                op_ptr = it->second;
                // Remove so it is not re-delivered (edge-triggered + one-shot).
                ts.ops_by_id.erase(op_ptr->id);
                ts.ops_by_fd.erase(it);
            }

            // Remove from epoll before invoking completion so a re-register
            // from inside the callback is clean.
            ::epoll_ctl(ts.epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

            std::error_code ec;
            if(ev & (EPOLLERR | EPOLLHUP)) {
                int err = 0;
                socklen_t len = sizeof(err);
                if(::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                 &err, &len) == 0 && err != 0) {
                    ec = make_ec(err);
                } else {
                    ec = make_ec(ECONNRESET);
                }
            }

            if(op_ptr->complete && !op_ptr->cancelled) {
                op_ptr->complete(fd, ec, ev, op_ptr->user);
            }
        }
    }
}

} // backend
} // network
} // detail
} // hce

