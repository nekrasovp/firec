#!/usr/bin/env python3
"""Capture a terminal program and render its output as an animated GIF."""

from __future__ import annotations

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import termios
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


DEFAULT_FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")


def xterm_color(index: int) -> tuple[int, int, int]:
    """Return the xterm-256 RGB value needed by firec's status line."""
    base = (
        (0, 0, 0),
        (128, 0, 0),
        (0, 128, 0),
        (128, 128, 0),
        (0, 0, 128),
        (128, 0, 128),
        (0, 128, 128),
        (192, 192, 192),
        (128, 128, 128),
        (255, 0, 0),
        (0, 255, 0),
        (255, 255, 0),
        (0, 0, 255),
        (255, 0, 255),
        (0, 255, 255),
        (255, 255, 255),
    )
    if index < 16:
        return base[index]
    if index < 232:
        level = (0, 95, 135, 175, 215, 255)
        offset = index - 16
        return (
            level[offset // 36],
            level[(offset // 6) % 6],
            level[offset % 6],
        )
    gray = 8 + (index - 232) * 10
    return gray, gray, gray


class Terminal:
    """Small ANSI parser covering the cursor and color sequences used here."""

    def __init__(self, columns: int, rows: int) -> None:
        self.columns = columns
        self.rows = rows
        self.characters = [[" "] * columns for _ in range(rows)]
        self.foreground = [[(255, 255, 255)] * columns for _ in range(rows)]
        self.background = [[(0, 0, 0)] * columns for _ in range(rows)]
        self.row = 0
        self.column = 0
        self.current_foreground = (255, 255, 255)
        self.current_background = (0, 0, 0)
        self.pending = bytearray()

    def clear(self) -> None:
        for row in range(self.rows):
            self.characters[row] = [" "] * self.columns
            self.foreground[row] = [(255, 255, 255)] * self.columns
            self.background[row] = [(0, 0, 0)] * self.columns

    def apply_sgr(self, parameters: list[int]) -> None:
        if not parameters:
            parameters = [0]
        index = 0
        while index < len(parameters):
            value = parameters[index]
            if value == 0:
                self.current_foreground = (255, 255, 255)
                self.current_background = (0, 0, 0)
                index += 1
            elif value in (38, 48) and index + 2 < len(parameters):
                target = "foreground" if value == 38 else "background"
                mode = parameters[index + 1]
                if mode == 2 and index + 4 < len(parameters):
                    color = tuple(parameters[index + 2 : index + 5])
                    index += 5
                elif mode == 5:
                    color = xterm_color(parameters[index + 2])
                    index += 3
                else:
                    index += 1
                    continue
                if target == "foreground":
                    self.current_foreground = color
                else:
                    self.current_background = color
            else:
                index += 1

    def apply_control(self, parameters: bytes, command: int) -> None:
        text = parameters.decode("ascii").lstrip("?")
        values = [int(value) if value else 0 for value in text.split(";")]
        if command == ord("H"):
            row = values[0] if values and values[0] else 1
            column = values[1] if len(values) > 1 and values[1] else 1
            self.row = min(max(row - 1, 0), self.rows - 1)
            self.column = min(max(column - 1, 0), self.columns - 1)
        elif command == ord("J") and values and values[0] == 2:
            self.clear()
            self.row = 0
            self.column = 0
        elif command == ord("K"):
            for column in range(self.column, self.columns):
                self.characters[self.row][column] = " "
                self.foreground[self.row][column] = self.current_foreground
                self.background[self.row][column] = self.current_background
        elif command == ord("m"):
            self.apply_sgr(values)

    def feed(self, data: bytes) -> None:
        self.pending.extend(data)
        position = 0
        while position < len(self.pending):
            byte = self.pending[position]
            if byte == 0x1B:
                if position + 1 >= len(self.pending):
                    break
                if self.pending[position + 1] != ord("["):
                    if self.pending[position + 1] in (ord("("), ord(")")):
                        if position + 2 >= len(self.pending):
                            break
                        position += 3
                        continue
                    position += 2
                    continue
                end = position + 2
                while end < len(self.pending) and not (
                    0x40 <= self.pending[end] <= 0x7E
                ):
                    end += 1
                if end == len(self.pending):
                    break
                self.apply_control(bytes(self.pending[position + 2 : end]), self.pending[end])
                position = end + 1
                continue
            if byte == 0x0D:
                self.column = 0
            elif byte == 0x0A:
                self.row = min(self.row + 1, self.rows - 1)
            if 0x20 <= byte <= 0x7E and self.row < self.rows and self.column < self.columns:
                self.characters[self.row][self.column] = chr(byte)
                self.foreground[self.row][self.column] = self.current_foreground
                self.background[self.row][self.column] = self.current_background
                self.column += 1
            position += 1
        del self.pending[:position]


def render_terminal(
    terminal: Terminal,
    font: ImageFont.FreeTypeFont,
    cell_width: int,
    cell_height: int,
    padding: int,
) -> Image.Image:
    image = Image.new(
        "RGB",
        (
            terminal.columns * cell_width + padding * 2,
            terminal.rows * cell_height + padding * 2,
        ),
        (0, 0, 0),
    )
    draw = ImageDraw.Draw(image)
    for row in range(terminal.rows):
        y = padding + row * cell_height
        start = 0
        while start < terminal.columns:
            color = terminal.background[row][start]
            end = start + 1
            while end < terminal.columns and terminal.background[row][end] == color:
                end += 1
            if color != (0, 0, 0):
                draw.rectangle(
                    (
                        padding + start * cell_width,
                        y,
                        padding + end * cell_width,
                        y + cell_height,
                    ),
                    fill=color,
                )
            start = end

        for column, character in enumerate(terminal.characters[row]):
            if character != " ":
                draw.text(
                    (padding + column * cell_width, y - 2),
                    character,
                    font=font,
                    fill=terminal.foreground[row][column],
                )
    return image


def capture(
    executable: Path,
    arguments: list[str],
    destination: Path,
    columns: int,
    rows: int,
    seconds: float,
    frame_rate: int,
    font_path: Path,
    font_size: int,
) -> None:
    font = ImageFont.truetype(str(font_path), font_size)
    cell_width = max(1, round(font.getlength("M")))
    cell_height = font_size + 3
    padding = 10
    terminal = Terminal(columns, rows)

    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    environment = os.environ.copy()
    if environment.get("TERM") in (None, "", "dumb"):
        environment["TERM"] = "xterm-256color"
    process = subprocess.Popen(
        [str(executable.resolve()), *arguments],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
        env=environment,
        start_new_session=True,
    )
    os.close(slave)
    os.set_blocking(master, False)

    frames: list[Image.Image] = []
    started = time.monotonic()
    warmup = 0.35
    next_frame = started + warmup
    interval = 1.0 / frame_rate
    try:
        while len(frames) < round(seconds * frame_rate):
            readable, _, _ = select.select([master], [], [], 0.01)
            if readable:
                try:
                    terminal.feed(os.read(master, 1 << 20))
                except BlockingIOError:
                    pass
            now = time.monotonic()
            while now >= next_frame and len(frames) < round(seconds * frame_rate):
                frames.append(
                    render_terminal(terminal, font, cell_width, cell_height, padding)
                )
                next_frame += interval
            if process.poll() is not None:
                raise RuntimeError("terminal program exited before the capture completed")
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            deadline = time.monotonic() + 2
            while process.poll() is None and time.monotonic() < deadline:
                readable, _, _ = select.select([master], [], [], 0.05)
                if readable:
                    try:
                        os.read(master, 1 << 20)
                    except (BlockingIOError, OSError):
                        pass
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=2)
        os.close(master)

    destination.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        destination,
        save_all=True,
        append_images=frames[1:],
        duration=round(1000 / frame_rate),
        loop=0,
        optimize=True,
        disposal=1,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, nargs="?", default=Path("./fire"))
    parser.add_argument(
        "destination",
        type=Path,
        nargs="?",
        default=Path("assets/fireplace.gif"),
    )
    parser.add_argument("--columns", type=int, default=96)
    parser.add_argument("--rows", type=int, default=28)
    parser.add_argument("--seconds", type=float, default=6.0)
    parser.add_argument("--frame-rate", type=int, default=10)
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT)
    parser.add_argument("--font-size", type=int, default=12)
    parser.add_argument(
        "--argument",
        action="append",
        default=[],
        help="argument passed to the terminal program; repeat as needed",
    )
    args = parser.parse_args()

    capture(
        args.executable,
        args.argument,
        args.destination,
        args.columns,
        args.rows,
        args.seconds,
        args.frame_rate,
        args.font,
        args.font_size,
    )


if __name__ == "__main__":
    main()
