set(NAME mnm)

add_library(${NAME} STATIC
    mnm_internal.h
    mnm_lib.cpp
)

target_include_directories(${NAME} PUBLIC
    ../include
)

target_link_libraries(${NAME}
    PUBLIC
        bgfx
    PRIVATE
        bx
        meshoptimizer
)

if(MSVC)
    target_compile_definitions(${NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(${NAME} PRIVATE
        /Wall
    )
else()
    target_compile_options(${NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(${NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON
    DEBUG_POSTFIX "_d"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin$<$<CONFIG:Debug>:>" # The dummy generator expression
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin$<$<CONFIG:Debug>:>" # is to avoid having to list all
)

include(cmake/add_shader_dependency.cmake)
