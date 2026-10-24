from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "carbon-source/generated"
EDITIONS = {"default": "825A43A8", "collectors": "825A4278"}  # CarRenderInfo::Render


class CarLodTests(unittest.TestCase):
    def test_hook_addresses_and_offsets_match_each_edition(self):
        header = (ROOT / "carbon-source/src/edition.h").read_text(encoding="utf-8")
        hook = (ROOT / "carbon-source/src/car_lod_hook.cpp").read_text(encoding="utf-8")
        for name, address in (("collectors", "825A4278"), ("default", "825A43A8")):
            self.assertIn(f"#define CAR_RENDER sub_{address}", header)
        self.assertIn("kMinLod = 12492, kMaxLod = 12496", hook)
        for edition, address in EDITIONS.items():
            directory = GEN / edition
            if not directory.is_dir():
                continue
            text = "".join(p.read_text(encoding="utf-8") for p in directory.glob("nfscarbon_recomp.*.cpp"))
            start = text.index(f"DEFINE_REX_FUNC(sub_{address})")
            body = text[start:text.index("\nDEFINE_REX_FUNC(", start + 10)]
            # this = r3 -> r20; min/max LOD read where the hook writes them; min + index capped by max.
            self.assertRegex(body, r"r20\.u64 = (ctx\.)?r3\.u64;")
            self.assertEqual(len(re.findall(r"r20\.u32 \+ 12492", body)), 3, edition)
            self.assertEqual(len(re.findall(r"r20\.u32 \+ 12496", body)), 1, edition)

    def test_default_is_off_and_only_pinned_cars_move(self):
        hook = (ROOT / "carbon-source/src/car_lod_hook.cpp").read_text(encoding="utf-8")
        self.assertIn("REXCVAR_DEFINE_INT32(carbon_ai_car_lod_bias, 0,", hook)
        self.assertIn("carbon_ai_car_lod_min_cars, 5,", hook)  # menus and the garage stay untouched
        self.assertIn("if (min_lod != max_lod)", hook)  # variable-range cars (the player's, showroom) never move
        self.assertIn("min_lod < 1 || min_lod >= kLastLod", hook)
        self.assertIn("constexpr int32_t kLastLod = 2;", hook)  # level 3 draws no body (hardware)
        for field in ("kMinLod", "kMaxLod"):  # both fields are raised and then restored after the call
            self.assertLess(hook.index(f"REX_STORE_U32(car + {field}, uint32_t(raised))"),
                            hook.index(f"REX_STORE_U32(car + {field}, uint32_t({'min_lod' if field == 'kMinLod' else 'max_lod'}))"))

if __name__ == "__main__":
    unittest.main()