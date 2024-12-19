project(hce_ex) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
#set(CMAKE_BUILD_TYPE Debug) 
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

set(EX_LIBS
    pthread
    hce 
    )

set(EXAMPLE_NUMS
    001
    )

foreach(example_num ${EXAMPLE_NUMS})
    set(EXAMPLES ${EXAMPLES} "example_${example_num}")
endforeach()

function(add_example_executable example)
    add_executable(${example} src/${example}.cpp)
    target_link_libraries(${example} ${EX_LIBS})
    target_compile_definitions(${example} PRIVATE -DHCELOGLIMIT=${HCELOGLIMIT})
endfunction() 

foreach(example ${EXAMPLES})
    add_example_executable(${example})
endforeach()

add_custom_target(hce_ex)
add_dependencies(hce_ex ${EXAMPLES})
