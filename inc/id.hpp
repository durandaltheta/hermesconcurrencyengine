//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_ID__
#define __HERMES_COROUTINE_ENGINE_ID__

#include <memory>
#include <cstddef>
#include <string>
#include <sstream>

#include "logging.hpp"
#include "memory.hpp"

namespace hce {

/**
 @brief identifier object interface

 Rerpresents an arbitrary unique identifying memory address. The address of this 
 memory is usable as a container key.
 */
struct id : public printable {
    inline std::string content() const {
        std::stringstream ss;
        ss << "get():" << get();
        return ss.str();
    }

    /**
     @brief construct the id
     */
    virtual void make() = 0;

    /**
     @brief deconstruct the id
     */
    virtual void reset() = 0;

    /**
     @return the allocated id pointer
     */
    virtual void* get() const = 0;

    /**
     @return true if the id represents a constructed id, else false
     */
    virtual inline operator bool() const { return (bool)get(); }

    /**
     @return true if the id is less than the other id
     */
    virtual inline bool operator<(const id& rhs) const { 
        return get() < rhs.get(); 
    }

    /**
     @return true if the ids represent the same value, else false
     */
    virtual inline bool operator ==(const id& rhs) const { 
        return get() == rhs.get(); 
    }

    /**
     @return true if the ids represent different values, else false
     */
    virtual inline bool operator !=(const id& rhs) const {
        return !(*this == rhs);
    }

};

/**
 @brief unique identifier object

 This object is not copiable, only movable
 */
struct uid : public id {
    uid() { HCE_TRACE_CONSTRUCTOR(); }
    uid(const uid&) = delete;
    uid(uid&&) = default;

    virtual ~uid() { HCE_TRACE_DESTRUCTOR(); }

    uid& operator=(const uid&) = delete;
    uid& operator=(uid&&) = default;

    static inline std::string info_name() { return "hce::uid"; }
    inline std::string name() const { return uid::info_name(); }

    inline void make() {
        HCE_TRACE_METHOD_ENTER("make");
        byte_ = hce::make_unique<std::byte>();
    }

    inline void reset() {
        HCE_TRACE_METHOD_ENTER("reset");
        byte_.reset();
    }

    inline void* get() const { return byte_.get(); }

private:
    hce::unique_ptr<std::byte> byte_;
};

/**
 @brief shared identifier object

 This object can be copied or moved.
 */
struct sid : public id {
    sid() { HCE_TRACE_CONSTRUCTOR(); }
    sid(const sid&) = default;
    sid(sid&&) = default;

    virtual ~sid() { HCE_TRACE_DESTRUCTOR(); }

    sid& operator=(const sid&) = default;
    sid& operator=(sid&&) = default;

    static inline std::string info_name() { return "hce::sid"; }
    inline std::string name() const { return sid::info_name(); }

    inline void make() {
        HCE_TRACE_METHOD_ENTER("make");
        byte_ = hce::make_shared<std::byte>();
    }

    inline void reset() {
        HCE_TRACE_METHOD_ENTER("reset");
        byte_.reset();
    }

    inline void* get() const { return byte_.get(); }

private:
    std::shared_ptr<std::byte> byte_;
};

}

#endif
