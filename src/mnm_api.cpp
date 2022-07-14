#include "mnm_internal.h"

#include <mutex>             // lock_guard, mutex

#include <bx/bx.h>           // BX_CONCATENATE

#include <imgui.h>           // ImGui::*
#include <imgui_impl_glfw.h> // ...

#include <mnm.h>

namespace mnm
{

// -----------------------------------------------------------------------------
// DEFERRED EXECUTION
// -----------------------------------------------------------------------------

template <typename Func>
struct Deferred
{
    Func func;

    Deferred(const Deferred&) = delete;

    Deferred& operator=(const Deferred&) = delete;

    Deferred(Func&& func)
        : func(static_cast<Func&&>(func))
    {
    }

    ~Deferred()
    {
        func();
    }
};

template <typename Func>
Deferred<Func> make_deferred(Func&& func)
{
    return Deferred<Func>(static_cast<decltype(func)>(func));
}

#define defer(...) auto BX_CONCATENATE(deferred_ , __LINE__) = \
    ::mnm::make_deferred([&]() mutable { __VA_ARGS__; })


// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------

static std::mutex                g_mutex;

static GlobalContext*            g_ctx = nullptr;

thread_local ThreadLocalContext* t_ctx = nullptr;


// -----------------------------------------------------------------------------
// MAIN MiNiMo ENTRY POINT
// -----------------------------------------------------------------------------

int run(void (* init)(void), void (* setup)(void), void (* update)(void), void (* cleanup)(void))
{
    // TODO : Top level exception catching.

    std::lock_guard<std::mutex> lock(g_mutex);

    if (init)
    {
        // NOTE : No context is set up at this point and almost no MiNiMo
        //        infrastructure work.
        init();
    }

    if (glfwInit() != GLFW_TRUE)
    {
        return 1;
    }
    defer(glfwTerminate());

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE); // NOT : This will be ignored when `glfwSetWindowSize` is specified.

    GLFWwindow* window = glfwCreateWindow(800, 600, "MiNiMo", nullptr, nullptr);
    if (!window)
    {
        return 2;
    }
    defer(glfwDestroyWindow(window));

    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        bgfx::Init init;
        init.platformData           = create_platform_data(window, init.type);
        init.resolution.width       = uint32_t(width);
        init.resolution.height      = uint32_t(height);
        init.limits.transientVbSize = 32 * 1024 * 1024; // TODO : Honor user settings.

        if (!bgfx::init(init))
        {
            return 3;
        }
    }
    defer(bgfx::shutdown());

    uint32_t debug_state = BGFX_DEBUG_NONE;
    bgfx::setDebug(debug_state);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    defer(ImGui::DestroyContext());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOther(window, true);
    defer(ImGui_ImplGlfw_Shutdown());

    // ...

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!io.WantCaptureMouse && !io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            break;
        }

        // ...

        ImGui::Render();

        bgfx::frame();
    }

    return 0;
}

} // namespace mnm
