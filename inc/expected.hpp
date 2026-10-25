// inc/expected.hpp (or inside an existing utility header)
#pragma once
#include <utility>
#include <variant>
#include <system_error>

namespace hce {

template <class E>
class unexpected {
public:
    explicit unexpected(E e) : err_(std::move(e)) {}
    const E& error() const & { return err_; }
    E&       error()       & { return err_; }
    E&&      error()      && { return std::move(err_); }
private:
    E err_;
};

template <class E>
unexpected(E) -> unexpected<E>;

template <class T, class E>
class expected {
public:
    expected(T v) : data_(std::move(v)) {}
    expected(unexpected<E> u) : data_(std::move(u)) {}

    bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return has_value(); }

    T&       value()       & { return std::get<T>(data_); }
    const T& value() const & { return std::get<T>(data_); }
    T&&      value()      && { return std::get<T>(std::move(data_)); }

    E&       error()       & { return std::get<unexpected<E>>(data_).error(); }
    const E& error() const & { return std::get<unexpected<E>>(data_).error(); }

private:
    std::variant<T, unexpected<E>> data_;
};

} // hce
