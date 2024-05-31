project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

add_subdirectory(googletest)

add_executable(hce_ut 
    atomic_ut.cpp 
    circular_buffer_ut.cpp
)

target_link_libraries(hce_ut gtest gtest_main hce)
