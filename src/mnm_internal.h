#pragma once

#include <stdint.h>    // uint*_t

#include <array>       // array
#include <span>        // span
#include <vector>      // vector

#include <bgfx/bgfx.h> // bgfx::*


namespace mnm
{

// -----------------------------------------------------------------------------
// RESOURCE LIMITS
// -----------------------------------------------------------------------------

// TODO : Ideally these are overridable by user via preprocessor directives. 

constexpr uint32_t MAX_MESHES            = 4096;


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
// THREAD-LOCAL CONTEXT
// -----------------------------------------------------------------------------

struct ThreadLocalContext
{
    std::vector<uint8_t> double_frame_memory;
    ArenaAllocator       frame_allocator;

    void init(uint32_t frame_memory);

    void cleanup();

    void swap_frame_allocator_memory();
};


// -----------------------------------------------------------------------------
// GLOBAL CONTEXT
// -----------------------------------------------------------------------------

struct GlobalContext
{
    MeshCache         mesh_cache;
    VertexLayoutCache vertex_layout_cache;

    void init();

    void cleanup();
};


// -----------------------------------------------------------------------------

} // namespace mnm
