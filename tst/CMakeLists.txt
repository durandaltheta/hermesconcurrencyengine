project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

# set default HCELOGLIMIT
if(NOT DEFINED HCELOGLIMIT OR HCELOGLIMIT STREQUAL "")
    set(HCELOGLIMIT -1)
endif() 

# set default test HCEPOOLALLOCATORDEFAULTSIZELIMIT
if(NOT DEFINED HCEPOOLALLOCATORDEFAULTSIZELIMIT OR HCEPOOLALLOCATORDEFAULTSIZELIMIT STREQUAL "")
    set(HCEPOOLALLOCATORDEFAULTSIZELIMIT 10240)
endif() 

# set default test HCESCHEDULERPOOLALLOCATORDEFAULTSIZELIMIT
if(NOT DEFINED HCESCHEDULERPOOLALLOCATORDEFAULTSIZELIMIT OR HCESCHEDULERPOOLALLOCATORDEFAULTSIZELIMIT STREQUAL "")
    set(HCESCHEDULERPOOLALLOCATORDEFAULTSIZELIMIT 10240)
endif() 


add_subdirectory(googletest)

add_executable(hce_ut 
    test_helpers.hpp
    #atomic_ut.cpp 
    #memory_ut.cpp
    #circular_buffer_ut.cpp
    #pool_allocator_ut.cpp 
    slab_allocator_ut.cpp 
    #queue_ut.cpp 
    #id_ut.cpp
    #chrono_ut.cpp
    #coroutine_ut.cpp
    #scheduler_schedule_ut.cpp
    #scheduler_timer_ut.cpp
    #scheduler_block_ut.cpp
    #scope_ut.cpp
    #channel_ut.cpp 
    #threadpool_ut.cpp
    #comparison_ut.cpp
)

target_include_directories(hce_ut PUBLIC ${HCE_INCLUDE_DIR} PRIVATE ${LOG_SOURCE_DIR})
target_link_libraries(hce_ut PRIVATE gtest gtest_main hce)

message("-- HCE UNIT TEST COMPILE DEFINES -- ")
message("-DHCELOGLIMIT:${HCELOGLIMIT}")

target_compile_definitions(
    hce_ut
    PRIVATE 
    -DHCELOGLIMIT=${HCELOGLIMIT})
