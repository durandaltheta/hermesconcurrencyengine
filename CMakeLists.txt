cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall -Wfatal-errors")
set(HCE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/inc)
set(HCE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(LOGURU_DIR ${CMAKE_CURRENT_LIST_DIR}/loguru)
set(BUILD_SHARED_LIBS OFF)

project(libhce)

# Count of coroutines the global scheduler will persist resources for reuse
if(NOT DEFINED HCEGLOBALCOROUTINEPOOLLIMIT OR HCEGLOBALCOROUTINEPOOLLIMIT STREQUAL "")
    set(HCEGLOBALCOROUTINEPOOLLIMIT 512)
endif()

# Count of reusable block worker threads in the default global scheduler. Since 
# the global scheduler is likely to be selected for scheduling, a default of 1
# reusable block worker is selected.
if(NOT DEFINED HCEGLOBALBLOCKWORKERPOOLLIMIT OR HCEGLOBALBLOCKWORKERPOOLLIMIT STREQUAL "")
    set(HCEGLOBALBLOCKWORKERPOOLLIMIT 1)
endif()

# Count of coroutines threadpool schedulers will persist resources for reuse
if(NOT DEFINED HCETHREADPOOLCOROUTINEPOOLLIMIT OR HCETHREADPOOLCOROUTINEPOOLLIMIT STREQUAL "")
    set(HCETHREADPOOLCOROUTINEPOOLLIMIT 1)
endif()

# Define the count of reusable block worker threads within threadpool 
# schedulers. Since CPU throughput is the expected primary usage of threadpool 
# schedulers, a default of 0 resusable threads is specified to not waste system 
# resources. This count is *per* threadpool scheduler.
if(NOT DEFINED HCETHREADPOOLBLOCKWORKERPOOLLIMIT OR HCETHREADPOOLBLOCKWORKERPOOLLIMIT STREQUAL "")
    set(HCETHREADPOOLBLOCKWORKERPOOLLIMIT 0) 
endif()

if(NOT DEFINED HCETHREADPOOLCOROUTINEPOOLLIMIT OR HCETHREADPOOLCOROUTINEPOOLLIMIT STREQUAL "")
    set(HCETHREADPOOLCOROUTINEPOOLLIMIT 512) 
endif()

# Define the count of available threads running schedulers in the threadpool. If 
# unspecified or set to 0, the framework will decide the final threadcount
if(NOT DEFINED HCETHREADPOOLSCHEDULERCOUNT OR HCETHREADPOOLSCHEDULERCOUNT STREQUAL "")
    set(HCETHREADPOOLSCHEDULERCOUNT 0) 
endif()

# Set loglevel to a sane default (loguru::Verbosity_WARNING)
if(NOT DEFINED HCELOGLEVEL OR HCELOGLEVEL STREQUAL "")
    set(HCELOGLEVEL -1) 
endif()

set(HCE_HEADER_FILES 
    ${LOGURU_DIR}/loguru.hpp
    ${HCE_INCLUDE_DIR}/utility.hpp
    ${HCE_INCLUDE_DIR}/logging.hpp
    ${HCE_INCLUDE_DIR}/memory.hpp
    ${HCE_INCLUDE_DIR}/atomic.hpp
    ${HCE_INCLUDE_DIR}/id.hpp
    ${HCE_INCLUDE_DIR}/chrono.hpp
    ${HCE_INCLUDE_DIR}/circular_buffer.hpp
    ${HCE_INCLUDE_DIR}/slab_allocator.hpp
    ${HCE_INCLUDE_DIR}/queue.hpp
    ${HCE_INCLUDE_DIR}/slab_allocator.hpp
    ${HCE_INCLUDE_DIR}/synchronized_queue.hpp
    ${HCE_INCLUDE_DIR}/coroutine.hpp
    ${HCE_INCLUDE_DIR}/scheduler.hpp
    ${HCE_INCLUDE_DIR}/threadpool.hpp
    ${HCE_INCLUDE_DIR}/scope.hpp
    #${HCE_INCLUDE_DIR}/mutex.hpp
    #${HCE_INCLUDE_DIR}/condition_variable.hpp
    ${HCE_INCLUDE_DIR}/transfer.hpp
    ${HCE_INCLUDE_DIR}/channel.hpp
    ${HCE_INCLUDE_DIR}/hce.hpp
)

set(HCE_SOURCE_FILES
    ${LOGURU_DIR}/loguru.cpp
    ${HCE_SOURCE_DIR}/logging.cpp
    ${HCE_SOURCE_DIR}/coroutine.cpp
    ${HCE_SOURCE_DIR}/scheduler.cpp
    ${HCE_SOURCE_DIR}/threadpool.cpp
)

# allow for user default config implementations
if(DEFINED HCECUSTOMCONFIG)
else()
    set(HCE_SOURCE_FILES 
        ${HCE_SOURCE_FILES} 
        ${HCE_SOURCE_DIR}/config.cpp)
endif()

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
    -DHCEGLOBALCOROUTINEPOOLLIMIT=${HCEGLOBALCOROUTINEPOOLLIMIT}
    -DHCEGLOBALBLOCKWORKERPOOLLIMIT=${HCEGLOBALBLOCKWORKERPOOLLIMIT}
    -DHCETHREADPOOLCOROUTINEPOOLLIMIT=${HCEGLOBALCOROUTINEPOOLLIMIT}
    -DHCETHREADPOOLSCHEDULERCOUNT=${HCETHREADPOOLSCHEDULERCOUNT}
    -DHCETHREADPOOLBLOCKWORKERPOOLLIMIT=${HCETHREADPOOLBLOCKWORKERPOOLLIMIT}
)

target_compile_definitions(hce PRIVATE ${HCE_COMPILE_DEFS})

message("-- HCE LIBRARY HEADERS -- ")
message(${PRETTY_HCE_HEADER_FILES})
message("-- HCE LIBRARY SOURCES -- ")
message(${PRETTY_HCE_SOURCE_FILES})
message("-- HCE LIBRARY COMPILE DEFINES -- ")
message("-DHCEGLOBALCOROUTINEPOOLLIMIT=${HCEGLOBALCOROUTINEPOOLLIMIT}")
message("-DHCEGLOBALBLOCKWORKERPOOLLIMIT=${HCEGLOBALBLOCKWORKERPOOLLIMIT}")
message("-DHCETHREADPOOLCOROUTINEPOOLLIMIT=${HCEGLOBALCOROUTINEPOOLLIMIT}")
message("-DHCETHREADPOOLBLOCKWORKERPOOLLIMIT=${HCETHREADPOOLBLOCKWORKERPOOLLIMIT}")
message("-DHCETHREADPOOLSCHEDULERCOUNT=${HCETHREADPOOLSCHEDULERCOUNT}")
message("-DHCELOGLEVEL:${HCELOGLEVEL}")

add_subdirectory(tst EXCLUDE_FROM_ALL)
add_subdirectory(ex EXCLUDE_FROM_ALL) 
