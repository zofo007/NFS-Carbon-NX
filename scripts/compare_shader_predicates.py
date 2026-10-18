"""Verify emitted operations and predicate generations against the preserved translator."""
import argparse
from pathlib import Path
import re


def parse(text):
    cases = []
    cursor = 0
    for match in re.finditer(r"(?ms)^CASE (\d+) SPIRV (\d+) (\d+)\n(.*?)^ENDCASE$", text):
        if text[cursor:match.start()].strip():
            raise ValueError("Unexpected or incomplete shader output")
        cursor = match.end()
        generation = 0
        scopes = []
        statements = []
        guards = 0
        for line in match[4].splitlines():
            line = line.strip()
            guard = re.fullmatch(r"if \(p0 == (true|false)\) \{", line)
            if guard:
                scopes.append((generation, guard[1]))
                guards += 1
                continue
            if line.startswith("}"):
                if not scopes:
                    raise ValueError("Unbalanced shader body")
                scope = scopes.pop()
                if scope is not None:
                    if line != "}":
                        raise ValueError("Unexpected predicate closure")
                    continue
            active = tuple(scope for scope in scopes if scope is not None)
            statements.append((line, active))
            if line == "p0 = xe_np;":
                generation += 1
            # shortcut: translator's line-oriented braces; use an AST if emission changes.
            balance = line.count("{") - line.count("}")
            if line.startswith("}"):
                balance += 1  # Already popped above.
            if balance < 0:
                raise ValueError("Unrecognized block closure")
            scopes.extend([None] * balance)
        if scopes:
            raise ValueError("Unbalanced shader body")
        cases.append((int(match[1]), statements, guards, int(match[2]), int(match[3])))
    if text[cursor:].strip():
        raise ValueError("Unexpected or incomplete shader output")
    if not cases or [c[0] for c in cases] != list(range(len(cases))):
        raise ValueError("Missing, reordered or duplicate shader cases")
    return cases


def compare(before, after):
    if len(before) != len(after):
        raise ValueError("Shader case count changed")
    for old, new in zip(before, after):
        if old[1] != new[1]:
            raise ValueError(f"Operations/predicate generation changed in case {old[0]}")
        if new[2] > old[2]:
            raise ValueError(f"Predicate checks increased in case {old[0]}")
    totals = tuple((sum(c[i] for c in before), sum(c[i] for c in after)) for i in (2, 3, 4))
    if totals[0][1] >= totals[0][0] or totals[2][1] >= totals[2][0]:
        raise ValueError("No reduction in checks/compiled conditional branches")
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    before, after = (parse(p.read_text(encoding="utf-8")) for p in (args.baseline, args.candidate))
    totals = compare(before, after)
    print(f"PASS: {len(before)} compiled shaders preserve operations and predicate generations")
    for name, counts in zip(("predicate checks", "SPIR-V blocks", "SPIR-V conditional branches"), totals):
        print(f"  {name}: {counts[0]} -> {counts[1]}")
    print("Synthetic shader results, not a measured Switch FPS/GPU-time improvement.")


if __name__ == "__main__":
    main()
