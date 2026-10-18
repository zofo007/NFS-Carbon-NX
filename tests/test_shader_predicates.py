import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("shader_compare", ROOT / "scripts/compare_shader_predicates.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def case(body, blocks=10, branches=5):
    return f"CASE 0 SPIRV {blocks} {branches}\n{body}\nENDCASE\n"


class ShaderPredicateComparisonTests(unittest.TestCase):
    def test_same_generation_groups_preserve_operations(self):
        old = case("if (p0 == true) {\n{\nr[0] = x;\n}\n}\n"
                   "if (p0 == true) {\n{\nr[1] = y;\n}\n}")
        new = case("if (p0 == true) {\n{\nr[0] = x;\n}\n{\nr[1] = y;\n}\n}", 8, 4)
        self.assertEqual(module.compare(module.parse(old), module.parse(new))[0], (2, 1))

    def test_reusing_a_changed_predicate_is_rejected(self):
        old = case("if (p0 == true) {\np0 = xe_np;\n}\nif (p0 == true) {\nr[0] = x;\n}")
        new = case("if (p0 == true) {\np0 = xe_np;\nr[0] = x;\n}", 8, 4)
        with self.assertRaisesRegex(ValueError, "generation changed"):
            module.compare(module.parse(old), module.parse(new))

    def test_arithmetic_or_polarity_changes_are_rejected(self):
        old = module.parse(case("if (p0 == false) {\nr[0] = x;\n}"))
        for body in ("if (p0 == false) {\nr[0] = y;\n}", "if (p0 == true) {\nr[0] = x;\n}"):
            with self.assertRaisesRegex(ValueError, "generation changed"):
                module.compare(old, module.parse(case(body)))

    def test_invalid_or_unbalanced_output_is_rejected(self):
        for text in ("", case("}"), case("if (p0 == true) {"),
                     case("x;").replace("CASE 0", "CASE 1"),
                     case("x;") + "CASE 1 SPIRV 10 5\npartial", "garbage\n" + case("x;")):
            with self.assertRaises(ValueError):
                module.parse(text)

    def test_group_closures_and_cache_keys_survive_generation(self):
        source = (ROOT / "carbon-source/renderer/src/shader_translator.cpp").read_text(encoding="utf-8")
        body = source[source.index("void Translator::EmitExecBody"):source.index("TranslatedShader Translator::Run")]
        self.assertIn("if (next != predicate)", body)
        self.assertIn("if (AluSetsPredicate(alu))", body)
        self.assertEqual(body.count("close_predicate();"), 3)
        compiler = (ROOT / "carbon-source/renderer/src/shader_compiler.cpp").read_text(encoding="utf-8")
        self.assertIn("HashBytes(glsl.data(), glsl.size(), kCacheVersion)", compiler)
        renderer = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        self.assertIn("kWarmVersion = 2", renderer)
        self.assertIn("predicates: {} instructions -> {} groups", renderer)
