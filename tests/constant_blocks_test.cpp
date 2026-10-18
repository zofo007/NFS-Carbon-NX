// Host test of carbon-source/renderer/src/constant_blocks.h (carbon_gpu_constant_blocks).
#include "constant_blocks.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::gpu;

int main() {
  // Fixed reads move into their block; relative reads, other names and malformed text stay.
  assert(UseConstantBlocks("x = xe_vsc.c[0] + xe_vsc.c[63] * xe_vsc.c[64];", "xe_vsc", "xe_vsb") ==
         "x = xe_vsb[0].c[0] + xe_vsb[0].c[63] * xe_vsb[1].c[0];");
  assert(UseConstantBlocks("y = xe_psc.c[255].xyz; z = xe_psc.c[128];", "xe_psc", "xe_psb") ==
         "y = xe_psb[3].c[63].xyz; z = xe_psb[2].c[0];");
  assert(UseConstantBlocks("r = xe_vsc.c[(a0 + 5) & 255];", "xe_vsc", "xe_vsb") == "r = xe_vsc.c[(a0 + 5) & 255];");
  assert(UseConstantBlocks("p = xe_psc.c[7]; v = xe_vsc.c[7];", "xe_vsc", "xe_vsb") ==
         "p = xe_psc.c[7]; v = xe_vsb[0].c[7];");
  assert(UseConstantBlocks("xe_vsc.c[12", "xe_vsc", "xe_vsb") == "xe_vsc.c[12");
  assert(UseConstantBlocks("", "xe_vsc", "xe_vsb").empty());
  // Blocks a shader reads, and the block of each written dword.
  assert(ConstantBlocksUsed(0) == 0 && ConstantBlocksUsed(1) == 1 && ConstantBlocksUsed(64) == 1);
  assert(ConstantBlocksUsed(65) == 3 && ConstantBlocksUsed(192) == 7 && ConstantBlocksUsed(256) == 15);
  assert(ConstantBlockOfDword(0) == 0 && ConstantBlockOfDword(255) == 0 && ConstantBlockOfDword(256) == 1);
  assert(ConstantBlockOfDword(1023) == 3 && ConstantBlockOfDword(1024) == 0 && ConstantBlockOfDword(2047) == 3);
  std::printf("PASS constant blocks: fixed/relative/other-stage/malformed rewrites, block masks\n");
}
