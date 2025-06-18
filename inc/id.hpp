//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_ID
#define HERMES_COROUTINE_ENGINE_ID

#include <memory>
#include <cstddef>
#include <string>
#include <sstream>

#include "logging.hpp"
#include "alloc.hpp"

namespace hce {

/**
 @brief identifier object interface

 Rerpresents an arbitrary unique identifying memory address. The address of this 
 memory is usable as a container key.
 */
struct id : public printable {
    std::string content() const;

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
    virtual operator bool() const;

    /**
     @return true if the id is less than the other id
     */
    virtual bool operator<(const id& rhs) const;

    /**
     @return true if the ids represent the same value, else false
     */
    virtual bool operator ==(const id& rhs) const;

    /**
     @return true if the ids represent different values, else false
     */
    virtual bool operator !=(const id& rhs) const;
};

/**
 @brief unique identifier object

 This object is not copiable, only movable
 */
struct uid : public id {
    uid();
    uid(const uid&) = delete;
    uid(uid&&) = default;
    virtual ~uid();
    uid& operator=(const uid&) = delete;
    uid& operator=(uid&&) = default;
    static std::string info_name();
    std::string name() const;
    void make();
    void reset();
    void* get() const;
private:
    hce::unique_ptr<std::byte> byte_;
};

/**
 @brief shared identifier object

 This object can be copied or moved.
 */
struct sid : public id {
    sid();
    sid(const sid&) = default;
    sid(sid&&) = default;
    virtual ~sid();
    sid& operator=(const sid&) = default;
    sid& operator=(sid&&) = default;
    static std::string info_name();
    std::string name() const;
    void make();
    void reset();
    void* get() const;
private:
    std::shared_ptr<std::byte> byte_;
};

}

#endif
