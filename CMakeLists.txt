cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall -Wfatal-errors")
set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wfatal-errors")
set(HCE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/inc)
set(HCE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(LOGURU_DIR ${CMAKE_CURRENT_LIST_DIR}/loguru)
set(BUILD_SHARED_LIBS OFF)

project(libhce)

# The number of buckets containing thread_local reusable allocated memory used 
# by this library's memory allocation facilities. Reused allocations prevent 
# needing to call process-wide locks.
#
# buckets are defined as incrementing holding powers of 2, each bucket one power 
# higher than the previous:
# - bucket 0 holds values of 1 byte
# - bucket 1 holds values of 2 bytes
# - bucket 2 holds values of 4 bytes
# - bucket 3 holds values of 8 bytes
# - etc.
#
# More buckets allows for reusable allocations of larger byte counts.
#
# Any allocated larger than the largest bucket can provide will be allocated 
# directly with `malloc()`.
if(NOT DEFINED HCETHREADLOCALMEMORYBUCKETCOUNT OR HCETHREADLOCALMEMORYBUCKETCOUNT STREQUAL "")
    set(HCETHREADLOCALMEMORYBUCKETCOUNT 13)
endif()

# Each reusable memmory bucket shares the same maximum count of bytes it can 
# grow to store. This value is that limit. The maximum count of elements a 
# bucket can contain is this value divided by the byte size of a bucket element.
# So increasing this value increases the number of elements a bucket can store 
# for reuse.
if(NOT DEFINED HCETHREADLOCALMEMORYBUCKETBYTELIMIT OR HCETHREADLOCALMEMORYBUCKETBYTELIMIT STREQUAL "")
    set(HCETHREADLOCALMEMORYBUCKETBYTELIMIT 65536)
endif()

# the default block limit of reusable allocations of a pool allocator
if(NOT DEFINED HCEPOOLALLOCATORDEFAULTBLOCKLIMIT OR HCEPOOLALLOCATORDEFAULTBLOCKLIMIT STREQUAL "")
    set(HCEPOOLALLOCATORDEFAULTBLOCKLIMIT 64)
endif()

# Default configuration of coroutine resources limit for reuse
if(NOT DEFINED HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT OR HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT STREQUAL "")
    set(HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT 256)
endif()

# Count of coroutine resources the global scheduler will persist for reuse
if(NOT DEFINED HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT OR HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT STREQUAL "")
    set(HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT 512)
endif()

# Count of coroutine resources threadpool schedulers will persist for reuse
if(NOT DEFINED HCETHREADPOOLCOROUTINERESOURCELIMIT OR HCETHREADPOOLCOROUTINERESOURCELIMIT STREQUAL "")
    set(HCETHREADPOOLCOROUTINERESOURCELIMIT ${HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT})
endif()

# Count of reusable block worker threads in the default global scheduler. Since 
# the global scheduler is likely to be selected for scheduling, a default of 1
# reusable block worker is selected.
if(NOT DEFINED HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT OR HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT STREQUAL "")
    set(HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT 1)
endif()

# Define the count of reusable block worker threads within threadpool 
# schedulers. Since CPU throughput is the expected primary usage of threadpool 
# schedulers, a default of 0 resusable threads is specified to not waste system 
# resources. This count is *per* threadpool scheduler.
if(NOT DEFINED HCETHREADPOOLBLOCKWORKERRESOURCELIMIT OR HCETHREADPOOLBLOCKWORKERRESOURCELIMIT STREQUAL "")
    set(HCETHREADPOOLBLOCKWORKERRESOURCELIMIT 0) 
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

if(NOT DEFINED HCELOGLIMIT OR HCELOGLIMIT STREQUAL "")
    set(HCELOGLIMIT -1) 
endif()

set(HCE_HEADER_FILES 
    ${LOGURU_DIR}/loguru.hpp
    ${HCE_INCLUDE_DIR}/utility.hpp
    ${HCE_INCLUDE_DIR}/memory.hpp
    ${HCE_INCLUDE_DIR}/logging.hpp
    ${HCE_INCLUDE_DIR}/atomic.hpp
    ${HCE_INCLUDE_DIR}/id.hpp
    ${HCE_INCLUDE_DIR}/chrono.hpp
    ${HCE_INCLUDE_DIR}/circular_buffer.hpp
    ${HCE_INCLUDE_DIR}/pool_allocator.hpp
    ${HCE_INCLUDE_DIR}/list.hpp
    ${HCE_INCLUDE_DIR}/synchronized_list.hpp
    ${HCE_INCLUDE_DIR}/coroutine.hpp
    ${HCE_INCLUDE_DIR}/scheduler.hpp
    ${HCE_INCLUDE_DIR}/threadpool.hpp
    #${HCE_INCLUDE_DIR}/mutex.hpp
    #${HCE_INCLUDE_DIR}/condition_variable.hpp
    ${HCE_INCLUDE_DIR}/channel.hpp
    ${HCE_INCLUDE_DIR}/scope.hpp
    ${HCE_INCLUDE_DIR}/hce.hpp
)

set(HCE_SOURCE_FILES
    ${LOGURU_DIR}/loguru.cpp
    ${HCE_SOURCE_DIR}/logging.cpp
    ${HCE_SOURCE_DIR}/memory.cpp
    ${HCE_SOURCE_DIR}/coroutine.cpp
    ${HCE_SOURCE_DIR}/scheduler.cpp
    ${HCE_SOURCE_DIR}/threadpool.cpp
    ${HCE_SOURCE_DIR}/config.cpp
)

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
    -DHCETHREADLOCALMEMORYBUCKETCOUNT=${HCETHREADLOCALMEMORYBUCKETCOUNT}
    -DHCETHREADLOCALMEMORYBUCKETBYTELIMIT=${HCETHREADLOCALMEMORYBUCKETBYTELIMIT}
    -DHCEPOOLALLOCATORDEFAULTBLOCKLIMIT=${HCEPOOLALLOCATORDEFAULTBLOCKLIMIT}
    -DHCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT=${HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT}
    -DHCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT=${HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT}
    -DHCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT=${HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT}
    -DHCETHREADPOOLCOROUTINERESOURCELIMIT=${HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT}
    -DHCETHREADPOOLBLOCKWORKERRESOURCELIMIT=${HCETHREADPOOLBLOCKWORKERRESOURCELIMIT}
    -DHCETHREADPOOLSCHEDULERCOUNT=${HCETHREADPOOLSCHEDULERCOUNT}
    -DHCELOGLEVEL=${HCELOGLEVEL}
    -DHCELOGLIMIT=${HCELOGLIMIT}
)

target_compile_definitions(hce PRIVATE ${HCE_COMPILE_DEFS})

message("-- HCE LIBRARY HEADERS -- ")
message(${PRETTY_HCE_HEADER_FILES})
message("-- HCE LIBRARY SOURCES -- ")
message(${PRETTY_HCE_SOURCE_FILES})
message("-- HCE LIBRARY COMPILE DEFINES -- ")
message("-DHCETHREADLOCALMEMORYBUCKETCOUNT=${HCETHREADLOCALMEMORYBUCKETCOUNT}")
message("-DHCETHREADLOCALMEMORYBUCKETBYTELIMIT=${HCETHREADLOCALMEMORYBUCKETBYTELIMIT}")
message("-DHCEPOOLALLOCATORDEFAULTBLOCKLIMIT=${HCEPOOLALLOCATORDEFAULTBLOCKLIMIT}")
message("-DHCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT=${HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT}")
message("-DHCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT=${HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT}")
message("-DHCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT=${HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT}")
message("-DHCETHREADPOOLCOROUTINERESOURCELIMIT=${HCETHREADPOOLCOROUTINERESOURCELIMIT}")
message("-DHCETHREADPOOLBLOCKWORKERRESOURCELIMIT=${HCETHREADPOOLBLOCKWORKERRESOURCELIMIT}")
message("-DHCETHREADPOOLSCHEDULERCOUNT=${HCETHREADPOOLSCHEDULERCOUNT}")
message("-DHCELOGLEVEL:${HCELOGLEVEL}")
message("-DHCELOGLIMIT:${HCELOGLIMIT}")

add_subdirectory(tst EXCLUDE_FROM_ALL)
add_subdirectory(ex EXCLUDE_FROM_ALL) 
