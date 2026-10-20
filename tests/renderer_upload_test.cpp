#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#define REXGPU_ERROR(...) ((void)0)
#define REXGPU_INFO(...) ((void)0)
#define REXCVAR_GET(...) true
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

struct VmaAllocation_T { uint8_t* data; };
bool fail_buffer = false, fail_descriptor = false;
int live_buffers = 0;

VkResult vmaCreateBuffer(VmaAllocator, const VkBufferCreateInfo* info,
                         const VmaAllocationCreateInfo*, VkBuffer* buffer,
                         VmaAllocation* allocation, VmaAllocationInfo* out) {
  if (fail_buffer) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  *allocation = new VmaAllocation_T{new uint8_t[info->size]};
  *buffer = reinterpret_cast<VkBuffer>(*allocation);
  out->pMappedData = (*allocation)->data;
  ++live_buffers;
  return VK_SUCCESS;
}
void vmaDestroyBuffer(VmaAllocator, VkBuffer, VmaAllocation allocation) {
  delete[] allocation->data;
  delete allocation;
  --live_buffers;
}
void vmaGetAllocationMemoryProperties(VmaAllocator, VmaAllocation, VkMemoryPropertyFlags* flags) {
  *flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}
struct Functions {
  VkResult vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet* set) const {
    if (fail_descriptor) return VK_ERROR_OUT_OF_POOL_MEMORY;
    *set = reinterpret_cast<VkDescriptorSet>(uintptr_t(1));
    return VK_SUCCESS;
  }
  void vkUpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*) const {}
};
struct Device { const Functions& functions() const { static Functions f; return f; } };

uint64_t AlignUp64(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }
struct DrawConstants { uint32_t vtx[4] = {}; uint32_t vfetch[192]; };
constexpr uint32_t kConstantBlocks = 4, kConstantBlockBytes = 1024;

namespace xenos {
enum class FetchConstantType { kVertex };
struct xe_gpu_vertex_fetch_t {
  uint32_t address = 0, size = 0, endian = 0;
  FetchConstantType type = FetchConstantType::kVertex;
};
}
struct RegisterFile {
  xenos::xe_gpu_vertex_fetch_t fetches[96];
  auto GetVertexFetch(uint32_t i) const { return fetches[i]; }
};
struct TranslatedShader {
  uint32_t vfetch_used[3] = {}, vfetch_computed[3] = {};
  uint32_t vfetch_stride[96] = {}, vfetch_extent[96] = {};
};
struct Shader { TranslatedShader translated; };
struct VertexRange { bool known = false; uint32_t min_index = 0, max_index = 0; };
enum class DrawSubStage { kVtxArena, kVtxChunk };

struct Renderer {
  struct UploadChunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    uint8_t* mapped = nullptr;
    VkDeviceSize size = 0, used = 0;
    VkDescriptorSet set0 = VK_NULL_HANDLE;
  };
  struct UploadAllocation { uint8_t* ptr = nullptr; VkBuffer buffer = VK_NULL_HANDLE; VkDeviceSize offset = 0; uint32_t chunk = 0; };
  struct Frame { std::vector<uint32_t> chunks; } frame_;
  Frame& frame() { return frame_; }
  Device device_;
  Device* vulkan_device_ = &device_;
  VkDevice vk_device_ = VK_NULL_HANDLE;
  VmaAllocator allocator_ = nullptr;
  VkDescriptorPool set0_pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set0_layout_ = VK_NULL_HANDLE;
  VkBuffer arena_buffer_ = VK_NULL_HANDLE;
  bool constant_blocks_ = false;
  std::vector<UploadChunk> upload_chunks_;
  std::vector<uint32_t> free_chunks_;
  uint32_t current_chunk_ = UINT32_MAX;
  VkDeviceSize chunk_offset_ = 0, chunk_size_ = 64;
  struct { uint64_t upload_bytes = 0, arena_draws = 0, chunk_vertex_draws = 0; } stats_;
  uint8_t* physical_base_ = nullptr;
  bool arena_accept_ = false;
  uint32_t arena_lo_ = 0, arena_hi_ = 0;
  bool ArenaFetch(uint64_t, uint32_t, uint32_t, uint32_t lo, uint32_t hi, uint32_t& offset) {
    arena_lo_ = lo;
    arena_hi_ = hi;
    offset = 123;
    return arena_accept_;
  }
  struct Scope { ~Scope() {} };
  Scope SubTime(DrawSubStage) { return {}; }
  uint32_t vertex_upload_cache_chunk_ = UINT32_MAX;
  std::unordered_map<uint64_t, uint32_t> vertex_upload_cache_;
#include "declarations.inc"
  ~Renderer() {
    for (auto& chunk : upload_chunks_) vmaDestroyBuffer(allocator_, chunk.buffer, chunk.allocation);
  }
};
#include "methods.inc"

int main() {
  // A failed rollover must not return the start of a live, full buffer.
  {
    Renderer r;
    CHECK(r.AcquireChunk());
    r.chunk_offset_ = 64;
    r.upload_chunks_[0].used = 64;
    r.upload_chunks_[0].mapped[0] = 0x7B;
    fail_buffer = true;
    auto a = r.Upload(8, 16);
    CHECK(!a.ptr && r.current_chunk_ == 0 && r.chunk_offset_ == 64);
    CHECK(r.upload_chunks_[0].used == 64 && r.upload_chunks_[0].mapped[0] == 0x7B);
    CHECK(r.stats_.upload_bytes == 0);
  }
  CHECK(live_buffers == 0);
  {
    Renderer r;
    auto a = r.Upload(8, 16);
    CHECK(!a.ptr && r.current_chunk_ == UINT32_MAX && r.upload_chunks_.empty());
    r.ReserveUpload(16);
    CHECK(r.current_chunk_ == UINT32_MAX && r.chunk_offset_ == 0);
  }
  fail_buffer = false;
  {
    Renderer r;
    CHECK(!r.Upload(65, 16).ptr);
    CHECK(r.upload_chunks_.empty());
  }
  {
    Renderer r;
    fail_descriptor = true;
    CHECK(!r.AcquireChunk());
    CHECK(live_buffers == 0 && r.upload_chunks_.empty());
    CHECK(r.current_chunk_ == UINT32_MAX && r.frame_.chunks.empty());
    fail_descriptor = false;
  }
  {
    Renderer r;
    auto a = r.Upload(17, 16);
    auto b = r.Upload(16, 16);
    CHECK(a.ptr && a.offset == 0 && b.offset == 32 && b.chunk == a.chunk);
    auto c = r.Upload(32, 16);
    CHECK(c.ptr && c.offset == 0 && c.chunk != b.chunk);
    CHECK(r.upload_chunks_[0].used == 48 && r.upload_chunks_[1].used == 32);
  }
  CHECK(live_buffers == 0);
  {
    Renderer r;
    r.chunk_size_ = 32 * 1024 * 1024;
    CHECK(r.AcquireChunk());
    r.chunk_offset_ = r.chunk_size_ - 8 * 1024 * 1024 - 4096;
    r.vertex_upload_cache_chunk_ = r.current_chunk_;
    r.arena_buffer_ = reinterpret_cast<VkBuffer>(uintptr_t(1));
    std::vector<uint8_t> guest(8 * 1024 * 1024, 0x42);
    r.physical_base_ = guest.data();
    RegisterFile regs;
    regs.fetches[0].size = uint32_t(guest.size() / 4);
    Shader shader;
    shader.translated.vfetch_used[0] = 1;
    DrawConstants constants{};
    VertexRange range;
    constexpr VkDeviceSize tail = 16384;
#if HAS_UPLOAD_TAIL
    CHECK(r.SetupVertexData(regs, &shader, constants, range, tail));
#else
    CHECK(r.SetupVertexData(regs, &shader, constants, range));
#endif
    const auto vertex_chunk = r.current_chunk_;
    auto constant_data = r.Upload(tail, 256);
    CHECK(constant_data.ptr && constant_data.chunk == vertex_chunk);
    CHECK(r.upload_chunks_[vertex_chunk].mapped[constants.vfetch[0] * 4] == 0x42);
    CHECK(!(constants.vfetch[1] & 0x80000000u));

#if HAS_UPLOAD_TAIL
    // Eight aliased fetch slots use one upload. Cached vertices remain usable
    // even when only the constants fit in the remaining part of the chunk.
    shader.translated.vfetch_used[0] = 0xFF;
    for (uint32_t i = 1; i < 8; ++i) regs.fetches[i] = regs.fetches[0];
    r.chunk_offset_ = r.chunk_size_ - tail;
    const auto previous_bytes = r.stats_.upload_bytes;
    CHECK(r.SetupVertexData(regs, &shader, constants, range, tail));
    CHECK(r.current_chunk_ == vertex_chunk && r.stats_.upload_bytes == previous_bytes);
    for (uint32_t i = 1; i < 8; ++i) CHECK(constants.vfetch[i * 2] == constants.vfetch[0]);
    CHECK(r.Upload(tail, 256).chunk == vertex_chunk);

    // When that chunk is full, invalidate reused offsets and copy aliases once.
    CHECK(r.SetupVertexData(regs, &shader, constants, range, tail));
    CHECK(r.current_chunk_ != vertex_chunk);
    CHECK(r.stats_.upload_bytes == previous_bytes + tail + guest.size());
    for (uint32_t i = 1; i < 8; ++i) CHECK(constants.vfetch[i * 2] == constants.vfetch[0]);
    CHECK(r.upload_chunks_[r.current_chunk_].mapped[constants.vfetch[0] * 4] == 0x42);
#endif
  }
#if HAS_UPLOAD_TAIL
  {
    Renderer r;
    r.chunk_size_ = 32 * 1024 * 1024;
    r.arena_accept_ = true;
    RegisterFile regs;
    regs.fetches[0].size = 128 * 1024 / 4;
    Shader shader;
    shader.translated.vfetch_used[0] = 1;
    shader.translated.vfetch_stride[0] = 16;
    DrawConstants constants{};
    constants.vtx[2] = 256;
    constants.vtx[3] = 0xFFFFFF;
    // Index zero is clamped by the GPU to vertex 256 (byte 16384).
    VertexRange range{true, 0, 0};
    CHECK(r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.arena_lo_ == 16384 && r.arena_hi_ == 16448);

    constants.vtx[2] = 0;
    constants.vtx[3] = 10;
    range = {true, 20, 30};
    CHECK(r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.arena_lo_ == 640 && r.arena_hi_ == 704);

    constants.vtx[3] = 0xFFFFFF;
    range = {true, 0x1000001, 0x1000002};
    CHECK(r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.arena_lo_ == 64 && r.arena_hi_ == 192);

    // An interval crossing the wrap point conservatively covers the full buffer.
    range = {true, 0xFFFFFE, 0x1000002};
    CHECK(r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.arena_lo_ == 0 && r.arena_hi_ == 128 * 1024);
  }
  {
    Renderer r;
    r.chunk_size_ = 32 * 1024 * 1024;
    CHECK(r.AcquireChunk());
    RegisterFile regs;
    Shader shader;
    shader.translated.vfetch_used[0] = 0x1F;
    for (uint32_t i = 0; i < 5; ++i) regs.fetches[i].size = 2 * 1024 * 1024 - i * 4;
    DrawConstants constants{};
    VertexRange range;
    // More than one chunk of distinct fallback data must be rejected before copying.
    CHECK(!r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.chunk_offset_ == 0 && r.stats_.upload_bytes == 0);
    r.arena_accept_ = true;
    // The same fetch sizes are valid when they all come from the arena.
    CHECK(r.SetupVertexData(regs, &shader, constants, range, 16384));
    CHECK(r.chunk_offset_ == 0 && r.stats_.upload_bytes == 0);
    for (uint32_t i = 0; i < 5; ++i) CHECK(constants.vfetch[i * 2 + 1] & 0x80000000u);
  }
#endif
  CHECK(live_buffers == 0);
  std::puts("PASS: allocation failures, descriptor cleanup, alignment and arena fallback with same-chunk constants");
}
