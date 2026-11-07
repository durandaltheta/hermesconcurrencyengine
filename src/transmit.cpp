//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include "transmit.hpp"

namespace hce {
namespace transmit {

connection::~connection(){ }

std::string connection::info_name() {
    return "hce::transmit::connection";
}

std::string connection::name() const {
    return connection::info_name();
}

pump::state::state():
    in(hce::chan<state::result>::make(1)),
    out(hce::chan<state::result>::make(1))
{ }

pump::state::~state() {
    in.close();
    out.close();
}

} // transmit
} // hce
