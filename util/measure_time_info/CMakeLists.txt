cmake_minimum_required(VERSION 3.0)
project(mti)
add_executable(mti mti.cpp)

# Exclude this target from the "all" target
set_target_properties(mti PROPERTIES EXCLUDE_FROM_ALL TRUE)
