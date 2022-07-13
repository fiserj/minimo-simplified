add_library(HandmadeMath INTERFACE)

target_include_directories(HandmadeMath INTERFACE
    ${handmademath_SOURCE_DIR}
)

target_compile_options(HandmadeMath INTERFACE
    -Wno-nested-anon-types
)