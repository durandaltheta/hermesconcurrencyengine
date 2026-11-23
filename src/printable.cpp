//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include "hce/printable.hpp"

std::string hce::printable::to_string() const { 
    std::stringstream ss;

    // assemble the namespaced name and memory address of the object
    ss << this->name() << "@" << (void*)this;

    // check for object content
    std::string c = this->content();

    if(!(c.empty())) { 
        // put content in brackets to print like a container
        ss << "[" << c << "]";
    }

    return ss.str();
}
