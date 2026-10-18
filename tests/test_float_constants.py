from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "carbon-source/renderer/src"


@unittest.skipUnless((ROOT / "command_processor.cpp").is_file(), "Carbon checkout unavailable")
class FloatConstantTests(unittest.TestCase):
    def test_only_float_registers_use_bit_identity_and_notification(self):
        text = (ROOT / "command_processor.cpp").read_text(encoding="utf-8")
        write = text[text.index("void CommandProcessor::WriteRegister("):text.index("const_cast<volatile uint32_t&>(regs_.values[index]) = value;")]
        self.assertIn("if (index >= kRegisterCount)", write)
        self.assertIn("if (index >= kRegFloatConstants)", write)
        self.assertIn("if (index < kRegFetchConstants)", write)
        self.assertIn("regs_.WriteFloatConstant(index, value)", write)
        self.assertIn("} else {\n      regs_.values[index] = value;", write)
        common = (ROOT / "gpu_common.h").read_text(encoding="utf-8")
        self.assertIn("assert(index >= kRegFloatConstants && index < kRegFetchConstants)", common)
        self.assertIn("if (previous == value) return false;", common)

    def test_dirty_state_is_sticky_and_reuse_guards_are_retained(self):
        header = (ROOT / "renderer.h").read_text(encoding="utf-8")
        notify = header[header.index("void OnFloatConstantWritten("):header.index("void Draw(")]
        self.assertIn("if (changed)", notify)
        self.assertIn("float_constants_dirty_[index >> 10] = true;", notify)
        self.assertNotIn("= false", notify)
        self.assertIn("bool float_constants_dirty_[2] = {true, true};", header)
        text = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        self.assertIn("last_constant_chunk_[0] = last_constant_chunk_[1] = UINT32_MAX;", text)
        self.assertIn("!float_constants_dirty_[stage] && last_constant_chunk_[stage] == current_chunk_ &&", text)
        self.assertIn("last_constant_count_[stage] >= n", text)
        self.assertIn("UploadAllocation a = Upload(4096, ubo_alignment_);", text)
        self.assertIn("float constants: {} changed words, {} identical words,", text)
