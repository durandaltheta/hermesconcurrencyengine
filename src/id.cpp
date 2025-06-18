#include "id.hpp"

std::string hce::id::content() const {
    std::stringstream ss;
    ss << "get():" << get();
    return ss.str();
}

hce::id::operator bool() const { return (bool)get(); }

bool hce::id::operator<(const hce::id& rhs) const { 
    return get() < rhs.get(); 
}

bool hce::id::operator ==(const hce::id& rhs) const { 
    return get() == rhs.get(); 
}

bool hce::id::operator !=(const hce::id& rhs) const {
    return !(*this == rhs);
}

hce::uid::uid() { HCE_TRACE_CONSTRUCTOR(); }
hce::uid::~uid() { HCE_TRACE_DESTRUCTOR(); }
std::string hce::uid::info_name() { return "hce::uid"; }
std::string hce::uid::name() const { return hce::uid::info_name(); }

void hce::uid::make() {
    HCE_TRACE_METHOD_ENTER("make");
    byte_ = hce::make_unique<std::byte>();
}

void hce::uid::reset() {
    HCE_TRACE_METHOD_ENTER("reset");
    byte_.reset();
}

void* hce::uid::get() const { return byte_.get(); }

hce::sid::sid() { HCE_TRACE_CONSTRUCTOR(); }
hce::sid::~sid() { HCE_TRACE_DESTRUCTOR(); }
std::string hce::sid::info_name() { return "hce::sid"; }
std::string hce::sid::name() const { return sid::info_name(); }

void hce::sid::make() {
    HCE_TRACE_METHOD_ENTER("make");
    byte_ = hce::make_shared<std::byte>();
}

void hce::sid::reset() {
    HCE_TRACE_METHOD_ENTER("reset");
    byte_.reset();
}

void* hce::sid::get() const { return byte_.get(); }
