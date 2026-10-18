// Differential test of the 16-byte-run untiling in texture_decode.cpp (carbon_gpu_texture_fast_untile):
// every decoded byte must match the per-block path, for tiled 2D textures of several formats, sizes,
// endian swaps, layers and mip chains.
#include "texture_decode.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::gpu;

namespace {
constexpr uint32_t kMemory = 0x20000000;  // the decoder masks addresses to 512 MB
constexpr uint32_t kCommitted = 96u << 20;

std::vector<uint8_t> Decode(const uint8_t* mem, const GuestTexture& t, bool runs, double* ms = nullptr) {
  const TextureFormatInfo info = GetTextureFormatInfo(t.format, true);
  std::vector<HostTextureRegion> regions;
  std::vector<uint8_t> out(GetHostTextureSize(t, info, regions), 0xCD);
  g_texture_decode_runs = runs;
  const auto start = std::chrono::steady_clock::now();
  DecodeGuestTexture(mem, t, info, regions, out.data());
  if (ms) *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return out;
}
}  // namespace

int main() {
  auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, kMemory, MEM_RESERVE, PAGE_NOACCESS));
  assert(mem && VirtualAlloc(mem, kCommitted, MEM_COMMIT, PAGE_READWRITE));
  std::mt19937_64 rng(0x7E571E);
  for (uint32_t i = 0; i < kCommitted; i += 8) {
    const uint64_t v = rng();
    std::memcpy(mem + i, &v, 8);
  }
  // Run identity: inside a run-aligned group of min(8, 16 / bpb) blocks the tiled offsets are consecutive.
  for (uint32_t bpb_log2 = 0; bpb_log2 <= 4; ++bpb_log2)
    for (uint32_t pitch : {32u, 64u, 96u, 160u, 1280u})
      for (int32_t y = 0; y < 64; ++y)
        for (int32_t x = 0, run = std::min(8, 16 >> bpb_log2); x < int32_t(pitch); x += run)
          for (int32_t k = 1; k < run; ++k)
            assert(TiledOffset2D(x + k, y, pitch, bpb_log2) == TiledOffset2D(x, y, pitch, bpb_log2) + (k << bpb_log2));
  const xenos::TextureFormat formats[] = {xenos::TextureFormat::k_8, xenos::TextureFormat::k_8_8,
                                          xenos::TextureFormat::k_8_8_8_8, xenos::TextureFormat::k_16_16_16_16,
                                          xenos::TextureFormat::k_DXT1, xenos::TextureFormat::k_DXT4_5};
  const xenos::Endian endians[] = {xenos::Endian::kNone, xenos::Endian::k8in16, xenos::Endian::k8in32,
                                   xenos::Endian::k16in32};
  unsigned cases = 0, fast = 0;
  for (auto format : formats)
    for (auto endian : endians)
      for (int round = 0; round < 24; ++round) {
        GuestTexture t;
        t.format = format;
        t.endian = endian;
        t.tiled = round < 12;  // rounds 12-23: linear layout (video planes)
        t.width = 1 + uint32_t(rng() % 1300);
        t.height = 1 + uint32_t(rng() % 760);
        if (round == 0) { t.width = 1280; t.height = 720; }
        if (round == 1) { t.width = 640; t.height = 360; }
        t.depth = round % 4 == 3 ? 2 : 1;  // stacked layers
        t.pitch_texels = t.tiled ? (t.width + 31) & ~31u : t.width + uint32_t(rng() % 3) * 16;
        t.base_address = 0x100000 + 4096 * uint32_t(rng() % 256);
        if (round % 3 == 2) {
          t.mip_address = t.base_address + (16u << 20);
          t.mip_max_level = 1 + uint32_t(rng() % 4);
          t.packed_mips = rng() & 1;
        }
        const GuestTextureExtent extent = GetGuestTextureExtent(t);
        if (extent.base_start + extent.base_size > kCommitted || extent.mip_start + extent.mip_size > kCommitted)
          continue;
        const auto slow = Decode(mem, t, false), quick = Decode(mem, t, true);
        assert(slow.size() == quick.size() && std::memcmp(slow.data(), quick.data(), slow.size()) == 0);
        ++cases;
        fast += GetTextureFormatInfo(format, true).conversion == TextureConversion::kNone;
      }
  GuestTexture video;  // a 1280x720 8-bit video plane, as the cutscenes upload every frame
  video.format = xenos::TextureFormat::k_8;
  video.tiled = false;  // measured as linear: the video planes
  video.width = 1280;
  video.height = 720;
  video.pitch_texels = 1280;
  video.base_address = 0x100000;
  double slow_ms = 0, fast_ms = 0;
  Decode(mem, video, false, &slow_ms);
  Decode(mem, video, true, &fast_ms);
  std::printf("PASS texture untile: run identity, %u tiled+linear decodes identical (%u copy-formats); "
              "1280x720 k_8 linear host %.2f -> %.2f ms (not Switch timing)\n", cases, fast, slow_ms, fast_ms);
  VirtualFree(mem, 0, MEM_RELEASE);
}
