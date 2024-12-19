project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
#set(CMAKE_BUILD_TYPE Debug) 
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

add_subdirectory(googletest)

add_executable(hce_ut 
    #test_helpers.hpp
    #atomic_ut.cpp 
    memory_ut.cpp
    #circular_buffer_ut.cpp
    #pool_allocator_ut.cpp 
    #list_ut.cpp 
    #synchronized_list_ut.cpp 
    #id_ut.cpp
    #chrono_ut.cpp
    #coroutine_ut.cpp
    scheduler_schedule_ut.cpp
    #scheduler_timer_ut.cpp
    #scheduler_block_ut.cpp
    #scope_ut.cpp
    #channel_ut.cpp 
    #threadpool_ut.cpp
    comparison_ut.cpp
    main_ut.cpp
)

target_include_directories(hce_ut PUBLIC ${HCE_INCLUDE_DIR} PRIVATE ${LOG_SOURCE_DIR})
target_link_libraries(hce_ut PRIVATE gtest hce)
target_compile_definitions(hce_ut PRIVATE -DHCELOGLIMIT=${HCELOGLIMIT} -DHCETHREADLOCALMEMORYBUCKETCOUNT=${HCETHREADLOCALMEMORYBUCKETCOUNT} -DHCETHREADLOCALMEMORYBUCKETBYTELIMIT=${HCETHREADLOCALMEMORYBUCKETBYTELIMIT})
