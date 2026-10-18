#include "shader_translator.h"
#include "shader_compiler.h"
#include <rex/graphics/format/ucode.h>
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace carbon::gpu {
uint64_t HashBytes(const void* p, size_t n, uint64_t seed) {
  return XXH3_64bits_withSeed(p, n, seed);
}
}

namespace u = rex::graphics::ucode;
using Words = std::array<uint32_t, 3>;

Words Alu(int predicate, u::AluVectorOpcode vector = u::AluVectorOpcode::kAdd,
          u::AluScalarOpcode scalar = u::AluScalarOpcode::kRetainPrev, bool masked_export = false) {
  Words words{(masked_export ? 1u << 15 : 15u << 16) | (uint32_t(scalar) << 26),
              predicate < 0 ? 0u : (1u << 28) | (uint32_t(predicate) << 27),
              uint32_t(vector) << 24};
  const auto& op = reinterpret_cast<const u::AluInstruction&>(words);
  assert(op.is_predicated() == (predicate >= 0));
  assert(op.vector_opcode() == vector && op.scalar_opcode() == scalar);
  assert(op.GetVectorOpResultWriteMask() == (masked_export ? 0u : 15u));
  return words;
}

Words Fetch(int predicate) {
  Words words{uint32_t(u::FetchOpcode::kTextureFetch),
              0x688u | (predicate < 0 ? 0u : 1u << 31), predicate > 0 ? 1u << 31 : 0u};
  const auto& op = reinterpret_cast<const u::FetchInstruction&>(words);
  assert(op.is_predicated() == (predicate >= 0));
  return words;
}

std::vector<uint32_t> Program(const std::vector<Words>& ops, uint32_t sequence, bool split, bool complex) {
  // Up to six instructions per EXEC. A second EXEC/jump exercises scope boundaries.
  const uint32_t start = complex ? 2 : 1;
  const uint32_t first_count = split ? uint32_t(ops.size() / 2) : uint32_t(ops.size());
  std::vector<std::array<uint32_t, 2>> cf;
  if (complex) cf.push_back({1, uint32_t(u::ControlFlowOpcode::kCondJmp) << 12});
  cf.push_back({start | (first_count << 12) | ((sequence & ((1u << (2 * first_count)) - 1)) << 16),
                uint32_t(split ? u::ControlFlowOpcode::kExec : u::ControlFlowOpcode::kExecEnd) << 12});
  if (split) {
    cf.push_back({start + first_count | (uint32_t(ops.size() - first_count) << 12) |
                     ((sequence >> (2 * first_count)) << 16),
                  uint32_t(u::ControlFlowOpcode::kExecEnd) << 12});
  }
  while (cf.size() < start * 2) cf.push_back({0, 0});
  std::vector<uint32_t> words;
  for (size_t i = 0; i < cf.size(); i += 2) {
    words.push_back(cf[i][0]);
    words.push_back(cf[i][1] | (cf[i + 1][0] << 16));
    words.push_back((cf[i + 1][0] >> 16) | (cf[i + 1][1] << 16));
  }
  for (auto op : ops) words.insert(words.end(), op.begin(), op.end());
  return words;
}

void Check(const std::vector<Words>& ops, uint32_t sequence = 0, bool split = false, bool complex = false) {
  static unsigned case_id = 0;
  const auto words = Program(ops, sequence, split, complex);
  static carbon::gpu::ShaderCompiler compiler;
  for (auto type : {carbon::gpu::xenos::ShaderType::kVertex, carbon::gpu::xenos::ShaderType::kPixel}) {
    const auto translated = carbon::gpu::TranslateShader(type, words.data(), uint32_t(words.size()));
    assert(translated.valid && translated.instruction_count == ops.size());
    const bool vertex = type == carbon::gpu::xenos::ShaderType::kVertex;
    const auto glsl = vertex ? carbon::gpu::BuildVertexShaderGlsl(translated, {4, false})
                             : carbon::gpu::BuildPixelShaderGlsl(translated, {4, 0, 0, false});
    std::vector<uint32_t> spirv;
    std::string error;
    if (!compiler.Compile(glsl, vertex, spirv, error)) {
      std::cerr << "case " << case_id << ": " << error << '\n' << glsl;
      std::abort();
    }
    // carbon_gpu_constant_blocks: the same shader reading 64-constant blocks must compile too (not
    // printed, so baseline/candidate traces stay comparable).
    if (!translated.float_constants_relative) {
      auto blocked = translated;
      blocked.constant_blocks = true;
      const auto block_glsl = vertex ? carbon::gpu::BuildVertexShaderGlsl(blocked, {4, false})
                                     : carbon::gpu::BuildPixelShaderGlsl(blocked, {4, 0, 0, false});
      std::vector<uint32_t> block_spirv;
      if (!compiler.Compile(block_glsl, vertex, block_spirv, error)) {
        std::cerr << "case " << case_id << " (constant blocks): " << error << '\n' << block_glsl;
        std::abort();
      }
      static unsigned compiled = 0;
      if (++compiled == 1) std::cerr << "constant-block GLSL compiles\n";
    }
    unsigned blocks = 0, branches = 0;
    for (size_t i = 5; i < spirv.size();) {
      const uint32_t length = spirv[i] >> 16, opcode = spirv[i] & 0xFFFF;
      assert(length && i + length <= spirv.size());
      blocks += opcode == 248;  // OpLabel
      branches += opcode == 250;  // OpBranchConditional
      i += length;
    }
    std::cout << "CASE " << case_id++ << " SPIRV " << blocks << ' ' << branches << '\n'
              << translated.body << "ENDCASE\n";
  }
}

int main() {
  Check({Alu(1), Alu(1), Alu(1), Alu(1), Alu(1), Alu(1)});
  Check({Alu(0), Alu(0), Fetch(0), Fetch(0), Alu(0), Alu(0)}, (1u << 4) | (1u << 6));
  Check({Alu(1), Alu(0), Alu(-1), Alu(1), Alu(1), Alu(0)});
  Check({Alu(1), Alu(1), Alu(1), Alu(1), Alu(1), Alu(1)}, 0, true);
  Check({Alu(1), Alu(1), Alu(1), Alu(1), Alu(1), Alu(1)}, 0, true, true);
  // All scalar/vector opcode combinations, both polarities, zero-mask exports.
  for (uint32_t v = 0; v < 32; ++v) {
    for (uint32_t s = 0; s < 64; ++s) {
      const int p = int((v + s) & 1);
      Check({Alu(p), Alu(p, u::AluVectorOpcode(v), u::AluScalarOpcode(s), (s & 1) != 0),
             Alu(p), Alu(1 - p)});
    }
  }
}
