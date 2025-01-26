project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release) 
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall -Wfatal-errors")

# Add flags specific to Debug build
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")

add_subdirectory(googletest)

add_executable(hce_ut 
    main.cpp
    test_helpers.hpp
    test_memory_helpers.hpp
    atomic_ut.cpp 
    memory_ut.cpp
    circular_buffer_ut.cpp
    pool_allocator_ut.cpp 
    list_ut.cpp 
    synchronized_list_ut.cpp 
    id_ut.cpp
    coroutine_ut.cpp
    scheduler_ut.cpp
    #timer_ut.cpp
    #blocking_ut.cpp
    #scope_ut.cpp
    #channel_ut.cpp 
    threadpool_ut.cpp
    #comparison_ut.cpp
)

target_include_directories(hce_ut PUBLIC ${HCE_INCLUDE_DIR} PRIVATE ${LOG_SOURCE_DIR})
target_link_libraries(hce_ut PRIVATE gtest hce)
target_compile_definitions(hce_ut PRIVATE -DHCELOGLIMIT=${HCELOGLIMIT} -DHCETHREADLOCALMEMORYBUCKETCOUNT=${HCETHREADLOCALMEMORYBUCKETCOUNT} -DHCETHREADLOCALMEMORYBUCKETBYTELIMIT=${HCETHREADLOCALMEMORYBUCKETBYTELIMIT})
