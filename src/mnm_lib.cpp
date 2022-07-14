#include "mnm_internal.h"

#include <inttypes.h>             // PRI*
#include <stddef.h>               // max_align_t, size_t
#include <string.h>               // memcpy

#include <bgfx/embedded_shader.h> // BGFX_EMBEDDED_SHADER

#include <bx/allocator.h>         // alignPtr
#include <bx/bx.h>                // BX_ASSERT, BX_WARN, isPowerOf2
#include <bx/timer.h>             // getHPCounter, getHPFrequency

#include <meshoptimizer.h>        // meshopt_*

#include <mnm.h>

#include "mnm_shaders.h"          // *_fs, *_vs

namespace mnm
{

// -----------------------------------------------------------------------------
// ASSERTIONS
// -----------------------------------------------------------------------------

#define ASSERT  BX_ASSERT
#define REQUIRE ASSERT // TODO : Make sure this is enabled in all builds.
#define TRACE   BX_TRACE
#define WARN    BX_WARN


// -----------------------------------------------------------------------------
// MEMORY ALLOCATION
// -----------------------------------------------------------------------------

void ArenaAllocator::init(std::span<uint8_t> buffer_)
{
    ASSERT(
        !buffer_.empty(),
        "Empty arena buffer."
    );

    *this = {};

    buffer = buffer_;
}

void ArenaAllocator::restart()
{
    offset = 0;
}

std::span<uint8_t> ArenaAllocator::allocate(uint32_t size, uint32_t alignment)
{
    if (alignment == 0)
    {
        alignment = alignof(max_align_t);
    }

    ASSERT(
        bx::isPowerOf2(alignment),
        "Alignment %" PRIu32 " not a power of two.",
        alignment
    );

    uint8_t* ptr = reinterpret_cast<uint8_t*>(bx::alignPtr(buffer.data() + offset, 0, alignment));
    const uintptr_t head = ptr - buffer.data(); 

    if (head + size <= buffer.size())
    {
        offset = head + size;

        return { ptr, size };
    }

    return {};
}

void PoolAllocator::init(std::span<uint8_t> buffer_, uint32_t item_size_, uint32_t item_alignment)
{
    ASSERT(
        !buffer_.empty(),
        "Empty arena buffer."
    );

    ASSERT(
        item_size_ > 0,
        "Zero item size.",
        item_size_
    );

    ASSERT(
        bx::isPowerOf2(item_alignment),
        "Item alignment %" PRIu32 " not a power of two.",
        item_alignment
    );

    *this = {};

    uint8_t* aligned = reinterpret_cast<uint8_t*>(bx::alignPtr(buffer_.data(), 0, item_alignment));

    item_size = item_size_;

    if (aligned < buffer_.data() + buffer_.size())
    {
        const size_t diff = aligned - buffer_.data();

        buffer = { aligned, buffer_.size() - diff };
    }
}

void PoolAllocator::restart()
{
    offset = 0;
}

std::span<uint8_t> PoolAllocator::allocate(uint32_t count)
{
    const uint32_t size = count * item_size;

    if (offset + size <= buffer.size())
    {
        uint8_t* data = buffer.data() + offset;

        offset += size;

        return { data, size };
    }

    return {};
}

template <typename T>
void allocate(T*& ptr, ArenaAllocator& allocator, uint32_t count = 1)
{
    ptr = reinterpret_cast<T*>(allocator.allocate(sizeof(T) * count, alignof(T)).data());
}

static void free_bgfx_memory(void*, void*)
{
    // NOTE : We do nothing, since the memory was allocated from an arena.
}

static const bgfx::Memory* allocacte_bgfx_memory(ArenaAllocator& allocator, uint32_t size)
{
    return bgfx::makeRef(allocator.allocate(size).data(), size, free_bgfx_memory);
}


// -----------------------------------------------------------------------------
// VERTEX LAYOUTS
// -----------------------------------------------------------------------------

static constexpr uint32_t VERTEX_POSITION     = VERTEX_COLOR >> 1;

static constexpr uint32_t VERTEX_ATTRIB_SHIFT = 6;

static constexpr uint32_t VERTEX_ATTRIB_MASK  = VERTEX_COLOR    |
                                                VERTEX_NORMAL   |
                                                VERTEX_TEXCOORD ;
struct VertexLayoutDesc
{
    uint32_t               flag;
    bgfx::Attrib::Enum     type;
    bgfx::AttribType::Enum element_type;
    uint8_t                element_count;
    bool                   normalized;
    bool                   packed;
    uint8_t                state_attrib_offset;
};

static const VertexLayoutDesc s_vertex_layout_descs[] =
{
    { VERTEX_POSITION, bgfx::Attrib::Position , bgfx::AttribType::Float, 3, false, false, offsetof(VertexState, position) },
    { VERTEX_COLOR   , bgfx::Attrib::Color0   , bgfx::AttribType::Uint8, 4, true , false, offsetof(VertexState, color   ) },
    { VERTEX_NORMAL  , bgfx::Attrib::Normal   , bgfx::AttribType::Uint8, 4, true , true , offsetof(VertexState, normal  ) },
    { VERTEX_TEXCOORD, bgfx::Attrib::TexCoord0, bgfx::AttribType::Int16, 2, true , true , offsetof(VertexState, texcoord) },
};

const bgfx::VertexLayout& VertexLayoutCache::operator[](uint32_t flags) const
{
    static_assert(
        (VERTEX_ATTRIB_MASK >> VERTEX_ATTRIB_SHIFT) == 0b00000111,
        "Invalid assumption about vertex attribute mask bits."
    );

    const uint32_t index = (flags & VERTEX_ATTRIB_MASK) >> VERTEX_ATTRIB_SHIFT;

    return layouts[index];
}

void VertexLayoutCache::init()
{
    for (uint32_t i = 0; i < layouts.size(); i++)
    {
        const uint32_t attribs = (i << VERTEX_ATTRIB_SHIFT) | VERTEX_POSITION;

        bgfx::VertexLayout& layout = layouts[i];
        layout.begin();

        for (const VertexLayoutDesc& desc : s_vertex_layout_descs)
        {
            if (attribs & desc.flag)
            {
                layout.add(desc.type, desc.element_count, desc.element_type, desc.normalized, desc.packed);
            }
        }

        layout.end();
    }
}


// -----------------------------------------------------------------------------
// VERTEX RECORDING
// -----------------------------------------------------------------------------

static uint64_t s_unused_vertex_attrib_sink[2];

void VertexState::reset(const bgfx::VertexLayout& layout)
{
    *this = {};

    for (const VertexLayoutDesc& desc : s_vertex_layout_descs)
    {
        union Attrib
        {
            uintptr_t addr;
            void**    ptr;
        };
        
        Attrib attrib;
        attrib.addr = uintptr_t(this) + desc.state_attrib_offset;

        if (layout.has(desc.type))
        {
            const uint32_t blob_offset = layout.getOffset(desc.type);

            *attrib.ptr = reinterpret_cast<uint8_t*>(blob) + blob_offset;
        }
        else
        {
            *attrib.ptr = &s_unused_vertex_attrib_sink;
        }
    }
}

void VertexRecorder::reset(uint32_t flags, const bgfx::VertexLayout& layout, std::span<uint8_t> buffer)
{
    *this = {};

    vertex_state.reset(layout);

    allocator.init(buffer, vertex_state.size, alignof(uint32_t));

    emulate_quads = flags & PRIMITIVE_QUADS;
}

void VertexRecorder::push_current_vertex()
{
    if (emulate_quads)
    {
        if ((invocation_count & 3) == 3)
        {
            ASSERT(
                vertex_count % 3 == 0,
                "Expected 3 outstanding vertices, but got " PRIu32 ".",
                vertex_count % 3
            );

            std::span<uint8_t> blob = allocator.allocate(2);
            ASSERT(
                !blob.empty(),
                "Vertex recorder full."
            );

            const uint32_t size = vertex_state.size;
            uint8_t*       end  = blob.data() + blob.size();

            // Assuming the last triangle has relative indices
            // [v0, v1, v2] = [-5, -4, -3], we need to copy the vertices v0 and v2.
            memcpy(end - 2 * size, end - 5 * size, size);
            memcpy(end -     size, end - 3 * size, size);

            vertex_count += 2;
        }

        invocation_count++;
    }

    std::span<uint8_t> dst = allocator.allocate();
    memcpy(dst.data(), vertex_state.blob, vertex_state.size);

    vertex_count++;
}

std::span<const uint8_t> VertexRecorder::buffer() const
{
    return { allocator.buffer.data(), vertex_state.size * vertex_count };
}


// -----------------------------------------------------------------------------
// MESH
// -----------------------------------------------------------------------------

static constexpr uint32_t PRIMITIVE_TYPE_MASK = PRIMITIVE_TRIANGLES |
                                                PRIMITIVE_QUADS     |
                                                PRIMITIVE_LINES     ;

MeshType Mesh::type() const
{
    return MeshType(transient_vertex_buffer != nullptr);
}

bool Mesh::is_valid() const
{
    return element_count > 0;
}

void Mesh::create(const MeshDesc& desc, ArenaAllocator& allocator)
{
    *this = {};

    const uint32_t vertex_size  = desc.layout->getStride();
    const uint32_t vertex_count = desc.buffer.size() / vertex_size;
    REQUIRE(
        vertex_count < UINT16_MAX,
        "Too many vertices (%" PRIu32 ").",
        vertex_count
    );

    if (desc.flags & MESH_TRANSIENT)
    {
        bgfx::TransientVertexBuffer* buffer;
        allocate(buffer, allocator);
        REQUIRE(
            buffer != nullptr,
            "Failed to allocate transient buffer structure."
        );

        bgfx::allocTransientVertexBuffer(buffer, vertex_count, *desc.layout);
        const uint32_t allocated_vertex_count = buffer->size / buffer->stride;
        WARN(
            allocated_vertex_count == vertex_count,
            "Failed to allocate enough transient vertices."
        );

        if (allocated_vertex_count == vertex_count)
        {
            memcpy(buffer->data, desc.buffer.data(), buffer->size);

            transient_vertex_buffer = buffer;
            element_count = allocated_vertex_count;
        }

        return;
    }

    // TODO : Set custom meshopt allocator (thread-local stack allocator).

    const uint32_t index_count = vertex_count;

    uint32_t* remap_table = nullptr;
    allocate(remap_table, allocator, index_count);
    REQUIRE(
        remap_table != nullptr,
        "Failed to allocate vertex remap table."
    );

    const uint32_t indexed_vertex_count = uint32_t(meshopt_generateVertexRemap(
        remap_table,
        nullptr,
        index_count,
        desc.buffer.data(),
        index_count,
        vertex_size
    ));

    const bgfx::Memory* indices = allocacte_bgfx_memory(allocator, indexed_vertex_count * vertex_size);
    REQUIRE(
        indices && indices->data,
        "Failed to allocate remapped index buffer memory."
    );

    const bgfx::Memory* vertices = allocacte_bgfx_memory(allocator, indexed_vertex_count * vertex_size);
    REQUIRE(
        vertices && vertices->data,
        "Failed to allocate remapped vertex buffer memory."
    );

    uint32_t* indices_u32 = reinterpret_cast<uint32_t*>(indices->data);

    meshopt_remapIndexBuffer(indices_u32, nullptr, index_count, remap_table);

    meshopt_remapVertexBuffer(vertices->data, desc.buffer.data(), indexed_vertex_count, vertex_size, remap_table);

    const bool optimize_geometry =
         (desc.flags & OPTIMIZE_GEOMETRY  ) &&
        ((desc.flags & PRIMITIVE_TYPE_MASK) != PRIMITIVE_LINES);

    if (optimize_geometry)
    {
        meshopt_optimizeVertexCache(indices_u32, indices_u32, index_count, indexed_vertex_count);

        meshopt_optimizeOverdraw(indices_u32, indices_u32, index_count, reinterpret_cast<float*>(vertices->data), indexed_vertex_count, vertex_size, 1.05f);

        meshopt_optimizeVertexFetch(vertices->data, indices_u32, index_count, vertices->data, indexed_vertex_count, vertex_size);
    }

    uint16_t* indices_u16 = reinterpret_cast<uint16_t*>(indices->data);

    for (uint32_t i = 0; i < index_count; i++)
    {
        indices_u16[i] = uint16_t(indices_u32[i]);
    }

    const_cast<bgfx::Memory*>(indices)->size /= 2;

    static_vertex_buffer = bgfx::createVertexBuffer(vertices, *desc.layout);
    REQUIRE(
        bgfx::isValid(static_vertex_buffer),
        "Failed to create BGFX vertex buffer."
    );

    static_index_buffer = bgfx::createIndexBuffer(indices);
    REQUIRE(
        bgfx::isValid(static_index_buffer),
        "Failed to create BGFX index buffer."
    );

    element_count = index_count;
}

void Mesh::destroy()
{
    if (element_count && !transient_vertex_buffer)
    {
        bgfx::destroy(static_vertex_buffer);
        bgfx::destroy(static_index_buffer );
    }

    *this = {};
}

void MeshCache::init()
{
    *this = {};
}

void MeshCache::cleanup()
{
    for (Mesh& mesh : meshes)
    {
        mesh.destroy();
    }
}

void MeshCache::add_mesh(uint32_t id, const Mesh& mesh)
{
    // NOTE : Not thread safe because users shouldn't create mesh with the same
    //        ID from multiple threads in the first place.

    meshes[id].destroy();
    meshes[id] = mesh;
}

void MeshCache::invalidate_transient_meshes()
{
    for (Mesh& mesh : meshes)
    {
        if (mesh.transient_vertex_buffer)
        {
            mesh = {};
        }
    }
}


// -----------------------------------------------------------------------------
// DEFAULT PROGRAMS
// -----------------------------------------------------------------------------

struct DefaultProgramDesc
{
    uint32_t    attribs;
    const char* vs_name;
    const char* fs_name = nullptr;
};

static const DefaultProgramDesc s_default_program_descs[] =
{
    {
        0, // NOTE : Position only. It's assumed everywhere else.
        "position"
    },
    {
        VERTEX_COLOR,
        "position_color"
    },
    {
        VERTEX_COLOR | VERTEX_NORMAL,
        "position_color_normal"
    },
    {
        VERTEX_COLOR | VERTEX_TEXCOORD,
        "position_color_texcoord"
    },
    {
        VERTEX_NORMAL,
        "position_normal"
    },
    {
        VERTEX_TEXCOORD,
        "position_texcoord"
    },
};

static const bgfx::EmbeddedShader s_default_shaders[] =
{
    BGFX_EMBEDDED_SHADER(position_fs),
    BGFX_EMBEDDED_SHADER(position_vs),

    BGFX_EMBEDDED_SHADER(position_color_fs),
    BGFX_EMBEDDED_SHADER(position_color_vs),

    BGFX_EMBEDDED_SHADER(position_color_normal_fs),
    BGFX_EMBEDDED_SHADER(position_color_normal_vs),

    BGFX_EMBEDDED_SHADER(position_color_texcoord_fs),
    BGFX_EMBEDDED_SHADER(position_color_texcoord_vs),

    BGFX_EMBEDDED_SHADER(position_normal_fs),
    BGFX_EMBEDDED_SHADER(position_normal_vs),

    BGFX_EMBEDDED_SHADER(position_texcoord_fs),
    BGFX_EMBEDDED_SHADER(position_texcoord_vs),
};

bgfx::ProgramHandle DefaultProgramCache::operator[](uint32_t flags) const
{
    static_assert(
        (VERTEX_ATTRIB_MASK >> VERTEX_ATTRIB_SHIFT) == 0b00000111,
        "Invalid assumption about vertex attribute mask bits."
    );

    const uint32_t index = (flags & VERTEX_ATTRIB_MASK) >> VERTEX_ATTRIB_SHIFT;

    return programs[index];
}

void DefaultProgramCache::init(bgfx::RendererType::Enum renderer)
{
    *this = {};

    if (renderer == bgfx::RendererType::Count)
    {
        renderer = bgfx::getRendererType();
    }

    programs.fill(BGFX_INVALID_HANDLE);

    char vs_name[32];
    char fs_name[32];

    for (const DefaultProgramDesc& desc : s_default_program_descs)
    {
        strcpy(vs_name, desc.vs_name);
        strcat(vs_name, "_vs");

        strcpy(fs_name, desc.fs_name ? desc.fs_name : desc.vs_name);
        strcat(fs_name, "_fs");

        const bgfx::ShaderHandle vertex = bgfx::createEmbeddedShader(s_default_shaders, renderer, vs_name);
        REQUIRE(
            bgfx::isValid(vertex),
            "Invalid default vertex shader '%s'.",
            vs_name
        );

        const bgfx::ShaderHandle fragment = bgfx::createEmbeddedShader(s_default_shaders, renderer, fs_name);
        REQUIRE(
            bgfx::isValid(fragment),
            "Invalid default fragment shader '%s'.",
            fs_name
        );

        const bgfx::ProgramHandle program = bgfx::createProgram(vertex, fragment, true);
        ASSERT(
            bgfx::isValid(program),
            "Invalid default program with shaders '%s' and '%s'.",
            vs_name, fs_name
        );

        (*this)[desc.attribs] = program;
    }
}

void DefaultProgramCache::cleanup()
{
    for (bgfx::ProgramHandle program : programs)
    {
        if (bgfx::isValid(program))
        {
            bgfx::destroy(program);
        }
    }

    *this = {};
}


// -----------------------------------------------------------------------------
// DEFAULT UNIFORMS
// -----------------------------------------------------------------------------

struct DefaultUniformDesc
{
    const char*             name;
    bgfx::UniformType::Enum type;
    DefaultUniform          index;
};

static const DefaultUniformDesc s_default_uniform_descs[] =
{
    { "s_tex_color_rgba", bgfx::UniformType::Sampler, DefaultUniform::COLOR_TEXTURE_RGBA },
};

bgfx::UniformHandle DefaultUniformCache::operator[](DefaultUniform uniform) const
{
    return uniforms[uint32_t(uniform)];
}

void DefaultUniformCache::init()
{
    for (const DefaultUniformDesc& desc : s_default_uniform_descs)
    {
        uniforms[uint32_t(desc.index)] = bgfx::createUniform(desc.name, desc.type);

        ASSERT(bgfx::isValid(uniforms[uint32_t(desc.index)]),
            "Failed to create default uniform '%s'.",
            desc.name
        );
    }
}

void DefaultUniformCache::cleanup()
{
    for (bgfx::UniformHandle uniform : uniforms)
    {
        bgfx::destroy(uniform);
    }

    *this = {};
}


// -----------------------------------------------------------------------------
// MATRIX STACK
// -----------------------------------------------------------------------------

void MatrixStack::init()
{
    top  = HMM_Mat4d(1.0f);
    size = 0;
}

void MatrixStack::push()
{
    matrices[size++] = top;
}

void MatrixStack::pop()
{
    top = matrices[--size];
}

void MatrixStack::multiply_top(const hmm_mat4& matrix)
{
    top = matrix * top;
}


// -----------------------------------------------------------------------------
// PASSES
// -----------------------------------------------------------------------------

void Pass::init()
{
    view_matrix     = HMM_Mat4d(1.0f);
    proj_matrix     = HMM_Mat4d(1.0f);

    viewport_x      = 0;
    viewport_y      = 0;
    viewport_width  = SIZE_EQUAL;
    viewport_height = SIZE_EQUAL;

    framebuffer     = BGFX_INVALID_HANDLE;

    clear_flags     = BGFX_CLEAR_NONE;
    clear_depth     = 1.0f;
    clear_rgba      = 0x000000ff;
    clear_stencil   = 0;

    dirty_flags     = DIRTY_CLEAR;
}

void Pass::update(bgfx::ViewId id, bool backbuffer_size_changed)
{
    if (dirty_flags & DIRTY_TOUCH)
    {
        bgfx::touch(id);
    }

    if (dirty_flags & DIRTY_CLEAR)
    {
        bgfx::setViewClear(id, clear_flags, clear_rgba, clear_depth, clear_stencil);
    }

    if (dirty_flags & DIRTY_TRANSFORM)
    {
        bgfx::setViewTransform(id, &view_matrix, &proj_matrix);
    }

    if ((dirty_flags & DIRTY_RECT) || (backbuffer_size_changed && viewport_width >= SIZE_EQUAL))
    {
        if (viewport_width >= SIZE_EQUAL)
        {
            bgfx::setViewRect(id, viewport_x, viewport_y, bgfx::BackbufferRatio::Enum(viewport_width - SIZE_EQUAL));
        }
        else
        {
            bgfx::setViewRect(id, viewport_x, viewport_y, viewport_width, viewport_height);
        }
    }

    if ((dirty_flags & DIRTY_FRAMEBUFFER) || backbuffer_size_changed)
    {
        // Having `BGFX_INVALID_HANDLE` here is OK.
        bgfx::setViewFrameBuffer(id, framebuffer);
    }

    dirty_flags = DIRTY_NONE;
}

void Pass::touch()
{
    dirty_flags |= DIRTY_TOUCH;
}

void Pass::set_view(const hmm_mat4& matrix)
{
    view_matrix  = matrix;
    dirty_flags |= DIRTY_TRANSFORM;
}

void Pass::set_projection(const hmm_mat4& matrix)
{
    proj_matrix  = matrix;
    dirty_flags |= DIRTY_TRANSFORM;
}

void Pass::set_framebuffer(bgfx::FrameBufferHandle framebuffer_)
{
    framebuffer  = framebuffer_;
    dirty_flags |= DIRTY_FRAMEBUFFER;
}

void Pass::set_no_clear()
{
    clear_flags  = BGFX_CLEAR_NONE;
    dirty_flags |= DIRTY_CLEAR;
}

void Pass::set_clear_depth(float depth)
{
    clear_flags |= BGFX_CLEAR_DEPTH;
    clear_depth  = depth;
    dirty_flags |= DIRTY_CLEAR;
}

void Pass::set_clear_color(uint32_t rgba)
{
    clear_flags |= BGFX_CLEAR_COLOR;
    clear_rgba   = rgba;
    dirty_flags |= DIRTY_CLEAR;
}

void Pass::set_viewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    ASSERT(
        width < SIZE_EQUAL || width == height,
        "Symbolic viewport size must be the same in bot X and Y axes."
    );

    viewport_x      = x;
    viewport_y      = y;
    viewport_width  = width;
    viewport_height = height;
    dirty_flags    |= DIRTY_RECT;
}

void PassCache::init()
{
    for (Pass& pass : passes)
    {
        pass.init();
    }

    backbuffer_size_changed = true;
}

void PassCache::update()
{
    for (bgfx::ViewId id = 0; id < passes.size(); id++)
    {
        passes[id].update(id, backbuffer_size_changed);
    }

    backbuffer_size_changed = false;
}


// -----------------------------------------------------------------------------
// TIME MEASUREMENT
// -----------------------------------------------------------------------------

static const double s_timer_inv_frequency = 1.0 / double(bx::getHPFrequency());

void Timer::tic()
{
    tic(bx::getHPCounter());
}

void Timer::tic(int64_t now)
{
    counter = now;
}

double Timer::toc(bool restart)
{
    return toc(bx::getHPCounter(), restart);
}

double Timer::toc(int64_t now, bool restart)
{
    elapsed = (now - counter) * s_timer_inv_frequency;

    if (restart)
    {
        counter = now;
    }

    return elapsed;
}


// -----------------------------------------------------------------------------
// THREAD-LOCAL CONTEXT
// -----------------------------------------------------------------------------

void ThreadLocalContext::init(uint32_t frame_memory)
{
    std::vector<uint8_t>(2u * frame_memory).swap(double_frame_memory);

    frame_allocator.init({ double_frame_memory.data(), frame_memory });
    matrix_stack   .init();
}

void ThreadLocalContext::cleanup()
{
    std::vector<uint8_t>().swap(double_frame_memory);
}

void ThreadLocalContext::swap_frame_allocator_memory()
{
    size_t offset = 0;

    if (frame_allocator.buffer.data() == double_frame_memory.data())
    {
        offset = frame_allocator.buffer.size();
    }

    frame_allocator.init({ double_frame_memory.data() + offset, frame_allocator.buffer.size() });
}


// -----------------------------------------------------------------------------
// GLOBAL CONTEXT
// -----------------------------------------------------------------------------

void GlobalContext::init()
{
    meshes          .init();
    passes          .init();
    vertex_layouts  .init();

    default_uniforms.init();
    default_programs.init();
}

void GlobalContext::cleanup()
{
    default_uniforms.cleanup();
    default_programs.cleanup();
    meshes          .cleanup();
}


// -----------------------------------------------------------------------------

} // namespace mnm
