//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_STRING__
#define __HERMES_COROUTINE_ENGINE_STRING__

#include <string>
#include <sstream>
#include <ostream>

#include "memory.hpp"

namespace hce {

/**
 @brief framework string implementation 

 The default Alloc uses framework thread_local allocation caching.

 This is a descendent type of `std::basic_string` rather than a typedef to allow 
 for conversion operator to `std::string`. 
 */
template <typename CharT, 
          typename Traits = std::char_traits<CharT>, 
          typename Alloc = hce::allocator<CharT>>
struct basic_string : 
    public std::basic_string<CharT,Traits,Alloc>
{
    using base = std::basic_string<CharT,Traits,Alloc>;

    template <typename... As>
    basic_string(As&&... as) : 
        base(std::forward<As>(as)...)
    { }

    /// allow for conversion to the non-framework type
    inline operator std::string() const { 
        return std::string(this->c_str(), this->size());
    }

    /// implementing the stream insertion operator for the derived class
    inline friend std::ostream& operator<<(std::ostream& os, const basic_string<CharT,Traits,Alloc>& obj) {
        os << static_cast<const base&>(obj);  // Serialize the base class (std::string)
        return os;
    }

    /// implementing the stream extraction operator for the derived class
    inline friend std::istream& operator>>(std::istream& is, basic_string<CharT,Traits,Alloc>& obj) {
        std::string temp;
        std::getline(is, temp);
        obj = temp;  // Assign the value to the derived class
        return is;
    }
};



// typedefs explicitly use thread_local memory caching for allocation/deallocation.

typedef basic_string<char,std::char_traits<char>,hce::allocator<char>> string;
typedef basic_string<wchar_t,std::char_traits<wchar_t>,hce::allocator<wchar_t>> wstring;
typedef basic_string<char8_t,std::char_traits<char8_t>,hce::allocator<char8_t>> u8string;
typedef basic_string<char16_t,std::char_traits<char16_t>,hce::allocator<char16_t>> u16string;
typedef basic_string<char32_t,std::char_traits<char32_t>,hce::allocator<char32_t>> u32string;

typedef 
std::basic_stringstream<char,std::char_traits<char>,hce::allocator<char>>
stringstream;

typedef 
std::basic_stringstream<wchar_t,std::char_traits<wchar_t>,hce::allocator<wchar_t>>
wstringstream; 

}

#endif
