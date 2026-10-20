"""Run the production upload methods with GPU allocation/arena boundaries faked."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "carbon-source/renderer/src/renderer.cpp"
COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(SOURCE.is_file() and COMPILER.is_file(), "local renderer/compiler unavailable")
class RendererUploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/tests/renderer-upload"
        build.mkdir(parents=True, exist_ok=True)
        source = SOURCE.read_text(encoding="utf-8")
        # Extract complete production methods; no rewritten allocation algorithm.
        signatures = ["bool Renderer::AcquireChunk()", "Renderer::UploadAllocation Renderer::Upload(",
                      "bool Renderer::SetupVertexData("]
        reserve = "bool Renderer::ReserveUpload(" if "bool Renderer::ReserveUpload(" in source else "void Renderer::ReserveUpload("
        signatures.insert(1, reserve)
        declarations = []
        definitions = []
        for signature in signatures:
            body = method(source, signature)
            declarations.append(body[:body.index("{")].replace("Renderer::", "") + ";")
            definitions.append(body)
        has_tail = int("VkDeviceSize tail_bytes" in definitions[-1])
        (build / "declarations.inc").write_text(f"#define HAS_UPLOAD_TAIL {has_tail}\n" + "\n".join(declarations), encoding="utf-8")
        (build / "methods.inc").write_text("\n".join(definitions), encoding="utf-8")
        cls.exe = build / "renderer-upload.exe"
        subprocess.run([str(COMPILER), "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wno-missing-field-initializers", "-I" + str(build),
                        "-I" + str(ROOT / "reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include"),
                        "-I" + str(ROOT / "reference-nfsmw-nx/sdk/thirdparty/vulkan-memory-allocator/include"),
                        str(ROOT / "tests/renderer_upload_test.cpp"), "-o", str(cls.exe)], check=True)

    def test_upload_failures_preserve_state(self):
        result = subprocess.run([str(self.exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
