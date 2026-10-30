"""FAST_LOADS: the scanner keeps spin-wait/barrier functions volatile, the transform round-trips, and the macros give the same values."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import fast_loads_apply as apply  # noqa: E402
import fast_loads_scan as scan  # noqa: E402

COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"

PCH = "\n".join([
    "#define REX_PHYS_HOST_OFFSET(addr) 0u",
    *apply.ORIGINAL,
    "#define REX_LOAD_STRING(x, len) 0",
    "",
])


def function(name, lines):
    # generated code keeps labels at column 0 and indents statements with a tab
    return "DEFINE_REX_FUNC(%s) {\n%s\n}\n" % (name, "\n".join(l if l.startswith("loc_") else "\t" + l for l in lines))


CPP = "".join([
    function("sub_82000010", [  # spin: fixed address, no call, no store
        "loc_82000020:", "ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);", "cr6.compare<int32_t>(ctx.r11.s32, 0, xer);",
        "if (cr6.eq) goto loc_82000020;", "return;"]),
    function("sub_82000100", [  # a call inside the loop: the compiler must re-read memory
        "loc_82000110:", "ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);", "sub_82000500(ctx, base);",
        "if (cr6.eq) goto loc_82000110;", "return;"]),
    function("sub_82000200", [  # the address moves every pass
        "loc_82000210:", "ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);", "ctx.r3.u64 = ctx.r3.u64 + 4;",
        "if (cr6.eq) goto loc_82000210;", "return;"]),
    function("sub_82000300", ["// lwsync ", "ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);", "return;"]),
    function("sub_82000400", ["ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 8);", "return;"]),
    function("sub_82000500", ["REX_ENTER_GLOBAL_LOCK();", "ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);", "return;"]),
])


class FastLoadsTests(unittest.TestCase):
    def folder(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        (root / "nfscarbon_pch.h").write_text(PCH, newline="\n")
        (root / "nfscarbon_recomp.0.cpp").write_text(CPP, newline="\n")
        return root

    def test_scanner_flags_only_synchronisation_shaped_functions(self):
        flagged = {n for n, body in scan.functions(CPP) if scan.reasons(body)}
        self.assertEqual(flagged, {"sub_82000010", "sub_82000300", "sub_82000500"})

    def test_apply_rewrites_only_flagged_functions_and_revert_restores_bytes(self):
        root = self.folder()
        before = {p.name: p.read_bytes() for p in root.iterdir()}
        apply.apply(root)
        text = (root / "nfscarbon_recomp.0.cpp").read_text()
        for name, volatile in (("sub_82000010", True), ("sub_82000100", False), ("sub_82000200", False),
                               ("sub_82000300", True), ("sub_82000400", False), ("sub_82000500", True)):
            body = dict(scan.functions(text))[name]
            self.assertEqual(any("REX_LOADV_" in l for l in body), volatile, name)
            self.assertEqual(any("REX_LOAD_U" in l for l in body), not volatile, name)
        pch = (root / "nfscarbon_pch.h").read_text()
        self.assertIn("#if defined(NFSCARBON_FAST_LOADS)", pch)
        self.assertIn("#define REX_LOADV_U32", pch)
        apply.apply(root)  # applying twice must not stack changes
        self.assertEqual(text, (root / "nfscarbon_recomp.0.cpp").read_text())
        apply.revert(root)
        after = {p.name: p.read_bytes() for p in root.iterdir()}
        self.assertEqual(before, after)

    def test_revert_leaves_a_regenerated_file_alone(self):
        root = self.folder()
        apply.apply(root)
        (root / "nfscarbon_recomp.0.cpp").write_text("// regenerated\n")
        apply.revert(root)
        self.assertEqual((root / "nfscarbon_recomp.0.cpp").read_text(), "// regenerated\n")

    @unittest.skipUnless(COMPILER.is_file(), "local compiler unavailable")
    def test_plain_and_volatile_loads_agree_including_type_punning_and_odd_addresses(self):
        root = self.folder()
        apply.apply(root)
        pch = (root / "nfscarbon_pch.h").read_text()
        harness = (
            "#include <cstdint>\n#include <cstdio>\n#include <cstring>\n"
            "typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;\n"
            + pch +
            "int main() { static u8 mem[64]; u8* base = mem;\n"
            "  for (int i = 0; i < 64; ++i) mem[i] = u8(i * 7 + 1);\n"
            "  u64 acc = 0; u64 w = 0x1122334455667788ull; memcpy(mem + 3, &w, 8);\n"
            "  acc += REX_LOAD_U8(5) + REX_LOAD_U16(5) + REX_LOAD_U32(5) + REX_LOAD_U64(3);\n"
            "  acc += REX_LOADV_U8(5) + REX_LOADV_U16(5) + REX_LOADV_U32(5) + REX_LOADV_U64(3);\n"
            "  std::printf(\"%llx\\n\", (unsigned long long)acc); }\n")
        results = []
        for define in ([], ["-DNFSCARBON_FAST_LOADS"]):
            src, exe = root / "h.cpp", root / "h.exe"
            src.write_text(harness)
            subprocess.run([str(COMPILER), "-O2", "-std=c++17", "-Wall", "-Werror", *define, str(src), "-o", str(exe)], check=True)
            results.append(subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.strip())
        self.assertEqual(results[0], results[1])


if __name__ == "__main__":
    unittest.main()