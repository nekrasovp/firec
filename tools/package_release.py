#!/usr/bin/env python3
"""Build a deterministic firec binary release archive and SHA-256 file."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import re
import tarfile
import tempfile
from pathlib import Path


TOKEN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
PROJECT_ROOT = Path(__file__).resolve().parent.parent
PACKAGE_FILES = (
    ("README.md", 0o644),
    ("LICENSE", 0o644),
    ("FIREPLACE_SOURCE.md", 0o644),
    ("contrib/oh-my-zsh/README.md", 0o644),
    ("contrib/oh-my-zsh/fireplace.zsh", 0o755),
)


def validated_token(value: str, label: str) -> str:
    if not TOKEN.fullmatch(value):
        raise ValueError(f"invalid {label}: {value!r}")
    return value


def source_date_epoch() -> int:
    raw_value = os.environ.get("SOURCE_DATE_EPOCH", "0")
    try:
        value = int(raw_value)
    except ValueError as error:
        raise ValueError("SOURCE_DATE_EPOCH must be a non-negative integer") from error
    if value < 0:
        raise ValueError("SOURCE_DATE_EPOCH must be a non-negative integer")
    return value


def tar_info(name: str, mode: int, size: int, epoch: int) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.mode = mode
    info.size = size
    info.mtime = epoch
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    return info


def add_bytes(
    archive: tarfile.TarFile,
    name: str,
    content: bytes,
    mode: int,
    epoch: int,
) -> None:
    archive.addfile(tar_info(name, mode, len(content), epoch), io.BytesIO(content))


def build_archive(
    binary: Path,
    version: str,
    platform: str,
    output_dir: Path,
) -> tuple[Path, Path]:
    version = validated_token(version, "version")
    platform = validated_token(platform, "platform")
    binary = binary.resolve()
    if not binary.is_file():
        raise ValueError(f"binary does not exist: {binary}")
    if not os.access(binary, os.X_OK):
        raise ValueError(f"binary is not executable: {binary}")

    package_files = [(name, PROJECT_ROOT / name, mode) for name, mode in PACKAGE_FILES]
    for name, path, _ in package_files:
        if not path.is_file():
            raise ValueError(f"required release document does not exist: {name}")

    epoch = source_date_epoch()
    package_name = f"firec-{version}-{platform}"
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{package_name}.tar.gz"
    checksum_path = output_dir / f"{package_name}.tar.gz.sha256"

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{package_name}.", suffix=".tmp", dir=output_dir
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        with temporary_path.open("wb") as raw_archive:
            with gzip.GzipFile(
                filename="",
                mode="wb",
                compresslevel=9,
                fileobj=raw_archive,
                mtime=epoch,
            ) as compressed:
                with tarfile.open(
                    fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
                ) as archive:
                    directory = tar_info(package_name, 0o755, 0, epoch)
                    directory.type = tarfile.DIRTYPE
                    archive.addfile(directory)
                    add_bytes(
                        archive,
                        f"{package_name}/fire",
                        binary.read_bytes(),
                        0o755,
                        epoch,
                    )
                    for name, path, mode in package_files:
                        add_bytes(
                            archive,
                            f"{package_name}/{name}",
                            path.read_bytes(),
                            mode,
                            epoch,
                        )
        os.replace(temporary_path, archive_path)
    finally:
        temporary_path.unlink(missing_ok=True)

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum_path.write_text(
        f"{digest}  {archive_path.name}\n",
        encoding="ascii",
        newline="\n",
    )
    return archive_path, checksum_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        archive, checksum = build_archive(
            arguments.binary,
            arguments.version,
            arguments.platform,
            arguments.output_dir,
        )
    except ValueError as error:
        parser.error(str(error))
    print(archive)
    print(checksum)


if __name__ == "__main__":
    main()
