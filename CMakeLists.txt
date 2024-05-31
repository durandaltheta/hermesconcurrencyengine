cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
set(HCE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/inc)
set(HCE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(LOG_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/loguru)

project(libhce)

# Define the minimum count of available threads for await tasks executed by 
# `hce::blocking::call()` (more will be temporarily spawned as necessary).
set(HCEMINBLOCKPROCS 1)

message("-- HCE LIBRARY CONFIGURATION -- ")
message("HCEMINBLOCKPROCS:${HCEMINBLOCKPROCS}")

# logging comes from the 'easyloggingpp' project:
# https://github.com/abumq/easyloggingpp 
#
# Said project uses the MIT license

set(HCE_HEADER_FILES 
    ${LOG_SOURCE_DIR}/loguru.hpp
    ${HCE_INCLUDE_DIR}/utility.hpp
    ${HCE_INCLUDE_DIR}/atomic.hpp
    ${HCE_INCLUDE_DIR}/circular_buffer.hpp
    ${HCE_INCLUDE_DIR}/coroutine.hpp
    ${HCE_INCLUDE_DIR}/scheduler.hpp
    ${HCE_INCLUDE_DIR}/block.hpp
    ${HCE_INCLUDE_DIR}/sleep.hpp
    ${HCE_INCLUDE_DIR}/mutex.hpp
    ${HCE_INCLUDE_DIR}/condition_variable.hpp
    ${HCE_INCLUDE_DIR}/channel.hpp
    ${HCE_INCLUDE_DIR}/hce.hpp)

set(HCE_SOURCE_FILES
    ${HCE_SOURCE_DIR}/log_util.cpp
    ${HCE_SOURCE_DIR}/coroutine.cpp
    ${HCE_SOURCE_DIR}/scheduler.cpp
    ${HCE_SOURCE_DIR}/block.cpp)

# convert a list to a readable string
function (ListToString result delim)
    list(GET ARGV 2 temp)
    math(EXPR N "${ARGC}-1")
    foreach(IDX RANGE 3 ${N})
        list(GET ARGV ${IDX} STR)
        set(temp "${temp}${delim}${STR}")
    endforeach()
    set(${result} "${temp}" PARENT_SCOPE)
endfunction(ListToString)

ListToString(PRETTY_HCE_HEADER_FILES "\n" ${HCE_HEADER_FILES})
ListToString(PRETTY_HCE_SOURCE_FILES "\n" ${HCE_SOURCE_FILES})

message("-- HCE LIBRARY HEADERS -- ")
message(${PRETTY_HCE_HEADER_FILES})
message("-- HCE LIBRARY SOURCES -- ")
message(${PRETTY_HCE_SOURCE_FILES})

add_library(hce STATIC 
    ${HCE_SOURCE_FILES}
    ${HCE_HEADER_FILES})

target_include_directories(hce PUBLIC ${HCE_INCLUDE_DIR} ${LOG_SOURCE_DIR})
target_link_libraries(hce PRIVATE loguru::loguru)

message("-- HCE LIBRARY COMPILE DEFINES -- ")
message("-DHCEMINBLOCKPROCS=${HCEMINBLOCKPROCS}")

target_compile_definitions(
    hce
    PRIVATE 
    -DHCEMINBLOCKPROCS=${HCEMINBLOCKPROCS})

add_subdirectory(loguru)
add_subdirectory(tst EXCLUDE_FROM_ALL)
