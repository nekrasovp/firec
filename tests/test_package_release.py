#!/usr/bin/env python3
"""Tests for deterministic release packaging and license provenance."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
PACKAGER = PROJECT_ROOT / "tools/package_release.py"
PACKAGE_NAME = "firec-v9.8.7-test-platform"


def package(binary: Path, output: Path) -> tuple[Path, Path]:
    result = subprocess.run(
        [
            sys.executable,
            str(PACKAGER),
            "--binary",
            str(binary),
            "--version",
            "v9.8.7",
            "--platform",
            "test-platform",
            "--output-dir",
            str(output),
        ],
        check=True,
        capture_output=True,
        text=True,
        env={"SOURCE_DATE_EPOCH": "123456789"},
    )
    paths = [Path(line) for line in result.stdout.splitlines()]
    assert len(paths) == 2
    return paths[0], paths[1]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="firec-package-test-") as directory:
        temporary = Path(directory)
        binary = temporary / "fire"
        binary.write_bytes(b"test firec binary\n")
        binary.chmod(0o755)

        first_archive, first_checksum = package(binary, temporary / "first")
        second_archive, second_checksum = package(binary, temporary / "second")

        first_bytes = first_archive.read_bytes()
        assert first_bytes == second_archive.read_bytes()
        digest = hashlib.sha256(first_bytes).hexdigest()
        assert first_checksum.read_text(encoding="ascii") == (
            f"{digest}  {first_archive.name}\n"
        )
        assert second_checksum.read_text(encoding="ascii") == (
            f"{digest}  {second_archive.name}\n"
        )

        with tarfile.open(first_archive, "r:gz") as archive:
            members = {member.name: member for member in archive.getmembers()}
            expected = {
                PACKAGE_NAME,
                f"{PACKAGE_NAME}/fire",
                f"{PACKAGE_NAME}/README.md",
                f"{PACKAGE_NAME}/LICENSE",
                f"{PACKAGE_NAME}/FIREPLACE_SOURCE.md",
                f"{PACKAGE_NAME}/contrib/oh-my-zsh/README.md",
                f"{PACKAGE_NAME}/contrib/oh-my-zsh/fireplace.zsh",
            }
            assert set(members) == expected
            assert members[PACKAGE_NAME].isdir()
            assert members[PACKAGE_NAME].mode == 0o755
            assert members[f"{PACKAGE_NAME}/fire"].mode == 0o755
            assert (
                members[f"{PACKAGE_NAME}/contrib/oh-my-zsh/fireplace.zsh"].mode
                == 0o755
            )
            for member in members.values():
                assert member.uid == 0
                assert member.gid == 0
                assert member.uname == "root"
                assert member.gname == "root"
                assert member.mtime == 123456789
            packaged_binary = archive.extractfile(f"{PACKAGE_NAME}/fire")
            assert packaged_binary is not None
            assert packaged_binary.read() == binary.read_bytes()

        invalid = subprocess.run(
            [
                sys.executable,
                str(PACKAGER),
                "--binary",
                str(binary),
                "--version",
                "v9.8.7",
                "--platform",
                "../escape",
                "--output-dir",
                str(temporary / "invalid"),
            ],
            capture_output=True,
            text=True,
        )
        assert invalid.returncode == 2
        assert "invalid platform" in invalid.stderr

    print("release package tests: OK")


if __name__ == "__main__":
    main()
