"""Runs a script of WASI 0.1 calls (tests/data/p1/*.txt) on the Python host, and returns the lines that tests/data/wasi_p1_driver.c prints for the same script."""

from __future__ import annotations

import re
import struct
from pathlib import Path

from fusedwasi.p1 import wasi_p1
from fusedwasi.p1.wasi_p1_core import Exit, Signal, State, WasiError

_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\S+)')
_ESCAPE = re.compile(r"\\(x[0-9a-fA-F]{2}|.)", re.DOTALL)


def _unescape(text: str) -> bytes:
    def one(match: re.Match[str]) -> str:
        c = match.group(1)
        return chr(int(c[1:], 16)) if c[0] == "x" and len(c) == 3 else {"n": "\n", "0": "\0"}.get(c, c)

    return _ESCAPE.sub(one, text).encode("latin-1")


def tokens(line: str) -> list[bytes]:
    found: list[bytes] = []
    for match in _TOKEN.finditer(line):
        if match.group(2) is not None and match.group(2).startswith("#"):
            break
        found.append(_unescape(match.group(1)) if match.group(1) is not None else match.group(2).encode())
    return found


def run(script: Path, tmp: str) -> list[str]:
    out: list[str] = []
    args: list[bytes] = []
    env: list[bytes] = []
    preopens: list[tuple[str, str, bool]] = []
    hooks_on = False
    stdin_data = b""
    status = {"stdout": 0, "clock": 0, "random": 0}
    memory = bytearray()
    state: State | None = None

    def hook_stdin(cap: int) -> bytes:
        nonlocal stdin_data
        data, stdin_data = stdin_data[:cap], stdin_data[cap:]
        return data

    def hook_stdout(fd: int, data: bytes) -> None:
        if status["stdout"]:
            raise WasiError(int(status["stdout"]))
        out.append(f"out {fd} {data.hex()}")

    def hook_clock(ident: int) -> int:
        if status["clock"]:
            raise WasiError(int(status["clock"]))
        return 1000 + 7 * ident

    def hook_random(n: int) -> bytes:
        if status["random"]:
            raise WasiError(int(status["random"]))
        return bytes((i * 3 + 1) & 0xFF for i in range(n))

    def hook_end(how: int, value: int) -> None:
        out.append(f"end {how} {value}")

    for raw in script.read_text().splitlines():
        t = tokens(raw)
        if not t:
            continue
        word = t[0].decode()
        if word == "arg":
            args.append(t[1])
        elif word == "env":
            env.append(t[1])
        elif word == "preopen":
            host_path = tmp if t[1] == b"$TMP" else t[1].decode()
            preopens.append((host_path, t[2].decode(), not (len(t) > 3 and t[3] == b"ro")))
        elif word == "hooks":
            hooks_on = t[1] == b"on"
        elif word == "start":
            memory = bytearray(int(t[1]))
            state = State(
                memory,
                args=args or None,
                env=env,
                preopens=preopens,
                stdin_read=hook_stdin if hooks_on else None,
                stdout_write=hook_stdout if hooks_on else None,
                clock=hook_clock if hooks_on else None,
                random=hook_random if hooks_on else None,
                end=hook_end if hooks_on else None,
            )
        elif word == "stdin":
            stdin_data = t[1]
        elif word in ("stdout_status", "clock_status", "random_status"):
            status[word.split("_")[0]] = int(t[1])
        elif word == "mem":
            sub = t[1].decode()
            if sub == "put":
                at = int(t[2], 0)
                memory[at : at + len(t[3])] = t[3]
            elif sub == "u32":
                struct.pack_into("<I", memory, int(t[2], 0), int(t[3], 0))
            elif sub == "dump":
                at, length = int(t[2], 0), int(t[3], 0)
                out.append(f"{t[4].decode()} {bytes(memory[at : at + length]).hex()}")
        elif word == "call":
            assert state is not None
            name = t[1].decode()
            numbers = [int(x, 0) for x in t[2:]]
            try:
                result = getattr(wasi_p1, name)(state, *numbers)
                out.append(f"{name} trapped" if name == "proc_exit" else f"{name} {result}")
            except (Exit, Signal):
                out.append(f"{name} trapped")
    if state is not None:
        state.close_all()
    return out
