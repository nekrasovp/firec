#!/usr/bin/env python3
"""Integration tests for firec's terminal-owned shellsaver mode."""

from __future__ import annotations

import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time
from pathlib import Path


ALT_SCREEN_ENTER = b"\x1b[?1049h"
ALT_SCREEN_LEAVE = b"\x1b[?1049l"
CURSOR_HIDE = b"\x1b[?25l"
CURSOR_SHOW = b"\x1b[?25h"


def fail(message: str) -> None:
    raise AssertionError(message)


def start_shellsaver(
    executable: Path, seconds: int
) -> tuple[subprocess.Popen[bytes], int, list]:
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 18, 49, 0, 0))
    original_attributes = termios.tcgetattr(slave)
    environment = os.environ.copy()
    environment["TERM"] = "xterm-256color"
    process = subprocess.Popen(
        [str(executable), "--shellsaver", "--seconds", str(seconds)],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
        env=environment,
        start_new_session=True,
    )
    os.close(slave)
    return process, master, original_attributes


def read_available(master: int) -> bytes:
    output = bytearray()
    while select.select([master], [], [], 0)[0]:
        try:
            chunk = os.read(master, 1 << 20)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)
    return bytes(output)


def collect_until_exit(process: subprocess.Popen[bytes], master: int, timeout: float) -> bytes:
    output = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        readable, _, _ = select.select([master], [], [], 0.05)
        if readable:
            try:
                output.extend(os.read(master, 1 << 20))
            except OSError:
                pass
        if process.poll() is not None:
            output.extend(read_available(master))
            return bytes(output)

    process.kill()
    process.wait(timeout=2)
    fail("shellsaver did not exit before the test timeout")


def assert_terminal_restored(master: int, original_attributes: list) -> None:
    restored = termios.tcgetattr(master)
    if restored != original_attributes:
        fail("shellsaver did not restore terminal attributes")


def test_timeout(executable: Path) -> None:
    process, master, original_attributes = start_shellsaver(executable, 1)
    try:
        output = collect_until_exit(process, master, 5)
        if process.returncode != 0:
            fail(f"timed shellsaver returned {process.returncode}")
        for sequence in (ALT_SCREEN_ENTER, ALT_SCREEN_LEAVE, CURSOR_HIDE, CURSOR_SHOW):
            if sequence not in output:
                fail(f"missing terminal lifecycle sequence: {sequence!r}")
        for metric in (b"CPU", b"RAM", b"TEMP", b"TIME"):
            if metric in output:
                fail(f"shellsaver rendered monitoring data: {metric!r}")
        assert_terminal_restored(master, original_attributes)
    finally:
        os.close(master)


def test_any_key(executable: Path) -> None:
    process, master, original_attributes = start_shellsaver(executable, 10)
    try:
        output = bytearray()
        deadline = time.monotonic() + 3
        while ALT_SCREEN_ENTER not in output and time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.05)
            if readable:
                try:
                    output.extend(os.read(master, 1 << 20))
                except OSError:
                    break
        if ALT_SCREEN_ENTER not in output:
            process.kill()
            process.wait(timeout=2)
            fail("shellsaver did not enter the alternate screen")

        started = time.monotonic()
        os.write(master, b"x")
        output.extend(collect_until_exit(process, master, 3))
        if process.returncode != 0:
            fail(f"key-triggered shellsaver returned {process.returncode}")
        if time.monotonic() - started > 2:
            fail("shellsaver did not react promptly to keyboard input")
        if ALT_SCREEN_LEAVE not in output or CURSOR_SHOW not in output:
            fail("key-triggered exit skipped terminal cleanup")
        assert_terminal_restored(master, original_attributes)
    finally:
        os.close(master)


def test_signal_cleanup(executable: Path) -> None:
    process, master, original_attributes = start_shellsaver(executable, 10)
    try:
        output = bytearray()
        deadline = time.monotonic() + 3
        while ALT_SCREEN_ENTER not in output and time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.05)
            if readable:
                try:
                    output.extend(os.read(master, 1 << 20))
                except OSError:
                    break
        if ALT_SCREEN_ENTER not in output:
            process.kill()
            process.wait(timeout=2)
            fail("shellsaver did not start before the signal test")

        process.send_signal(signal.SIGTERM)
        output.extend(collect_until_exit(process, master, 3))
        if process.returncode != 0:
            fail(f"signal-triggered shellsaver returned {process.returncode}")
        if ALT_SCREEN_LEAVE not in output or CURSOR_SHOW not in output:
            fail("signal-triggered exit skipped terminal cleanup")
        assert_terminal_restored(master, original_attributes)
    finally:
        os.close(master)


def test_cli(executable: Path) -> None:
    help_result = subprocess.run(
        [str(executable), "--help"], capture_output=True, check=False, text=True
    )
    if help_result.returncode != 0 or "--shellsaver" not in help_result.stdout:
        fail("--help does not describe shellsaver mode")

    for arguments in (("--shellsaver", "--seconds", "0"), ("--unknown",)):
        result = subprocess.run(
            [str(executable), *arguments], capture_output=True, check=False, text=True
        )
        if result.returncode != 2:
            fail(f"invalid arguments {arguments!r} returned {result.returncode}")


def main() -> None:
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else "./fire").resolve()
    test_cli(executable)
    test_timeout(executable)
    test_any_key(executable)
    test_signal_cleanup(executable)
    print("shellsaver tests: OK")


if __name__ == "__main__":
    main()
