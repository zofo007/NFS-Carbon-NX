"""Emit a register-local copy of a generated leaf guest function, at build time (never committed).

ReXGlue keeps the volatile registers (r0, r3-r12, f0-f13, v0-v19) in the context struct. Guest memory
accesses are byte pointers too, so the compiler must assume every guest store may change them and
reloads/stores them around each access. For a LEAF function (no calls, no indirect branches) this
copies the generated body with those registers in locals, loaded once at entry and written back at
every return. Every guest memory access, FP/VMX operation, rounding and flush-mode switch stays as
generated, so the final context and memory are the same; only the context traffic disappears.

usage: localize_leaf.py [--nonvolatile] <generated_dir> <out_file> <name>=<sub_XXXXXXXX> ...
Writes one inline function `carbon::leaf::<name>(PPCContext&, uint8_t*)` per pair into out_file.
Refuses functions that call anything, branch indirectly or touch ctx fields it does not handle.
--nonvolatile also localizes r14-r31, f14-f31 and v20-v127. Needed for the context-preserving Collector's codegen,
where those registers are ctx fields; the leaf saves and restores them itself, so entry-load/exit-store is exact.
"""
import json
import re
import sys
from pathlib import Path

LOCAL = re.compile(r"ctx\.((?:r(?:0|[3-9]|1[0-2]))|(?:f(?:[0-9]|1[0-3]))|(?:v(?:[0-9]|1[0-9])))\b")
LOCAL_ALL = re.compile(r"ctx\.((?:r(?:0|[3-9]|1[0-2]|1[4-9]|2[0-9]|3[01]))|(?:f(?:[0-9]|1[0-9]|2[0-9]|3[01]))|(?:v(?:[0-9]|1[0-9]|2[0-9]|[3-9][0-9]|1[0-2][0-9])))\b")
ALLOWED_CTX = re.compile(r"ctx\.(r1|lr|fpscr|xer|cr\d)\b")
FORBIDDEN = re.compile(r"REX_CALL|__imp__|__(?:save|rest)(?:gpr|fpr|vmx)\w*\(|\bsub_[0-9A-F]+\(ctx|longjmp|setjmp|ctx\.ctr\b|REX_FUNC\(")


def extract(gen: Path, name: str) -> str:
    partition = json.loads((gen / "codegen.partition.json").read_text())
    bucket = partition["assignments"][name[4:]]
    text = (gen / f"nfscarbon_recomp.{bucket}.cpp").read_text()
    m = re.search(r"(?s)DEFINE_REX_FUNC\(" + name + r"\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\Z)", text)
    if not m:
        raise SystemExit(f"{name}: not found")
    return m.group(0)


def localize(name: str, fn: str, source: str, local=LOCAL) -> str:
    body = source.split("{", 1)[1].rstrip().rstrip("}")
    code = "\n".join(l for l in body.split("\n") if not l.strip().startswith("//"))
    if FORBIDDEN.search(code):
        raise SystemExit(f"{name}: not a leaf (calls or indirect branches)")
    used = sorted(set(local.findall(code)), key=lambda r: (r[0], int(r[1:])))
    leftover = re.findall(r"ctx\.\w+", local.sub("", code))
    if any(not ALLOWED_CTX.match(x) for x in leftover):
        raise SystemExit(f"{name}: unhandled context fields {sorted(set(leftover))}")
    code = local.sub(lambda m: "l_" + m.group(1), code)
    code = re.sub(r"\breturn;", "goto writeback;", code)
    load = " ".join(f"auto l_{r} = ctx.{r};" for r in used)
    store = " ".join(f"ctx.{r} = l_{r};" for r in used)
    return (f"// {name}, register-local copy made by scripts/localize_leaf.py from the generated code.\n"
            f"inline void {fn}(PPCContext& __restrict ctx, uint8_t* base) {{\n  {load}\n{code}\nwriteback:\n"
            f"  {store}\n}}\n")


def main() -> None:
    args = sys.argv[1:]
    local = LOCAL
    if args and args[0] == "--nonvolatile":
        args = args[1:]
        local = LOCAL_ALL
    gen, out = Path(args[0]), Path(args[1])
    parts = ["#pragma once\n// Generated at build time; do not commit.\n#include \"nfscarbon_pch.h\"\n#include <cmath>\n",
             "namespace carbon::leaf {\n"]
    for pair in args[2:]:
        fn, name = pair.split("=")
        parts.append(localize(name, fn, extract(gen, name), local))
    parts.append("}  // namespace carbon::leaf\n")
    new = "\n".join(parts)
    if not out.exists() or out.read_text() != new:  # keep the timestamp when unchanged (no rebuild)
        out.write_text(new)


if __name__ == "__main__":
    main()
