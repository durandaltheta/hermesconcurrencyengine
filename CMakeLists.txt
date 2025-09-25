cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release) 
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wfatal-errors")

set(HCE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/inc)
set(HCE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(LOGURU_DIR ${CMAKE_CURRENT_LIST_DIR}/loguru)
option(BUILD_SHARED_LIBS "Build hce as a shared library" OFF)

project(libhce)

function(SplitOnWhitespace input result_var)
    string(STRIP "${input}" trimmed_input)  # Trim leading and trailing whitespace
    string(REGEX REPLACE "[ \t\n\r]+" ";" split_list "${trimmed_input}")  # Split on whitespace
    set(${result_var} ${split_list} PARENT_SCOPE)  # Return as list
endfunction()

function(pow2 result exponent)
    set(value 1)
    foreach(_ UNUSED RANGE ${exponent})
        math(EXPR value "${value} * 2")
    endforeach()
    set(${result} ${value} PARENT_SCOPE)
endfunction()

set(HCE_HEADER_FILES 
    ${LOGURU_DIR}/loguru.hpp
    ${HCE_INCLUDE_DIR}/base.hpp
    ${HCE_INCLUDE_DIR}/utility.hpp
    ${HCE_INCLUDE_DIR}/cleanup.hpp
    ${HCE_INCLUDE_DIR}/logging.hpp
    ${HCE_INCLUDE_DIR}/service.hpp
    ${HCE_INCLUDE_DIR}/memory.hpp
    ${HCE_INCLUDE_DIR}/alloc.hpp
    ${HCE_INCLUDE_DIR}/thread.hpp
    ${HCE_INCLUDE_DIR}/atomic.hpp
    ${HCE_INCLUDE_DIR}/id.hpp
    ${HCE_INCLUDE_DIR}/chrono.hpp
    ${HCE_INCLUDE_DIR}/circular_buffer.hpp
    ${HCE_INCLUDE_DIR}/list.hpp
    ${HCE_INCLUDE_DIR}/timer.hpp
    #${HCE_INCLUDE_DIR}/synchronized_list.hpp
    ${HCE_INCLUDE_DIR}/coroutine.hpp
    ${HCE_INCLUDE_DIR}/awaitable.hpp
    ${HCE_INCLUDE_DIR}/scheduler.hpp
    ${HCE_INCLUDE_DIR}/blocking.hpp
    ${HCE_INCLUDE_DIR}/mutex.hpp
    #${HCE_INCLUDE_DIR}/condition_variable.hpp
    ${HCE_INCLUDE_DIR}/channel.hpp
    #${HCE_INCLUDE_DIR}/scope.hpp
    ${HCE_INCLUDE_DIR}/threadpool.hpp
    ${HCE_INCLUDE_DIR}/lifecycle.hpp
    ${HCE_INCLUDE_DIR}/module.hpp
    ${HCE_INCLUDE_DIR}/hce.hpp
)

set(HCE_SOURCE_FILES
    ${LOGURU_DIR}/loguru.cpp
    ${HCE_SOURCE_DIR}/logging.cpp
    ${HCE_SOURCE_DIR}/cleanup.cpp
    ${HCE_SOURCE_DIR}/id.cpp
    ${HCE_SOURCE_DIR}/coroutine.cpp
    ${HCE_SOURCE_DIR}/awaitable.cpp
    ${HCE_SOURCE_DIR}/scheduler.cpp
    ${HCE_SOURCE_DIR}/blocking.cpp
    ${HCE_SOURCE_DIR}/mutex.cpp
    ${HCE_SOURCE_DIR}/timer.cpp
    ${HCE_SOURCE_DIR}/thread.cpp
    ${HCE_SOURCE_DIR}/threadpool.cpp
    ${HCE_SOURCE_DIR}/services.cpp
    ${HCE_SOURCE_DIR}/lifecycle.cpp
    ${HCE_SOURCE_DIR}/module.cpp
    ${HCE_SOURCE_DIR}/config.cpp
)

# Compile unit tests with or without time sensitive tests (which take a long 
# time and may be affected by processing environment)
if(NOT DEFINED TIME_SENSITIVE_TESTS_ENABLED OR "${TIME_SENSITIVE_TESTS_ENABLED}" STREQUAL "")
    set(TIME_SENSITIVE_TESTS_ENABLED "1")
endif()

if(NOT DEFINED HCE_COMPILE_DEFINES)
    set(HCE_COMPILE_DEFINES "")
endif()

# Define the pairs of variable names and their default values
set(PAIRS 
# the default cache size of reusable allocations of pool allocators
        HCEPOOLALLOCATORDEFAULTCACHESIZE "16"

# Default configuration of scheduler reusable coroutine handles. A sane value 
# (roughly equal or above the median count of executing coroutines during busy 
# periods) allows avoiding unnecessary re-allocation of handles. Handles are 
# very small allocations (pointer sized).
        HCEREUSABLECOROUTINEHANDLEDEFAULTSCHEDULERCACHE "256"

# Count of coroutine handles the global scheduler will persist for reuse
        HCEREUSABLECOROUTINEHANDLEGLOBALSCHEDULERCACHE "512"

# Count of coroutine handles threadpool schedulers will persist for reuse
        HCEREUSABLECOROUTINEHANDLETHREADPOOLCACHE "256"

# Count of reusable block worker threads shared amongst the whole process.
#
# Block worker threads (accessed by calls to `hce::block()` and 
# `hce::blocking::service::block()`) are spawned and maintained process-wide.
#
# Reuse of workers eliminates thread startup costs. This value sets the count of 
# cached block worker threads that will be held by the framework when blocking 
# calls finish. Setting this value higher can alleviate slowdown from frequent 
# blocking calls at the cost of increased memory usage from keeping more system 
# threads in existence.
        HCEREUSABLEBLOCKWORKERCACHESIZE "1"

# Define the count of available threads running schedulers in the threadpool. If 
# unspecified or set to 0, the framework will decide the final threadcount. The 
# default is 1, only spawning the global scheduler thread.
        HCETHREADPOOLSCHEDULERCOUNT "1"

# Microsecond busy wait timer threshhold. If a timer has this amount of time or 
# less before timeout, the timer_service will busy-wait
        HCETIMERBUSYWAITMICROSECONDTHRESHOLD "5000"

# Microsecond early wakeup duration. The timer service will attempt to wakeup 
# early by this duraiton to increase timeout precision by going back to sleep 
# closer to the intended timeout.
        HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD "10000"

# A second tier of early wakeup to try to wakeup longer timers a little earlier 
# even than the first early wakeup threshold.
        HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD "100000"

# Set runtime loglevel to a sane default (loguru::Verbosity_WARNING)
        HCELOGLEVEL "-1"

# Set compilation limiter loglimit to a sane default (loguru::Verbosity_WARNING)
        HCELOGLIMIT "-1"
)

# Add pairs to compile defines
list(LENGTH PAIRS len)
set(i 0)
while(i LESS len)
    list(GET PAIRS ${i} name)
    math(EXPR i "${i} + 1")
    list(GET PAIRS ${i} value)

    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        set(${name} ${value})
    endif()

    list(APPEND HCE_COMPILE_DEFINES "${name}=${${name}}")
    math(EXPR i "${i} + 1")
endwhile()

add_library(hce SHARED ${HCE_HEADER_FILES} ${HCE_SOURCE_FILES})

# Apply the shared/static setting
set_target_properties(hce PROPERTIES
    POSITION_INDEPENDENT_CODE ON  # Ensure PIC for shared builds
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(STD_LIB "c++")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(STD_LIB "stdc++")
endif()

target_link_libraries(hce PRIVATE ${STD_LIB})
target_include_directories(hce PUBLIC ${HCE_INCLUDE_DIR} ${LOGURU_DIR})
target_compile_definitions(hce PRIVATE ${HCE_COMPILE_DEFINES})

SplitOnWhitespace("${HCE_HEADER_FILES}" PRETTY_HCE_HEADER_FILES)
SplitOnWhitespace("${HCE_SOURCE_FILES}" PRETTY_HCE_SOURCE_FILES)
SplitOnWhitespace("${HCE_COMPILE_DEFINES}" PRETTY_HCE_COMPILE_DEFINES)

message("-- HCE LIBRARY -- ")
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
message("TIME_SENSITIVE_TESTS_ENABLED=${TIME_SENSITIVE_TESTS_ENABLED}")
message("-- HCE LIBRARY HEADERS -- ")

foreach(item IN LISTS PRETTY_HCE_HEADER_FILES)
    message("${item}")
endforeach()

message("-- HCE LIBRARY SOURCES -- ")

foreach(item IN LISTS PRETTY_HCE_SOURCE_FILES)
    message("${item}")
endforeach()

message("-- HCE LIBRARY COMPILE DEFINES -- ")

foreach(item IN LISTS PRETTY_HCE_COMPILE_DEFINES)
    message("${item}")
endforeach()

add_subdirectory(tst EXCLUDE_FROM_ALL)
add_subdirectory(ex EXCLUDE_FROM_ALL) 
add_subdirectory(util EXCLUDE_FROM_ALL)
