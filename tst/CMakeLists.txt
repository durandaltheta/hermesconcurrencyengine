project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
#set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

# set HCELOGLIMIT
if(DEFINED HCELOGLIMIT)
else()
    set(HCELOGLIMIT 0) 
endif()

add_subdirectory(googletest)

add_executable(hce_ut 
    atomic_ut.cpp 
    circular_buffer_ut.cpp
    coroutine_ut.cpp
    scheduler_ut.cpp
)

target_include_directories(hce_ut PUBLIC ${HCE_INCLUDE_DIR} PRIVATE ${LOG_SOURCE_DIR})
target_link_libraries(hce_ut PRIVATE gtest gtest_main hce)

message("-- HCE UNIT TEST COMPILE DEFINES -- ")
message("-DHCELOGLIMIT:${HCELOGLIMIT}")

target_compile_definitions(
    hce_ut
    PRIVATE 
    -DHCELOGLIMIT=${HCELOGLIMIT})
