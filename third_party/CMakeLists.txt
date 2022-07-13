include(FetchContent)

FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.git
    GIT_TAG        3cc0de333528d546f1518ebb9111233eee168022
)

FetchContent_Declare(
    bimg
    GIT_REPOSITORY https://github.com/bkaradzic/bimg.git
    GIT_TAG        663f724186e26caf46494e389ed82409106205fb
)

FetchContent_Declare(
    bx
    GIT_REPOSITORY https://github.com/bkaradzic/bx.git
    GIT_TAG        ad018d47c6c107e2fe2f3ba0716f9e728ed59a39
)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.17
)

FetchContent_Populate(bgfx)
FetchContent_Populate(bimg)
FetchContent_Populate(bx)
FetchContent_Populate(meshoptimizer)

include(cmake/bgfx.cmake)
include(cmake/bimg.cmake)
include(cmake/bx.cmake)
include(cmake/meshoptimizer.cmake)