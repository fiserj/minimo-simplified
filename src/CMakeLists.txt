set(NAME mnm)

set(SOURCE_FILES
    mnm_internal.h
    mnm_api.cpp
    mnm_lib.cpp
)

if(APPLE)
    list(APPEND SOURCE_FILES mnm_platform.mm)
else()
    list(APPEND SOURCE_FILES mnm_platform.cpp)
endif()

add_library(${NAME} STATIC
    ${SOURCE_FILES}
)

target_include_directories(${NAME} PUBLIC
    ../include
)

target_link_libraries(${NAME} PRIVATE
    bgfx
    bx
    glfw
    HandmadeMath
    imgui
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

add_shader_dependency(${NAME} "shaders/position.vs"                 )
add_shader_dependency(${NAME} "shaders/position.fs"                 )
add_shader_dependency(${NAME} "shaders/position_color.vs"           )
add_shader_dependency(${NAME} "shaders/position_color.fs"           )
add_shader_dependency(${NAME} "shaders/position_color_normal.vs"    )
add_shader_dependency(${NAME} "shaders/position_color_normal.fs"    )
add_shader_dependency(${NAME} "shaders/position_color_texcoord.vs"  )
add_shader_dependency(${NAME} "shaders/position_color_texcoord.fs"  )
add_shader_dependency(${NAME} "shaders/position_normal.vs"          )
add_shader_dependency(${NAME} "shaders/position_normal.fs"          )
add_shader_dependency(${NAME} "shaders/position_texcoord.vs"        )
add_shader_dependency(${NAME} "shaders/position_texcoord.fs"        )
