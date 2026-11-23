#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <string>
#include <sstream>

namespace hce {

/*
 @brief interface for allowing an object instance to be printable

 Objects which implement printable can passed to streams (IE, 
 `std::stringstream`, `std::cout`) and also converted to `std::string` 
 representation.
 */
struct printable {
    /**
     It is expected that the returned string properly include namespaces and 
     be properly templatized as necessary.

     @return the object name 
     */
    virtual std::string name() const = 0;

    /**
     @brief string with optional content of this object 

     This method can be overwritten by the inheritor, useful for describing 
     internal elements of a given object, such as allocated pointers, or other 
     printable objects it contains.
     */
    virtual inline std::string content() const { return {}; }

    /// string conversion
    std::string to_string() const;

    /// std::string conversion
    inline operator std::string() const { return to_string(); }
}; 

} // hce

namespace std {

/// std:: printable string conversion
inline std::string to_string(const hce::printable& p) { return p.to_string(); }

/**
 :: ostream writing of a printable reference. An object which implements 
 printable can be passed by reference here without typecasting.
 */
inline std::ostream& operator<<(std::ostream& out, const hce::printable& p) {
    out << p.to_string();
    return out;
}

/**
 :: ostream writing of a printable pointer. An object which implements 
 printable can be passed by pointer here without typecasting.
 */
inline std::ostream& operator<<(std::ostream& out, const hce::printable* p) {
    if(p) { out << *p; } 
    else { out << "hce::printable@nullptr"; }
    return out;
} 
 
} // std
