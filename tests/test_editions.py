from pathlib import Path
import re
import unittest

from scripts.codegen import edition_paths, collectors_glue


class EditionTests(unittest.TestCase):
    def test_collectors_glue_uses_root_relative_paths(self):
        generated = Path.cwd() / "carbon-source/generated/collectors"
        raw = '${CMAKE_CURRENT_SOURCE_DIR}/' + generated.as_posix() + '/sources.cmake'
        self.assertEqual(collectors_glue(raw, generated),
                         '${CMAKE_CURRENT_SOURCE_DIR}/generated/collectors/sources.cmake')

    def test_collectors_outputs_cannot_replace_standard_outputs(self):
        root = Path.cwd()
        standard = edition_paths(root, "standard")
        collectors = edition_paths(root, "collectors")
        for key in ("game", "generated", "manifest", "glue"):
            self.assertNotEqual(standard[key], collectors[key], key)
        self.assertEqual(standard["generated"], root / "carbon-source/generated/default")
        self.assertEqual(collectors["game"], root / "game-data-collectors")

    def test_collectors_host_codegen_uses_collectors_manifest(self):
        raw = 'codegen ${CMAKE_CURRENT_SOURCE_DIR}/nfscarbon_manifest.toml'
        self.assertEqual(collectors_glue(raw, Path.cwd() / "carbon-source/generated/collectors"),
                         'codegen ${CMAKE_CURRENT_SOURCE_DIR}/generated/collectors/project/nfscarbon_manifest.toml')

    def test_unknown_edition_is_rejected(self):
        with self.assertRaises(ValueError):
            edition_paths(Path.cwd(), "typo")

    def test_collectors_manifest_hints_are_registered(self):
        generated = Path("carbon-source/generated/collectors/nfscarbon_register.cpp")
        if not generated.exists():
            self.skipTest("Local Collector's generated code unavailable")
        text = generated.read_text(encoding="utf-8")
        template = Path("config/collectors_manifest.toml.in").read_text(encoding="utf-8")
        for address in re.findall(r"^0x([0-9A-F]{8})\s*=", template, re.M):
            with self.subTest(address=address):
                self.assertTrue(f"SetFunction(0x{address}, sub_{address})" in text)

    def test_collectors_observed_callbacks_are_registered(self):
        # CE hardware and strict Windows runs reached these callbacks, then trapped.
        generated = Path("carbon-source/generated/collectors/nfscarbon_register.cpp")
        if not generated.exists():
            self.skipTest("Local Collector's generated code unavailable")
        text = generated.read_text(encoding="utf-8")
        for address in ("824DA838", "82321E88", "82350220", "821641C8"):
            with self.subTest(address=address):
                self.assertTrue(f"SetFunction(0x{address}, sub_{address})" in text,
                                f"CE callback 0x{address} is missing from the dispatch registry")

    def test_configured_collectors_build_uses_no_standard_guest_hooks(self):
        ninja = Path("build/switch-collectors/build.ninja")
        if not ninja.is_file():
            self.skipTest("Local Collector's CMake build unavailable")
        text = ninja.read_text(encoding="utf-8")
        sources = set(re.findall(r"build CMakeFiles/nfscarbon\.dir/src/(\w+\.cpp)\.obj:", text))
        self.assertTrue({"main.cpp", "dlc_install.cpp", "switch_limits_log.cpp"} <= sources)
        # Only the verified, address-parameterized scheduling and audio hooks are shared with Collector's.
        self.assertEqual({name for name in sources if name.endswith("_hook.cpp")},
                         {"ring_wait_hook.cpp", "frame_handoff_hook.cpp", "audio_resampler_hook.cpp",
                          "audio_server_wait_hook.cpp", "render_pass_profile_hook.cpp", "car_lod_hook.cpp",
                          "audio_rate_hook.cpp"}, sources)
        self.assertNotIn("dispatch_tolerance.cpp", sources)
        self.assertNotIn("carbon-source/generated/default/", text)
        self.assertNotIn("sdk/out/switch-arm64/", text)

    def test_collectors_scheduling_hook_sites_match_generated_code(self):
        # Every address in the NFSCARBON_COLLECTORS branches of the shared hooks was derived by
        # instruction-for-instruction matching against standard; re-check them against CE code.
        generated = Path("carbon-source/generated/collectors")
        if not generated.is_dir():
            self.skipTest("Local Collector's generated code unavailable")
        text = "".join(p.read_text(encoding="utf-8") for p in generated.glob("nfscarbon_recomp.*.cpp"))
        for ret in ("826DBC4C", "826DBA30"):  # ring-space loops calling the watchdog
            self.assertIn(f"ctx.lr = 0x{ret};\n\tsub_826E0088(ctx, base);", text, ret)
        for ret in ("824E964C", "824E98B4"):  # Sleep(0) in the frame handoff loops
            site = re.search(r"ctx\.r3\.s64 = 0;\n\t// bl 0x82943378\n\tctx\.lr = 0x%s;\n\tsub_82943378\(ctx, base\);\n"
                             r"\t// lwz r11,20932\(r(27|31)\)" % ret, text)
            self.assertIsNotNone(site, ret)
        self.assertIn("r29.u32 + 10813", text[text.index("DEFINE_REX_FUNC(sub_826E0088)"):][:4000])
        hooks = Path("carbon-source/src")
        ring = (hooks / "ring_wait_hook.cpp").read_text(encoding="utf-8")
        handoff = (hooks / "frame_handoff_hook.cpp").read_text(encoding="utf-8")
        for token in ("sub_826E0088", "0x826DBC4C", "0x826DBA30"):
            self.assertIn(token, ring)
        for token in ("sub_82943378", "0x824E964C", "0x824E98B4"):
            self.assertIn(token, handoff)

    def test_collectors_audio_hook_addresses_match_generated_code(self):
        generated = Path("carbon-source/generated/collectors")
        if not generated.is_dir():
            self.skipTest("Local Collector's generated code unavailable")
        text = "".join(p.read_text(encoding="utf-8") for p in generated.glob("nfscarbon_recomp.*.cpp"))
        header = Path("carbon-source/src/edition.h").read_text(encoding="utf-8")
        collectors = header[header.index("#if defined(NFSCARBON_COLLECTORS)"):header.index('#else\n#define CARBON_GUEST_PCH "../generated/default')]
        for name in re.findall(r"#define AUDIO_FN_\w+ (sub_[0-9A-F]{8})", collectors):
            self.assertIn(f"DEFINE_REX_FUNC({name})", text, name)
        # Link-register values the filter hooks set must be the return addresses of the helper/callback calls.
        for lr, callee in (("826C89F0", "__savegprlr_28"), ("826C8A2C", None), ("826CC860", "__savegprlr_28")):
            self.assertIn(f"0x{lr}", collectors)
            self.assertIn(f"ctx.lr = 0x{lr};", text)
        self.assertEqual(len(re.findall(r"#define AUDIO_FN_", collectors)), 11)
        # Render passes: same call order inside the frame function, all at standard - 0x130.
        passes = re.findall(r"#define PASS_\w+ (sub_[0-9A-F]{8})", collectors)
        self.assertEqual(len(passes), 7)
        frame = text[text.index(f"DEFINE_REX_FUNC({passes[0]})"):][:12000]
        order = [frame.index(p + "(ctx, base);") for p in passes[1:]]
        self.assertEqual(order, sorted(order))
        # Data the hooks read directly is addressed identically by the CE originals (lis -32244 + 32280 etc.).
        for name in ("sub_826CB010", "sub_826D1408"):
            body = text[text.index(f"DEFINE_REX_FUNC({name})"):][:6000]
            self.assertIn("+ 32280)", body)

    def test_collectors_leaf_list_excludes_functions_that_call_millicode(self):
        names = dict(line.split("=") for line in Path("scripts/native_leaf_list_collectors.txt").read_text().split())
        self.assertNotIn("LeafB56A8", names)  # sub_826B54E0 saves r14-r31 through __savegprlr_14
        self.assertEqual(set(names), {"Leaf1678", "LeafCD8D0", "LeafCD528"})
        script = Path("scripts/localize_leaf.py").read_text(encoding="utf-8")
        self.assertIn("__(?:save|rest)(?:gpr|fpr|vmx)", script)


if __name__ == "__main__":
    unittest.main()
