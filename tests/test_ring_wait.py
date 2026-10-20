from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CARBON = ROOT / "carbon-source"


@unittest.skipUnless(CARBON.is_dir(), "Carbon source checkout unavailable")
class RingWaitTests(unittest.TestCase):
    def test_carbon_sites_original_watchdog_and_bounded_wait(self):
        hook = (CARBON / "src/ring_wait_hook.cpp").read_text(encoding="utf-8")
        generated = CARBON / "generated/default"
        if not generated.is_dir():
            self.skipTest("Generated local guest sources unavailable")
        assignments = {int(address, 16): bucket for address, bucket in
                       json.loads((generated / "codegen.partition.json").read_text())["assignments"].items()}

        def guest_source(address):
            # Locate the containing function in the current codegen partition.
            start = max(start for start in assignments if start <= int(address, 16))
            return (generated / f"nfscarbon_recomp.{assignments[start]}.cpp").read_text(encoding="utf-8")

        for address in ("826DBE14", "826DBBF8"):
            caller = guest_source(address)
            self.assertIn(f"0x{address}", hook)
            self.assertIn(f"ctx.lr = 0x{address};\n\tsub_826E0250(ctx, base);", caller)
            self.assertIn("+ 10768)", caller)
        original = guest_source("826E0250")
        self.assertIn("r29.u32 + 10813", original)  # ctx.r29, or a local with non_volatile_as_local
        pointer_read = hook.index("REX_LOAD_U32(REX_LOAD_U32(device + kDeviceRptrPointer))")
        self.assertLess(hook.index("const uint32_t before ="), pointer_read)
        self.assertLess(hook.index("REX_LOAD_U8(device + 10813)"), pointer_read)
        self.assertIn("if (can_wait && REX_LOAD_U32(", hook)
        self.assertLess(hook.index("REX_LOAD_U8(device + 10813)"), hook.index("svcWaitForAddress("))
        self.assertRegex(hook, r"ArbitrationType_WaitIfEqual,\s+int32_t\(before\), 1'000'000\)")
        self.assertIn("KERNELRESULT(TimedOut)", hook)
        self.assertIn("KERNELRESULT(InvalidState)", hook)
        self.assertTrue(hook.rstrip().endswith("__imp__sub_826E0250(ctx, base);\n}"))

    def test_native_activation_wake_and_counter_contract(self):
        hook = (CARBON / "src/ring_wait_hook.cpp").read_text(encoding="utf-8")
        cp = (CARBON / "renderer/src/command_processor.cpp").read_text(encoding="utf-8")
        cmake = (CARBON / "CMakeLists.txt").read_text(encoding="utf-8")
        report = (ROOT / "reference-nfsmw-nx/sdk/src/ui/switch_perf.cpp").read_text(encoding="utf-8")
        self.assertIn("carbon_cp_ring_wait_active.load(std::memory_order_acquire)", hook)
        self.assertIn("carbon_cp_ring_wait_active.store(true, std::memory_order_release)", cp)
        self.assertIn("carbon_cp_ring_wait_active.store(false, std::memory_order_release)", cp)
        wake = cp[cp.index("void WakeRptrWaiters()"):cp.index("}  // namespace")]
        self.assertLess(wake.index("fetch_add(1, std::memory_order_release)"), wake.index("svcSignalToAddress"))
        self.assertIn("SignalType_Signal, 0, -1", wake)
        self.assertIn("NFSCARBON_STATIC_CARBON_GPU=1)\n        target_sources(nfscarbon PRIVATE src/ring_wait_hook.cpp)", cmake)
        counters = {int(i): op for op, i in re.findall(r"RexSwitchPerf(Add|Max)\((\d+),", hook)}
        self.assertEqual(counters, {37: "Add", 38: "Add", 39: "Add", 40: "Max", 41: "Add"})
        self.assertGreater(int(re.search(r"kCounterCount = (\d+)", report)[1]), max(counters))
        for i in (37, 38, 39, 41):
            self.assertIn(f"delta({i})", report)
        self.assertIn("g_counters[40].exchange(0, std::memory_order_relaxed)", report)
        self.assertIn("carbon ring wait: attempts", report)
