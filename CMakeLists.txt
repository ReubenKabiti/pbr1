cmake_minimum_required(VERSION 3.0)

project(sm)

add_subdirectory(glew/build/cmake)
add_subdirectory(glfw/)

add_executable(sm main.cpp)

set_target_properties(sm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

target_include_directories(sm PUBLIC "." glfw/include glew/include)
target_link_libraries(sm PUBLIC glew_s glfw opengl32.lib)

# Force binary output to project root for all build types
foreach(OUTPUTCONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(sm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}
    )
endforeach()
