project(hce_ut) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(HCE_UT_BIN_DIR "${CMAKE_CURRENT_LIST_DIR}/ut_bin")

add_subdirectory(googletest)

set(HCE_UT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_helpers.hpp
    ${CMAKE_CURRENT_LIST_DIR}/test_blocking_helpers.hpp
    ${CMAKE_CURRENT_LIST_DIR}/test_module_helpers.hpp
    ${CMAKE_CURRENT_LIST_DIR}/lifecycle_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/atomic_ut.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/allocator_ut.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/circular_buffer_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/list_ut.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/synchronized_list_ut.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/id_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/coroutine_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/scheduler_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/scope_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/channel_ut.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/threadpool_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/comparison_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/blocking_ut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/module_ut.cpp
    )

if(TIME_SENSITIVE_TESTS_ENABLED)
    # for tests which can be controlled with compiler flag
    set(HCETESTENABLETIMESENSITIVE true)

    # add timers
    list(APPEND HCE_UT_SOURCES 
        ${CMAKE_CURRENT_LIST_DIR}/timer_ut.cpp 
    )
else()
    set(HCETESTENABLETIMESENSITIVE false)
endif()

set(HCE_UT_COMPILE_DEFINES ${HCE_COMPILE_DEFINES} -DHCETESTENABLETIMESENSITIVE=${HCETESTENABLETIMESENSITIVE})

# add module unit test shared library
add_library(module_ut_shared 
            SHARED 
            ${CMAKE_CURRENT_LIST_DIR}/test_module_helpers.hpp
            ${CMAKE_CURRENT_LIST_DIR}/module_ut_shared.cpp)
target_link_libraries(module_ut_shared PRIVATE hce)
target_compile_definitions(module_ut_shared PRIVATE ${HCE_UT_COMPILE_DEFINES})
set_target_properties(module_ut_shared PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${HCE_UT_BIN_DIR}" )

# add main unit test executable
add_executable(hce_ut ${HCE_UT_SOURCES})
target_include_directories(hce_ut PUBLIC ${HCE_INCLUDE_DIR} PRIVATE ${LOG_SOURCE_DIR})
target_link_libraries(hce_ut PRIVATE gtest hce)
target_compile_definitions(hce_ut PRIVATE ${HCE_UT_COMPILE_DEFINES})
target_compile_definitions(hce_ut PRIVATE MODULE_UT_SHARED_PATH="$<TARGET_FILE:module_ut_shared>")
add_dependencies(hce_ut module_ut_shared)
set_target_properties(hce_ut PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${HCE_UT_BIN_DIR}" )

SplitOnWhitespace("${HCE_UT_SOURCES}" PRETTY_HCE_UT_SOURCES)
SplitOnWhitespace("${HCE_UT_COMPILE_DEFINES}" PRETTY_HCE_UT_COMPILE_DEFINES)

message("-- HCE UNIT TESTS -- ")
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
message("-- HCE UNIT TEST SOURCES -- ")

foreach(item IN LISTS PRETTY_HCE_UT_SOURCES)
    message("${item}")
endforeach()

message("-- HCE UNIT TEST COMPILE DEFINES -- ")

foreach(item IN LISTS PRETTY_HCE_UT_COMPILE_DEFINES)
    message("${item}")
endforeach()
