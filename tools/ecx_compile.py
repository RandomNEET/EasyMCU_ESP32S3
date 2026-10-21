#!/usr/bin/env python3
"""Compile an ECS script with the existing EasyCon CLI and an installed .NET 10 SDK."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def has_dotnet10():
    if not shutil.which("dotnet"):
        return False
    result = subprocess.run(["dotnet", "--list-sdks"], capture_output=True, text=True)
    return result.returncode == 0 and any(
        line.startswith("10.") for line in result.stdout.splitlines()
    )


def main():
    default_root = Path(__file__).resolve().parents[2] / "EasyCon"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", type=Path, help="input .ecs file")
    parser.add_argument(
        "--out", "-o", type=Path, help="default: input path with .ecx suffix"
    )
    parser.add_argument(
        "--easycon",
        type=Path,
        default=Path(os.environ.get("EASYCON_ROOT", str(default_root))),
        help="EasyCon repo root (default: sibling EasyCon repo or EASYCON_ROOT)",
    )
    args = parser.parse_args()
    source = args.script.expanduser().resolve()
    target = args.out.expanduser().resolve() if args.out else source.with_suffix(".ecx")
    project = (
        args.easycon.expanduser().resolve() / "src/EasyCon2.CLI/EasyCon2.CLI.csproj"
    )
    if not source.is_file() or source.suffix.lower() != ".ecs":
        parser.error(f"Input must be an existing .ecs file: {source}")
    if not project.is_file():
        parser.error(
            f"EasyCon CLI not found: {project}; use --easycon /path/to/EasyCon"
        )
    if target == source:
        parser.error("Output must not overwrite the ECS source file")
    command = [
        "dotnet",
        "run",
        "--project",
        str(project),
        "--",
        "compile",
        str(source),
        "--out",
        str(target),
    ]
    try:
        if not has_dotnet10():
            parser.error(
                ".NET 10 SDK not found on PATH. Enter the project's devenv shell, "
                "or install the .NET 10 SDK for your OS and reopen the terminal. "
                "The compiler script does not install dependencies."
            )
        target.parent.mkdir(parents=True, exist_ok=True)
        # Argument lists preserve paths containing spaces and avoid shell glob expansion.
        return subprocess.run(command).returncode
    except OSError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
