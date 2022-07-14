#pragma once

#include <stdint.h>       // uint*_t

#include <array>          // array
#include <span>           // span
#include <vector>         // vector

#include <bgfx/bgfx.h>    // bgfx::*

#include <GLFW/glfw3.h>   // GLFWwindow

#include <HandmadeMath.h> // hmm_*

namespace mnm
{

// -----------------------------------------------------------------------------
// RESOURCE LIMITS
// -----------------------------------------------------------------------------

// TODO : Ideally these are overridable by user via preprocessor directives. 

constexpr uint32_t MAX_MATRIX_STACK_DEPTH = 16;

constexpr uint32_t MAX_MESHES             = 4096;

constexpr uint32_t MAX_PASSES             = 48;


// -----------------------------------------------------------------------------
// MEMORY ALLOCATION
// -----------------------------------------------------------------------------

struct ArenaAllocator
{
    std::span<uint8_t> buffer;
    uint32_t           offset;

    void init(std::span<uint8_t> buffer);

    void restart();

    std::span<uint8_t> allocate(uint32_t size, uint32_t alignment = 0);
};

struct PoolAllocator
{
    std::span<uint8_t> buffer;
    uint32_t           offset;
    uint32_t           item_size;

    void init(std::span<uint8_t> buffer, uint32_t item_size, uint32_t item_alignment);

    void restart();

    std::span<uint8_t> allocate(uint32_t count = 1);
};


// -----------------------------------------------------------------------------
// VERTEX LAYOUTS
// -----------------------------------------------------------------------------

struct VertexLayoutCache
{
    std::array<bgfx::VertexLayout, 8> layouts;

    const bgfx::VertexLayout& operator[](uint32_t flags) const;

    void init();
};


// -----------------------------------------------------------------------------
// VERTEX RECORDING
// -----------------------------------------------------------------------------

struct VertexState
{
    uint64_t  blob[3];
    uint32_t  size;

    float*    position;
    uint32_t* color;
    uint32_t* texcoord;
    uint32_t* normal;

    void reset(const bgfx::VertexLayout& layout);
};

struct VertexRecorder
{
    VertexState   vertex_state;
    PoolAllocator allocator;
    uint32_t      vertex_count;
    uint32_t      invocation_count;
    bool          emulate_quads;

    void reset(uint32_t flags, const bgfx::VertexLayout& layout, std::span<uint8_t> buffer);

    void push_current_vertex();

    std::span<const uint8_t> buffer() const;
};


// -----------------------------------------------------------------------------
// MESH
// -----------------------------------------------------------------------------

enum struct MeshType
{
    STATIC,
    TRANSIENT,
};

struct MeshDesc
{
    std::span<const uint8_t>  buffer;
    const bgfx::VertexLayout* layout; 
    uint32_t                  flags;
};

union Mesh
{
    bgfx::TransientVertexBuffer* transient_vertex_buffer;
    bgfx::VertexBufferHandle     static_vertex_buffer;
    bgfx::IndexBufferHandle      static_index_buffer;
    uint16_t                     element_count;
    uint16_t                     annotation;

    MeshType type() const;

    bool is_valid() const;

    void create(const MeshDesc& desc, ArenaAllocator& allocator);

    void destroy();
};

struct MeshCache
{
    std::array<Mesh, MAX_MESHES> meshes;

    void init();

    void cleanup();

    void add_mesh(uint32_t id, const Mesh& mesh);

    void invalidate_transient_meshes();
};


// -----------------------------------------------------------------------------
// DEFAULT PROGRAMS
// -----------------------------------------------------------------------------

struct DefaultProgramCache
{
    std::array<bgfx::ProgramHandle, 8> programs;

    bgfx::ProgramHandle operator[](uint32_t flags) const;

    void init(bgfx::RendererType::Enum renderer = bgfx::RendererType::Count);

    void cleanup();
};


// -----------------------------------------------------------------------------
// DEFAULT UNIFORMS
// -----------------------------------------------------------------------------

enum struct DefaultUniform : uint32_t
{
    COLOR_TEXTURE_RGBA,
};

struct DefaultUniformCache
{
    std::array<bgfx::UniformHandle, 1> uniforms;

    bgfx::UniformHandle operator[](DefaultUniform uniform) const;

    void init();

    void cleanup();
};


// -----------------------------------------------------------------------------
// MATRIX STACK
// -----------------------------------------------------------------------------

struct MatrixStack
{
    hmm_mat4                                     top;
    uint32_t                                     size;
    std::array<hmm_mat4, MAX_MATRIX_STACK_DEPTH> matrices;

    void init();

    void push();

    void pop();

    void multiply_top(const hmm_mat4& matrix);
};


// -----------------------------------------------------------------------------
// PASSES
// -----------------------------------------------------------------------------

struct Pass
{
    enum : uint8_t
    {
        DIRTY_NONE        = 0x00,
        DIRTY_CLEAR       = 0x01,
        DIRTY_TOUCH       = 0x02,
        DIRTY_TRANSFORM   = 0x04,
        DIRTY_RECT        = 0x08,
        DIRTY_FRAMEBUFFER = 0x10,
    };

    hmm_mat4                view_matrix;
    hmm_mat4                proj_matrix;

    uint16_t                viewport_x;
    uint16_t                viewport_y;
    uint16_t                viewport_width;
    uint16_t                viewport_height;

    bgfx::FrameBufferHandle framebuffer;

    uint16_t                clear_flags;
    float                   clear_depth;
    uint32_t                clear_rgba;
    uint8_t                 clear_stencil;

    uint8_t                 dirty_flags;

    void init();

    void update(bgfx::ViewId id, bool backbuffer_size_changed);

    void touch();

    void set_view(const hmm_mat4& matrix);

    void set_projection(const hmm_mat4& matrix);

    void set_framebuffer(bgfx::FrameBufferHandle framebuffer);

    void set_no_clear();

    void set_clear_depth(float depth);

    void set_clear_color(uint32_t rgba);

    void set_viewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
};

struct PassCache
{
    std::array<Pass, MAX_PASSES> passes;
    bool                         backbuffer_size_changed;

    Pass& operator[](bgfx::ViewId id);

    void init();

    void update();
};


// -----------------------------------------------------------------------------
// TIME MEASUREMENT
// -----------------------------------------------------------------------------

struct Timer
{
    int64_t counter;
    double  elapsed;

    void tic();

    void tic(int64_t now);

    double toc(bool restart = false);

    double toc(int64_t now, bool restart = false);
};


// -----------------------------------------------------------------------------
// PLATFORM-SPECIFIC STUFF
// -----------------------------------------------------------------------------

bgfx::PlatformData create_platform_data(GLFWwindow* window, bgfx::RendererType::Enum renderer);


// -----------------------------------------------------------------------------
// THREAD-LOCAL CONTEXT
// -----------------------------------------------------------------------------

struct ThreadLocalContext
{
    std::vector<uint8_t> double_frame_memory;
    ArenaAllocator       frame_allocator;
    MatrixStack          matrix_stack;

    void init(uint32_t frame_memory);

    void cleanup();

    void swap_frame_allocator_memory();
};


// -----------------------------------------------------------------------------
// GLOBAL CONTEXT
// -----------------------------------------------------------------------------

struct GlobalContext
{
    MeshCache           meshes;
    PassCache           passes;
    VertexLayoutCache   vertex_layouts;

    // These ones require BGFX to be set up.
    DefaultUniformCache default_uniforms;
    DefaultProgramCache default_programs;

    void init();

    void cleanup();
};


// -----------------------------------------------------------------------------

} // namespace mnm
