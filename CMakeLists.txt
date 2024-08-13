cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
set(HCE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/inc)
set(HCE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(LOGURU_DIR ${CMAKE_CURRENT_LIST_DIR}/loguru)
set(BUILD_SHARED_LIBS OFF)

project(libhce)

#-------------------------------------------------------------------------------
# environment variable controls 

# Define the minimum count of available threads for await tasks executed by 
# `hce::blocking::call()` (more will be temporarily spawned as necessary).
if(DEFINED HCEGLOBALREUSEBLOCKPROCS)
else()
    set(HCEGLOBALREUSEBLOCKPROCS 1) 
endif()

# set loglevel
if(DEFINED HCELOGLEVEL)
else()
    set(HCELOGLEVEL -1) 
endif()

# logging comes from the 'easyloggingpp' project:
# https://github.com/abumq/easyloggingpp 
#
# Said project uses the MIT license

set(HCE_HEADER_FILES 
    ${LOGURU_DIR}/loguru.hpp
    ${HCE_INCLUDE_DIR}/utility.hpp
    ${HCE_INCLUDE_DIR}/chrono.hpp
    ${HCE_INCLUDE_DIR}/atomic.hpp
    ${HCE_INCLUDE_DIR}/circular_buffer.hpp
    ${HCE_INCLUDE_DIR}/coroutine.hpp
    ${HCE_INCLUDE_DIR}/scheduler.hpp
    #${HCE_INCLUDE_DIR}/mutex.hpp
    #${HCE_INCLUDE_DIR}/condition_variable.hpp
    ${HCE_INCLUDE_DIR}/channel.hpp
    ${HCE_INCLUDE_DIR}/hce.hpp
)

set(HCE_SOURCE_FILES
    ${LOGURU_DIR}/loguru.cpp
    ${HCE_SOURCE_DIR}/utility.cpp
    ${HCE_SOURCE_DIR}/coroutine.cpp
    ${HCE_SOURCE_DIR}/scheduler.cpp)

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

add_library(hce STATIC ${HCE_HEADER_FILES} ${HCE_SOURCE_FILES})

target_include_directories(hce PUBLIC ${HCE_INCLUDE_DIR} ${LOGURU_DIR})

set(HCE_COMPILE_DEFS 
    -DHCELOGLEVEL=${HCELOGLEVEL}
    -DHCEGLOBALREUSEBLOCKPROCS=${HCEGLOBALREUSEBLOCKPROCS})

# if the user wants to implement their own logging initialization they should 
# enable this option
if(DEFINED ENV{HCECUSTOMLOGINIT})
    set(HCE_COMPILE_DEFS ${HCE_COMPILE_DEFS} -DHCECUSTOMLOGINIT)
endif()

# if the user wants to implement their own global configuration they should 
# enable this option
if(DEFINED ENV{HCECUSTOMGLOBALCONFIG})
    set(HCE_COMPILE_DEFS ${HCE_COMPILE_DEFS} -DHCECUSTOMGLOBALCONFIG)
endif()

target_compile_definitions(hce PRIVATE ${HCE_COMPILE_DEFS})

message("-- HCE LIBRARY HEADERS -- ")
message(${PRETTY_HCE_HEADER_FILES})
message("-- HCE LIBRARY SOURCES -- ")
message(${PRETTY_HCE_SOURCE_FILES})
message("-- HCE LIBRARY COMPILE DEFINES -- ")
message("-DHCEGLOBALREUSEBLOCKPROCS=${HCEGLOBALREUSEBLOCKPROCS}")
message("-DHCELOGLEVEL:${HCELOGLEVEL}")

add_subdirectory(tst EXCLUDE_FROM_ALL)
