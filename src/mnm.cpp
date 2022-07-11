#include "mnm_internal.h"

#include <inttypes.h>     // PRI*
#include <stddef.h>       // max_align_t, size_t
#include <string.h>       // memcpy

#include <bx/allocator.h> // alignPtr
#include <bx/bx.h>        // BX_ASSERT, BX_WARN, isPowerOf2

#include <mnm.h>

namespace mnm
{

// -----------------------------------------------------------------------------
// ASSERTIONS
// -----------------------------------------------------------------------------

#define ASSERT BX_ASSERT
#define TRACE  BX_TRACE
#define WARN   BX_WARN


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

// ...


// -----------------------------------------------------------------------------
// THREAD-LOCAL CONTEXT
// -----------------------------------------------------------------------------

void ThreadLocalContext::init(uint32_t frame_memory)
{
    std::vector<uint8_t>(2u * frame_memory).swap(double_frame_memory);

    frame_allocator.init({ double_frame_memory.data(), frame_memory });
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
    vertex_layouts.init();
}

void GlobalContext::cleanup()
{
}


// -----------------------------------------------------------------------------

} // namespace mnm
