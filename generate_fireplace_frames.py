#!/usr/bin/env python3
"""Generate an xterm-256 frame header from a fireplace video."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


FRAME_WIDTH = 128
FRAME_HEIGHT = 72
FRAME_RATE = 12
CLIP_DURATION_SECONDS = 8


def packed_color(red: int, green: int, blue: int) -> int:
    """Pack RGB into R4G3B1, favoring warm-color detail in the fireplace."""
    red_level = (red * 15 + 127) // 255
    green_level = (green * 7 + 127) // 255
    blue_level = 1 if blue >= 128 else 0
    return (red_level << 4) | (green_level << 1) | blue_level


def extract_frames(source: Path) -> bytes:
    command = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(source),
        "-t",
        str(CLIP_DURATION_SECONDS),
        "-an",
        "-vf",
        f"fps={FRAME_RATE},scale={FRAME_WIDTH}:{FRAME_HEIGHT}:flags=lanczos",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]
    return subprocess.run(command, check=True, stdout=subprocess.PIPE).stdout


def write_header(raw_frames: bytes, destination: Path) -> int:
    source_frame_size = FRAME_WIDTH * FRAME_HEIGHT * 3
    if len(raw_frames) == 0 or len(raw_frames) % source_frame_size != 0:
        raise ValueError("ffmpeg returned an incomplete frame sequence")

    frame_count = len(raw_frames) // source_frame_size
    colors = bytearray()
    for offset in range(0, len(raw_frames), 3):
        colors.append(packed_color(*raw_frames[offset : offset + 3]))

    with destination.open("w", encoding="ascii") as header:
        header.write(
            "/* Generated R4G3B1 frames derived from a CC BY 3.0 video.\n"
            " * Attribution and transformation details: FIREPLACE_SOURCE.md\n"
            " */\n"
        )
        header.write("#ifndef FIREPLACE_FRAMES_H\n#define FIREPLACE_FRAMES_H\n\n")
        header.write(f"#define FIREPLACE_FRAME_WIDTH {FRAME_WIDTH}\n")
        header.write(f"#define FIREPLACE_FRAME_HEIGHT {FRAME_HEIGHT}\n")
        header.write(f"#define FIREPLACE_FRAME_COUNT {frame_count}\n")
        header.write(f"#define FIREPLACE_FRAME_RATE {FRAME_RATE}\n\n")
        header.write("static const unsigned char fireplace_frames[] = {\n")
        for offset in range(0, len(colors), 24):
            values = ", ".join(str(value) for value in colors[offset : offset + 24])
            header.write(f"    {values},\n")
        header.write("};\n\n#endif\n")
    return frame_count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    frames = write_header(extract_frames(args.source), args.destination)
    print(f"Generated {frames} frames at {FRAME_WIDTH}x{FRAME_HEIGHT}")


if __name__ == "__main__":
    main()
