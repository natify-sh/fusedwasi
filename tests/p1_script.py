"""Runs a script of WASI 0.1 calls (tests/data/p1/*.txt) on the Python host, and returns the lines that tests/data/wasi_p1_driver.c prints for the same script."""

from __future__ import annotations

import errno
import re
import struct
from pathlib import Path
from typing import Any

from fusedwasi.p1 import wasi_p1, wasi_p1_unstable
from fusedwasi.p1.wasi_p1_core import Exit, Signal, State
from fusedwasi.p2.wasi_p2_core import Host
from fusedwasi.p2.wasi_p2_io import InputStream, OutputStream, StreamFailed

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


class Remote:
    """A memory that is not a buffer, as an engine keeps it out of this process: `read`, `write` and `len` only."""

    def __init__(self, data: bytearray) -> None:
        self.data = data

    def __len__(self) -> int:
        return len(self.data)

    def read(self, offset: int, length: int) -> bytes:
        return bytes(self.data[offset : offset + length])

    def write(self, offset: int, data: bytes) -> None:
        self.data[offset : offset + len(data)] = data


def run(script: Path, tmp: str, remote: bool = False) -> list[str]:
    out: list[str] = []
    args: list[bytes] = []
    env: list[bytes] = []
    preopens: list[tuple[str, str, bool]] = []
    hooks_on = False
    stdin_data = b""
    status = {"stdout": 0, "clock": 0, "random": 0}
    memory = bytearray()
    state: State | None = None
    table: Any = wasi_p1

    class Stdin(InputStream):
        def read(self, n: int) -> bytes:
            nonlocal stdin_data
            data, stdin_data = stdin_data[:n], stdin_data[n:]
            return data

    class Stdout(OutputStream):
        def __init__(self, fd: int) -> None:
            self.fd = fd

        def check_write(self) -> int:
            return 4096

        def write(self, data: bytes) -> None:
            if status["stdout"]:
                raise StreamFailed("write failed", errno=errno.EINVAL if status["stdout"] == 28 else errno.EIO)
            out.append(f"out {self.fd} {data.hex()}")

    def hook_clock(ident: int) -> int:
        if status["clock"]:
            raise OSError("the clock cannot be read")
        return 1000 + 7 * ident

    def hook_random(n: int) -> bytes:
        if status["random"]:
            raise OSError("the source of random bytes failed")
        return bytes((i * 3 + 1) & 0xFF for i in range(n))

    def hook_sleep(ns: int) -> None:
        out.append(f"sleep {ns}")

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
            host_path = tmp + t[1][4:].decode() if t[1].startswith(b"$TMP") else t[1].decode()
            preopens.append((host_path, t[2].decode(), not (len(t) > 3 and t[3] == b"ro")))
        elif word == "mode":
            table = wasi_p1_unstable if t[1] == b"unstable" else wasi_p1
        elif word == "hooks":
            hooks_on = t[1] == b"on"
        elif word == "start":
            memory = bytearray(int(t[1]))
            core = Host(
                args=[a.decode() for a in args] or ["program"],
                env=[(k.decode(), v.decode()) for k, _, v in (e.partition(b"=") for e in env)],
            )
            if hooks_on:
                core.clock, core.random_bytes, core.sleep = hook_clock, hook_random, hook_sleep
                core.std.update(stdin=Stdin(), stdout=Stdout(1), stderr=Stdout(2))
            state = State(
                Remote(memory) if remote else memory,
                core=core,
                preopens=preopens,
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
            elif sub == "u64":
                struct.pack_into("<Q", memory, int(t[2], 0), int(t[3], 0))
            elif sub == "zero":
                at, length = int(t[2], 0), int(t[3], 0)
                memory[at : at + length] = bytes(length)
            elif sub == "dump":
                at, length = int(t[2], 0), int(t[3], 0)
                out.append(f"{t[4].decode()} {bytes(memory[at : at + length]).hex()}")
        elif (
            word == "dirents"
        ):  # dirents <buffer> <used address> <label>: the entries of a readdir buffer, without the inode numbers
            used = struct.unpack_from("<I", memory, int(t[2], 0))[0]
            at = int(t[1], 0)
            end = at + used
            while at + 24 <= end:
                next_cookie, _ino, namelen, kind = struct.unpack_from("<QQIB", memory, at)
                have = min(namelen, end - (at + 24))
                cut = " (cut)" if have < namelen else ""
                out.append(
                    f"{t[3].decode()} next {next_cookie} type {kind} name {bytes(memory[at + 24 : at + 24 + have]).hex()}{cut}"
                )
                at += 24 + have
        elif word == "call":
            assert state is not None
            name = t[1].decode()
            numbers = [int(x, 0) for x in t[2:]]
            try:
                result = getattr(table, name)(state, *numbers)
                out.append(f"{name} trapped" if name == "proc_exit" else f"{name} {result}")
            except (Exit, Signal):
                out.append(f"{name} trapped")
                out.append(f"end {state.finished} {state.end_value}")
                out.append(f"exit {int(state.core.exited)} {state.core.exit_code}")
    if state is not None:
        state.close_all()
    return out
