macro(TURN_OFF option)
    set(${option} OFF CACHE INTERNAL "" FORCE)
endmacro()

TURN_OFF(ENKITS_BUILD_C_INTERFACE)
TURN_OFF(ENKITS_BUILD_EXAMPLES)
TURN_OFF(ENKITS_BUILD_SHARED)
TURN_OFF(ENKITS_INSTALL)

TURN_OFF(GLFW_BUILD_DOCS)
TURN_OFF(GLFW_BUILD_EXAMPLES)
TURN_OFF(GLFW_BUILD_TESTS)
TURN_OFF(GLFW_DOCUMENT_INTERNALS)
TURN_OFF(GLFW_INSTALL)
TURN_OFF(GLFW_VULKAN_STATIC)

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
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        62e175ef9fae75335575964c845a302447c012c7
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        250333d895b1067533533dcfab137512745b9689
)

FetchContent_Declare(
    HandmadeMath
    GIT_REPOSITORY https://github.com/HandmadeMath/Handmade-Math.git
    GIT_TAG        v1.13.0
)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.17
)

FetchContent_MakeAvailable(
    glfw
)

FetchContent_Populate(bgfx)
FetchContent_Populate(bimg)
FetchContent_Populate(bx)
FetchContent_Populate(HandmadeMath)
FetchContent_Populate(imgui)
FetchContent_Populate(meshoptimizer)

include(cmake/bgfx.cmake)
include(cmake/bimg.cmake)
include(cmake/bx.cmake)
include(cmake/HandmadeMath.cmake)
include(cmake/imgui.cmake)
include(cmake/meshoptimizer.cmake)