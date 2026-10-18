import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "toolchains/mesa-switch/src/nouveau/headers/util.py"


@unittest.skipUnless(SOURCE.is_file(), "Mesa source checkout unavailable")
class MesaTemplateTests(unittest.TestCase):
    def test_rust_formatter_uses_utf8_for_non_ascii_comments(self):
        spec = importlib.util.spec_from_file_location("mesa_header_util", SOURCE)
        util = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(util)

        class Template:
            def render(self, **_):
                return "// R\u00e9flexions\n"

        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            output = Path(directory) / "header.rs"
            with patch.object(util.subprocess, "check_output", return_value="// R\u00e9flexions\n") as formatter:
                util.write_template_rs(output, Template(), {})
            self.assertEqual(formatter.call_args.kwargs["encoding"], "utf-8")
            self.assertEqual(output.read_text(encoding="utf-8"), "// R\u00e9flexions\n")
