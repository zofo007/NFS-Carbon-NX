"""Find guest functions that read a non-volatile register before writing it.

With non_volatile_as_local (nfscarbon_manifest.toml) r14-r31, f14-f31, v14-v31 and v64-v127
are per-function locals, so a function that receives one from elsewhere (a tail split off its
owner, an SEH handler body, setjmp, the WMV decoder's VMX128 constants) reads zero unless it is
marked share_registers. Scans the PPC comments of the generated code in text order; registers
the codegen already routes through ctx (marked or auto-detected funclets) are skipped.
Read-only. Exit 1 if a function outside KNOWN_TEXT_ORDER needs a look.
"""
import argparse
import glob
import re
import sys
from pathlib import Path

# Normal functions (save helpers, called by bl) whose loop body comes first in text order and
# is entered by a goto after the register is written: not real reads, never mark them.
KNOWN_TEXT_ORDER = {"sub_82936C80", "sub_82936D80", "sub_82A8C528",  # standard
                    "sub_82936AB0", "sub_82936BB0", "sub_82A8C358"}  # the same functions in Collector's

FN = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
INSN = re.compile(r"^\s*// ([a-z][a-z0-9.+-]*)\s*(.*)$")
REG = re.compile(r"\b([rfv])(\d+)\b")
NO_DEST = ("st", "cmp", "mt", "tw", "td", "dcb", "icb", "fcmp", "vcmpbfp")  # every operand is a source
READS_DEST = {"rlwimi", "rldimi", "vrlimi128", "vmaddcfp128", "vnmsubfp128"}


def non_volatile(kind, n):
    return (kind in "rf" and 14 <= n <= 31) or (kind == "v" and (14 <= n <= 31 or 64 <= n <= 127))


def scan(generated, manifest):
    """{function: {(kind, n): first reading instruction}} for unmarked functions."""
    shared = {"sub_" + m[1] for m in re.finditer(r"^0x([0-9A-F]{8}) = \{ share_registers = true \}",
                                                 Path(manifest).read_text(encoding="utf-8"), re.M)}
    hits = {}
    for path in sorted(glob.glob(str(Path(generated) / "*_recomp.*.cpp"))):
        cur = None
        with open(path, encoding="utf-8", errors="replace") as source:
            for line in source:
                m = FN.match(line)
                if m:
                    cur = None if m[1] in shared or m[1].startswith("__") else m[1]
                    written, bad, body = set(), {}, []
                    continue
                if cur is None:
                    continue
                body.append(line)
                if line.startswith("}"):
                    text = "".join(body)
                    bad = {r: i for r, i in bad.items() if f"ctx.{r[0]}{r[1]}." not in text}
                    if bad:
                        hits[cur] = bad
                    cur = None
                    continue
                m = INSN.match(line)
                if not m or m[1].startswith("b"):
                    continue
                op, args = m[1], m[2]
                regs = [(k, int(n)) for k, n in REG.findall(args)]
                if not regs or (op.startswith("st") and ("(r1)" in args or ",r1," in args)):
                    continue  # no registers, or a prologue save of a non-volatile to the stack
                if op.startswith(NO_DEST):
                    dest, srcs = None, regs
                else:
                    dest, srcs = regs[0], regs[1:] + ([regs[0]] if op.rstrip(".") in READS_DEST else [])
                for r in srcs:
                    if non_volatile(*r) and r not in written:
                        bad.setdefault(r, f"{op} {args}")
                if dest is not None:
                    written.add(dest)
                if op.rstrip(".").endswith("u") and op[0] in "ls" and len(regs) >= 2:
                    written.add(regs[-1])  # load/store with update also writes the base register
    return hits


def main():
    root = Path(__file__).resolve().parents[1] / "carbon-source"
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--generated", default=root / "generated/default")
    parser.add_argument("--manifest", default=root / "nfscarbon_manifest.toml")
    args = parser.parse_args()
    unexpected = {f: r for f, r in scan(args.generated, args.manifest).items() if f not in KNOWN_TEXT_ORDER}
    for f, regs in sorted(unexpected.items()):
        names = " ".join(f"{k}{n}" for k, n in sorted(regs))
        print(f"{f} reads {names} before writing it | {next(iter(regs.values()))}")
    print(f"{len(unexpected)} function(s) need share_registers or a KNOWN_TEXT_ORDER entry")
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
