//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_STRING__
#define __HERMES_COROUTINE_ENGINE_STRING__

#include <string>
#include <sstream>
#include <ostream>
#include <iostream>

#include "memory.hpp"

namespace hce {

/// hce::basic_string uses thread_local memory caching for allocation/deallocation
template <typename CharT, 
          typename Traits = std::char_traits<CharT>, 
          typename Alloc = hce::allocator<CharT>>
struct basic_string : 
    public std::basic_string<CharT,Traits,Alloc>
{
    using base =
        std::basic_string<CharT,Traits,Alloc>;

    template <typename... As>
    basic_string(As&&... as) : 
        base(std::forward<As>(as)...)
    { }

    inline operator std::string() const { 
        return std::string(this->c_str(), this->size());
    }

    // Implementing the stream insertion operator for the derived class
    inline friend std::ostream& operator<<(std::ostream& os, const basic_string<CharT>& obj) {
        os << static_cast<const base&>(obj);  // Serialize the base class (std::string)
        return os;
    }

    // Implementing the stream extraction operator for the derived class
    inline friend std::istream& operator>>(std::istream& is, basic_string<CharT>& obj) {
        std::string temp;
        std::getline(is, temp);
        obj = temp;  // Assign the value to the derived class
        return is;
    }
};

typedef basic_string<char> string;
typedef basic_string<wchar_t> wstring;
typedef basic_string<char8_t> u8string;
typedef basic_string<char16_t> u16string;
typedef basic_string<char32_t> u32string;

typedef 
std::basic_stringstream<char,std::char_traits<char>,hce::allocator<char>>
stringstream;

typedef 
std::basic_stringstream<wchar_t,std::char_traits<wchar_t>,hce::allocator<wchar_t>>
wstringstream; 

}

#endif
