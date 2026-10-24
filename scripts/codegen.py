"""Generate one checked edition using the existing host tool; never build a host game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from scripts.check_game import check_game


def edition_paths(root, edition):
    if edition not in ("standard", "collectors", "collectors-rl"):
        raise ValueError(f"Unknown edition: {edition}")
    ce = edition != "standard"
    generated = root / "carbon-source/generated" / ("default" if not ce else edition)
    return {
        "game": root / ("game-data-collectors" if ce else "game-data"),
        "generated": generated,
        "manifest": generated / "project/nfscarbon_manifest.toml" if ce else root / "carbon-source/nfscarbon_manifest.toml",
        "glue": generated / "rexglue.cmake" if ce else root / "carbon-source/generated/rexglue.cmake",
    }


def collectors_glue(text, generated):
    # The SDK template prepends CMAKE_CURRENT_SOURCE_DIR even to absolute outputs.
    name = generated.name
    return text.replace(generated.as_posix(), f"generated/{name}").replace(
        "${CMAKE_CURRENT_SOURCE_DIR}/nfscarbon_manifest.toml",
        f"${{CMAKE_CURRENT_SOURCE_DIR}}/generated/{name}/project/nfscarbon_manifest.toml")


def register_local(template, root):
    """Standard-style register-local codegen for Collector's: the four flags plus the register-sharing functions
    (config/collectors_rl_share_registers.txt, from scripts/check_register_sharing.py on this edition's code)."""
    flags = "cr_as_local = true\nnon_volatile_as_local = true\nxer_as_local = true\nctr_as_local = true\n"
    shared = [line.strip() for line in (root / "config/collectors_rl_share_registers.txt").read_text().splitlines()
              if line.strip()]
    addresses = ["0x" + name[4:] for name in shared]
    for address in addresses:  # a share entry replaces the plain hint for the same function
        template = template.replace(f"{address} = {{}}\n", "")
    marker = "out_directory_path = @GENERATED@\n"
    if marker not in template:
        raise ValueError("manifest template has no out_directory_path line")
    template = template.replace(marker, marker + flags)
    return template.rstrip() + "\n# Register sharing (see register_local in scripts/codegen.py).\n" + "".join(
        f"{address} = {{ share_registers = true }}\n" for address in addresses)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edition", choices=("standard", "collectors", "collectors-rl"), default="standard")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    paths = edition_paths(root, args.edition)
    check_game(paths["game"], "standard" if args.edition == "standard" else "collectors")
    tool = root / "reference-nfsmw-nx/sdk/out/win-amd64/rexglue.exe"
    if not tool.is_file():
        raise SystemExit("Build the host code generator first (scripts/build-host.ps1 -CodegenOnly).")
    if args.edition != "standard":
        template = (root / "config/collectors_manifest.toml.in").read_text(encoding="utf-8")
        if args.edition == "collectors-rl":
            template = register_local(template, root)
        for name, path in {"GAME_ROOT": paths["game"], "XEX": paths["game"] / "default.xex",
                           "GENERATED": paths["generated"]}.items():
            template = template.replace("@" + name + "@", json.dumps(path.as_posix()))
        paths["manifest"].parent.mkdir(parents=True, exist_ok=True)
        if not paths["manifest"].is_file() or paths["manifest"].read_text(encoding="utf-8") != template:
            paths["manifest"].write_text(template, encoding="utf-8", newline="\n")
    else:
        source = paths["game"] / "default.xex"
        target = root / "carbon-source/game/default.xex"
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            if hashlib.file_digest(target.open("rb"), "sha256").digest() != hashlib.file_digest(source.open("rb"), "sha256").digest():
                raise SystemExit("Existing standard working XEX differs; preserving it.")
        else:
            shutil.copyfile(source, target)
    subprocess.run([str(tool), "codegen", str(paths["manifest"])], cwd=root, check=True)
    if args.edition != "standard":
        raw = (paths["manifest"].parent / "generated/rexglue.cmake").read_text(encoding="utf-8")
        glue = collectors_glue(raw, paths["generated"])
        if not paths["glue"].exists() or paths["glue"].read_text(encoding="utf-8") != glue:
            paths["glue"].write_text(glue, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
