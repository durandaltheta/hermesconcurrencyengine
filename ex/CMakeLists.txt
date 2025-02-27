project(hce_ex) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HCE_EX_COMPILE_DEFINES -DHCELOGLEVEL=${HCELOGLEVEL} -DHCELOGLIMIT=${HCELOGLIMIT})

set(EX_LIBS
    pthread
    hce 
    )

set(EXAMPLE_NUMS
    001
    002
    )

#-------------------------------------------------------------------------------
# example executables

foreach(example_num ${EXAMPLE_NUMS})
    set(EXAMPLES ${EXAMPLES} "example_${example_num}")
endforeach()

function(add_example_executable example)
    add_executable(${example} src/${example}.cpp)
    target_link_libraries(${example} ${EX_LIBS})
    target_compile_definitions(${example} PRIVATE ${HCE_EX_COMPILE_DEFINES})
endfunction() 

foreach(example ${EXAMPLES})
    add_example_executable(${example})
endforeach()

#-------------------------------------------------------------------------------
# shared example libs

set(SHARED_EXAMPLE_NUMS
    002
    )

foreach(shared_example_num ${SHARED_EXAMPLE_NUMS})
    set(SHARED_EXAMPLES ${SHARED_EXAMPLES} "example_${shared_example_num}_shared")
endforeach()

function(add_shared_example_executable shared_example)
    add_library(${shared_example} SHARED src/${shared_example}.cpp)
    target_link_libraries(${shared_example} ${EX_LIBS})
    target_compile_definitions(${shared_example} PRIVATE ${HCE_EX_COMPILE_DEFINES})
endfunction() 

foreach(shared_example ${SHARED_EXAMPLES})
    add_shared_example_executable(${shared_example})
endforeach()

#-------------------------------------------------------------------------------
# example target 

SplitOnWhitespace("${HCE_EX_COMPILE_DEFINES}" PRETTY_HCE_EX_COMPILE_DEFINES)

message("-- HCE EXAMPLE COMPILE DEFINES -- ")

foreach(item IN LISTS PRETTY_HCE_EX_COMPILE_DEFINES)
    message("${item}")
endforeach()

add_custom_target(hce_ex)
add_dependencies(hce_ex ${EXAMPLES} example_002_shared)
