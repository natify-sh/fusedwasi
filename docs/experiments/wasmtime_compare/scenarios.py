"""The scenarios: the same list of steps for any `Rt` of harness.py. A step is `(label, text)`; two runs are compared label by label.

Handles of a resource (a descriptor, a stream) are different numbers in the two hosts, so a step never prints one.
"""

from __future__ import annotations

import os
import time
from collections.abc import Callable
from pathlib import Path

from harness import (
    DATA,
    RET,
    D,
    Ended,
    R,
    Rt,
    scalar,
    window_u8,
    window_u32,
    window_u64,
)

Steps = list[tuple[str, str]]
Fresh = Callable[[], Rt]  # a new instance of the guest (a trap or an exit may leave the old one unusable)

FS_ERRORS = [
    "access", "would-block", "already", "bad-descriptor", "busy", "deadlock", "quota", "exist", "file-too-large",
    "illegal-byte-sequence", "in-progress", "interrupted", "invalid", "io", "is-directory", "loop", "too-many-links",
    "message-size", "name-too-long", "no-device", "no-entry", "no-lock", "insufficient-memory", "insufficient-space",
    "not-directory", "not-empty", "not-recoverable", "unsupported", "no-tty", "no-such-device", "overflow",
    "not-permitted", "pipe", "read-only", "invalid-seek", "text-file-busy", "cross-device",
]  # fmt: skip
TYPES = ["unknown", "block-device", "character-device", "directory", "fifo", "symbolic-link", "regular-file", "socket"]
FOLLOW = 1
CREATE, DIRECTORY, EXCLUSIVE, TRUNCATE = 1, 2, 4, 8
READ, WRITE, MUTATE = 1, 2, 32


def make_tree(base: Path) -> Path:
    """A tree for the scenarios; returns the directory to preopen. `base/outdir` is outside it."""
    root = base / "world"
    (root / "sub").mkdir(parents=True)
    (root / "sub" / "f").write_text("data")
    (root / "sub" / "g").write_text("")
    (root / "empty").mkdir()
    (root / "full").mkdir()
    (root / "full" / "x").write_text("x")
    (root / "hello.txt").write_text("hello world\n")
    (root / "other.txt").write_text("other")
    (root / "ro.txt").write_text("fixed")
    (root / "ro.txt").chmod(0o444)
    os.symlink("sub", root / "ln")
    os.symlink("hello.txt", root / "filelink")
    os.symlink("/etc", root / "abs")
    os.symlink("../x", root / "up")
    os.symlink("loop2", root / "loop1")
    os.symlink("loop1", root / "loop2")
    os.symlink("nothing", root / "dangling")
    os.symlink("nothing", root / "dang2")
    os.symlink("sub", root / "ln2")
    (base / "outdir").mkdir()
    (base / "outdir" / "secret").write_text("secret")
    os.symlink("../outdir", root / "outlink")
    os.symlink("../outdir/secret", root / "outfile")
    return root


# ---- WASI 0.2 -----------------------------------------------------------------------------------------------------------------


class P2:
    def __init__(self, rt: Rt) -> None:
        self.rt = rt
        self.steps: Steps = []
        w = rt.call("preopens", R(0))
        ptr = window_u32(w, 0)
        self.root = int.from_bytes(rt.peek(ptr, 4), "little")

    def say(self, label: str, text: str) -> None:
        self.steps.append((label, text))

    def string(self, ptr: int, n: int) -> str:
        return self.rt.peek(ptr, n).decode("utf-8", "replace") if n else ""

    # decoders of the results
    @staticmethod
    def code(w: bytes, off: int) -> str:
        return "err=" + FS_ERRORS[window_u8(w, off)]

    def unit(self, op: str, *args: int, data: bytes = b"") -> str:
        w = self.rt.call(op, *args, data=data)
        return self.code(w, 1) if window_u8(w, 0) else "ok"

    def open(
        self, label: str, path: str, pf: int = 0, of: int = 0, df: int = READ, at: int | None = None
    ) -> int | None:
        raw = path.encode()
        w = self.rt.call("open-at", self.root if at is None else at, pf, D(0), len(raw), of, df, R(0), data=raw)
        if window_u8(w, 0):
            self.say(label, self.code(w, 4))
            return None
        self.say(label, "ok")
        return window_u32(w, 4)

    def stat(self, handle: int) -> str:
        w = self.rt.call("stat", handle, R(0))
        if window_u8(w, 0):
            return self.code(w, 8)
        return f"type={TYPES[window_u8(w, 8)]} size={window_u64(w, 24)} links={window_u64(w, 16)}"

    def stat_at(self, path: str, pf: int) -> str:
        raw = path.encode()
        w = self.rt.call("stat-at", self.root, pf, D(0), len(raw), R(0), data=raw)
        if window_u8(w, 0):
            return self.code(w, 8)
        return f"type={TYPES[window_u8(w, 8)]} size={window_u64(w, 24)}"

    def read(self, handle: int, length: int, offset: int) -> str:
        w = self.rt.call("read", handle, length, offset, R(0))
        if window_u8(w, 0):
            return self.code(w, 4)
        data = self.rt.peek(window_u32(w, 4), window_u32(w, 8))
        return f"{data!r} eof={bool(window_u8(w, 12))}"

    def write(self, handle: int, data: bytes, offset: int) -> str:
        w = self.rt.call("write", handle, D(0), len(data), offset, R(0), data=data)
        return self.code(w, 8) if window_u8(w, 0) else f"written={window_u64(w, 8)}"

    def path_op(self, op: str, path: str) -> str:
        raw = path.encode()
        return self.unit(op, self.root, D(0), len(raw), R(0), data=raw)

    def readdir(self, handle: int) -> str:
        w = self.rt.call("read-directory", handle, R(0))
        if window_u8(w, 0):
            return self.code(w, 4)
        stream = window_u32(w, 4)
        names = []
        while True:
            w = self.rt.call("read-entry", stream, R(0))
            if window_u8(w, 0):
                names.append(self.code(w, 4))
                break
            if not window_u8(w, 4):
                break
            names.append(f"{self.string(window_u32(w, 12), window_u32(w, 16))}:{TYPES[window_u8(w, 8)]}")
        self.rt.call("drop-entries", stream)
        return ",".join(sorted(names))

    def readlink(self, path: str) -> str:
        raw = path.encode()
        w = self.rt.call("readlink", self.root, D(0), len(raw), R(0), data=raw)
        if window_u8(w, 0):
            return self.code(w, 4)
        return "target=" + self.string(window_u32(w, 4), window_u32(w, 8))


def fs_p2(rt: Rt, fresh: Fresh) -> Steps:
    c = P2(rt)
    w = rt.call("preopens", R(0))
    n = window_u32(w, 4)
    names = [
        c.string(*[int.from_bytes(rt.peek(window_u32(w, 0) + 12 * i + o, 4), "little") for o in (4, 8)])
        for i in range(n)
    ]
    c.say("preopens", f"{names}")
    w = rt.call("get-type", c.root, R(0))
    c.say("type of the preopen", TYPES[window_u8(w, 1)])
    c.say("stat of the preopen", c.stat(c.root))
    c.say("readdir /", c.readdir(c.root))

    # reading
    h = c.open("open hello.txt", "hello.txt")
    if h is not None:
        c.say("read 5@0", c.read(h, 5, 0))
        c.say("read 100@6", c.read(h, 100, 6))
        c.say("read 5@100 (past the end)", c.read(h, 5, 100))
        c.say("read 0@0", c.read(h, 0, 0))
        c.say("stat hello.txt", c.stat(h))
        c.say("write to a descriptor opened for reading", c.write(h, b"x", 0))
        c.say("set-size on a descriptor opened for reading", c.unit("set-size", h, 0, R(0)))
        c.rt.call("drop-descriptor", h)

    # ways of opening
    c.open("open missing", "missing")
    c.open("open empty path", "")
    c.open("open '.'", ".")
    c.open("open 'sub/..'", "sub/..")
    c.open("open 'sub//f'", "sub//f")
    c.open("open 'sub/../..' (above the preopen)", "sub/../..")
    c.open("open '..'", "..")
    c.open("open '../outdir/secret'", "../outdir/secret")
    c.open("open 'sub/../../outdir/secret'", "sub/../../outdir/secret")
    c.open("open '/etc/passwd' (absolute)", "/etc/passwd")
    c.open("open '/' (absolute)", "/")
    c.open("open 'outlink/secret' (link out, follow)", "outlink/secret", FOLLOW)
    c.open("open 'outlink/secret' (link out, no follow flag)", "outlink/secret")
    c.open("open 'outfile' (link to a file outside, follow)", "outfile", FOLLOW)
    c.open("open 'outlink' (link to a directory outside, follow)", "outlink", FOLLOW, DIRECTORY)
    c.open("open 'abs' (link to /etc, follow)", "abs", FOLLOW)
    c.open("open 'up' (link to ../x, follow)", "up", FOLLOW)
    c.open("open 'ln' (link to a directory, follow)", "ln", FOLLOW)
    c.open("open 'ln' (link to a directory, no follow)", "ln")
    c.open("open 'ln' (no follow, directory flag)", "ln", 0, DIRECTORY)
    c.open("open 'ln/f' (through a link in the middle)", "ln/f")
    c.open("open 'filelink' (follow)", "filelink", FOLLOW)
    c.open("open 'filelink' (no follow)", "filelink")
    c.open("open 'dangling' (follow)", "dangling", FOLLOW)
    c.open("open 'dangling' (no follow)", "dangling")
    c.open("open 'loop1' (follow)", "loop1", FOLLOW)
    c.open("open 'loop1' (no follow)", "loop1")
    c.open("open 'hello.txt/' (slash after a file)", "hello.txt/")
    c.open("open 'hello.txt/x'", "hello.txt/x")
    c.open("open 'hello.txt' with directory", "hello.txt", 0, DIRECTORY)
    c.open("open 'sub' for writing", "sub", 0, 0, WRITE)
    c.open("open 'new.txt' without create", "new.txt")
    c.open("open 'hello.txt' create+exclusive", "hello.txt", 0, CREATE | EXCLUSIVE)
    c.open("open 'ro.txt' for writing", "ro.txt", 0, 0, WRITE)
    c.open("open 'ro.txt' truncate", "ro.txt", 0, TRUNCATE, WRITE)
    c.open("open 'x' * 300 (name too long)", "x" * 300)
    c.open("open 'a\\0b' (nul in the name)", "a\0b")
    c.open("open 'sub' read+mutate-directory", "sub", 0, DIRECTORY, READ | MUTATE)

    # writing
    h = c.open("create new.txt", "new.txt", 0, CREATE, READ | WRITE)
    if h is not None:
        c.say("write 'abc'@0", c.write(h, b"abc", 0))
        c.say("write 'Z'@5 (past the end)", c.write(h, b"Z", 5))
        c.say("read back", c.read(h, 100, 0))
        c.say("stat new.txt", c.stat(h))
        c.say("set-size 2", c.unit("set-size", h, 2, R(0)))
        c.say("read after set-size", c.read(h, 100, 0))
        c.rt.call("drop-descriptor", h)
    h = c.open("create 'sub/deep.txt'", "sub/deep.txt", 0, CREATE | EXCLUSIVE, WRITE)
    if h is not None:
        c.rt.call("drop-descriptor", h)
    c.open("create 'missing/x.txt'", "missing/x.txt", 0, CREATE, WRITE)
    c.open("create '../outdir/new.txt' (outside)", "../outdir/new.txt", 0, CREATE, WRITE)
    c.open("create '/tmp/fusedwasi-cmp-abs' (absolute)", "/tmp/fusedwasi-cmp-abs", 0, CREATE, WRITE)

    # stat
    c.say("stat-at hello.txt", c.stat_at("hello.txt", 0))
    c.say("stat-at missing", c.stat_at("missing", 0))
    c.say("stat-at ln (no follow)", c.stat_at("ln", 0))
    c.say("stat-at ln (follow)", c.stat_at("ln", FOLLOW))
    c.say("stat-at dangling (no follow)", c.stat_at("dangling", 0))
    c.say("stat-at dangling (follow)", c.stat_at("dangling", FOLLOW))
    c.say("stat-at '../outdir' (no follow)", c.stat_at("../outdir", 0))
    c.say("stat-at 'outlink' (follow, outside)", c.stat_at("outlink", FOLLOW))
    c.say("stat-at '/etc' (absolute)", c.stat_at("/etc", 0))
    c.say("stat-at 'loop1' (follow)", c.stat_at("loop1", FOLLOW))
    c.say("stat-at 'hello.txt/' ", c.stat_at("hello.txt/", 0))

    # unlink
    c.say("unlink a directory ('empty')", c.path_op("unlink", "empty"))
    c.say("unlink a directory with files ('full')", c.path_op("unlink", "full"))
    c.say("unlink missing", c.path_op("unlink", "missing"))
    c.say("unlink '../outdir/secret'", c.path_op("unlink", "../outdir/secret"))
    c.say("unlink '/etc/passwd' (absolute)", c.path_op("unlink", "/etc/passwd"))
    c.say("unlink 'outlink/secret' (through a link out)", c.path_op("unlink", "outlink/secret"))
    c.say("unlink 'outfile' (a link to a file outside)", c.path_op("unlink", "outfile"))
    c.say("unlink 'ln' (a link to a directory)", c.path_op("unlink", "ln"))
    c.say("unlink 'hello.txt/' (slash after a file)", c.path_op("unlink", "hello.txt/"))
    c.say("unlink 'ro.txt' (read-only file)", c.path_op("unlink", "ro.txt"))
    c.say("unlink 'other.txt'", c.path_op("unlink", "other.txt"))
    c.say("unlink 'other.txt' again", c.path_op("unlink", "other.txt"))
    c.say("unlink '' (empty)", c.path_op("unlink", ""))
    c.say("unlink '.'", c.path_op("unlink", "."))
    c.say("unlink 'sub/..'", c.path_op("unlink", "sub/.."))
    c.say("unlink 'dangling'", c.path_op("unlink", "dangling"))

    # rmdir and mkdir
    c.say("rmdir a file", c.path_op("rmdir", "hello.txt"))
    c.say("rmdir a directory with files", c.path_op("rmdir", "full"))
    c.say("rmdir missing", c.path_op("rmdir", "missing"))
    c.say("rmdir 'empty'", c.path_op("rmdir", "empty"))
    c.say("rmdir '.'", c.path_op("rmdir", "."))
    c.say("rmdir '..'", c.path_op("rmdir", ".."))
    c.say("rmdir '../outdir'", c.path_op("rmdir", "../outdir"))
    c.say("rmdir 'ln2' (a link to a directory)", c.path_op("rmdir", "ln2"))
    c.say("rmdir 'full/' (non-empty, trailing slash)", c.path_op("rmdir", "full/"))
    c.say("mkdir existing", c.path_op("mkdir", "sub"))
    c.say("mkdir existing file", c.path_op("mkdir", "hello.txt"))
    c.say("mkdir 'newdir'", c.path_op("mkdir", "newdir"))
    c.say("mkdir 'a/b' (missing parent)", c.path_op("mkdir", "a/b"))
    c.say("mkdir '../outdir/n'", c.path_op("mkdir", "../outdir/n"))
    c.say("mkdir '/tmp/fusedwasi-cmp-dir' (absolute)", c.path_op("mkdir", "/tmp/fusedwasi-cmp-dir"))
    c.say("mkdir ''", c.path_op("mkdir", ""))
    c.say("mkdir 'dangling' (a dangling link)", c.path_op("mkdir", "dangling"))
    c.say("mkdir 'trail/' trailing slash", c.path_op("mkdir", "trail/"))
    c.say("rmdir 'trail/' (empty, trailing slash)", c.path_op("rmdir", "trail/"))

    # rename
    def rename(old: str, new: str) -> str:
        a, b = old.encode(), new.encode()
        return c.unit("rename", c.root, D(0), len(a), c.root, D(len(a)), len(b), R(0), data=a + b)

    c.say("rename file", rename("hello.txt", "h2.txt"))
    c.say("rename missing", rename("missing", "m2"))
    c.say("rename to '../x'", rename("h2.txt", "../x"))
    c.say("rename to '/tmp/fusedwasi-cmp-r' (absolute)", rename("h2.txt", "/tmp/fusedwasi-cmp-r"))
    c.say("rename from '../outdir/secret'", rename("../outdir/secret", "stolen"))
    c.say("rename directory over a non-empty directory", rename("newdir", "full"))
    c.say("rename file over a directory", rename("h2.txt", "sub"))
    c.say("rename directory over a file", rename("sub", "h2.txt"))
    c.say("rename file over a file", rename("h2.txt", "ro.txt"))
    c.say("rename into itself", rename("sub", "sub/inner"))
    c.say("rename a link", rename("filelink", "filelink2"))
    c.say("rename to the same name", rename("ro.txt", "ro.txt"))
    c.say("rename ''", rename("", "x"))
    c.say("rename to 'sub/f/x' (through a file)", rename("ro.txt", "sub/f/x"))

    # links
    c.say("readlink filelink", c.readlink("filelink2"))
    c.say("readlink abs (absolute target)", c.readlink("abs"))
    c.say("readlink up (target outside)", c.readlink("up"))
    c.say("readlink outlink", c.readlink("outlink"))
    c.say("readlink a file", c.readlink("ro.txt"))
    c.say("readlink missing", c.readlink("missing"))
    c.say("readlink a directory", c.readlink("sub"))
    c.say("readlink dang2 (dangling)", c.readlink("dang2"))

    def symlink(target: str, name: str) -> str:
        a, b = target.encode(), name.encode()
        return c.unit("symlink", c.root, D(0), len(a), D(len(a)), len(b), R(0), data=a + b)

    c.say("symlink new", symlink("ro.txt", "sl1"))
    c.say("symlink existing", symlink("ro.txt", "sl1"))
    c.say("symlink absolute target", symlink("/etc/passwd", "sl2"))
    c.say("symlink target outside ('../x')", symlink("../x", "sl3"))
    c.say("symlink into a missing directory", symlink("ro.txt", "nodir/sl4"))
    c.say("symlink named '../sl5' (outside)", symlink("ro.txt", "../sl5"))
    c.say("symlink empty target", symlink("", "sl6"))
    c.say("readlink sl3", c.readlink("sl3"))

    def link(old: str, new: str, flags: int = 0) -> str:
        a, b = old.encode(), new.encode()
        return c.unit("link", c.root, flags, D(0), len(a), c.root, D(len(a)), len(b), R(0), data=a + b)

    c.say("hard link", link("ro.txt", "hl1"))
    c.say("hard link to existing name", link("ro.txt", "hl1"))
    c.say("hard link of a directory", link("sub", "hl2"))
    c.say("hard link of a missing file", link("missing", "hl3"))
    c.say("hard link to '../hl4'", link("ro.txt", "../hl4"))
    c.say("hard link from '../outdir/secret'", link("../outdir/secret", "hl5"))
    c.say("hard link of a symlink (no follow)", link("sl1", "hl6"))
    c.say("hard link of a symlink (follow)", link("sl1", "hl7", FOLLOW))

    # directories
    h = c.open("open sub as a directory", "sub", 0, DIRECTORY)
    if h is not None:
        c.say("readdir sub", c.readdir(h))
        c.say("read a directory", c.read(h, 5, 0))
        c.say("write to a directory", c.write(h, b"x", 0))
        c.say("stat sub", c.stat(h))
        c.rt.call("drop-descriptor", h)
    h = c.open("open hello as a file, then readdir", "ro.txt")
    if h is not None:
        c.say("readdir of a file", c.readdir(h))
        c.rt.call("drop-descriptor", h)
    c.say("readdir /", c.readdir(c.root))
    return c.steps


def cli_p2(rt: Rt, fresh: Fresh) -> Steps:
    c = P2(rt)
    w = rt.call("args", R(0))
    ptr, n = window_u32(w, 0), window_u32(w, 4)
    items = []
    for i in range(n):
        p, ln = (int.from_bytes(rt.peek(ptr + 8 * i + o, 4), "little") for o in (0, 4))
        items.append(c.string(p, ln))
    c.say("get-arguments", repr(items))
    w = rt.call("env", R(0))
    ptr, n = window_u32(w, 0), window_u32(w, 4)
    pairs = []
    for i in range(n):
        q = [int.from_bytes(rt.peek(ptr + 16 * i + o, 4), "little") for o in (0, 4, 8, 12)]
        pairs.append((c.string(q[0], q[1]), c.string(q[2], q[3])))
    c.say("get-environment", repr(pairs))
    w = rt.call("cwd", R(0))
    c.say("initial-cwd", "none" if not window_u8(w, 0) else repr(c.string(window_u32(w, 4), window_u32(w, 8))))

    out = scalar(rt.call("stdout"))
    w = rt.call("check-write", out, R(0))
    c.say("stdout check-write", "ok" if not window_u8(w, 0) else "err")
    w = rt.call("write-flush", out, D(0), 6, R(0), data=b"hello\n")
    c.say("stdout blocking-write-and-flush", "ok" if not window_u8(w, 0) else "err")
    c.say("stdout bytes seen by the host", repr(rt.stdout_text()))
    rt.call("drop-output", out)
    out2 = scalar(rt.call("stdout"))
    w = rt.call("write-flush", out2, D(0), 3, R(0), data=b"two")
    c.say("a second handle to stdout", "ok" if not window_u8(w, 0) else "err")
    c.say("stdout bytes seen by the host (2)", repr(rt.stdout_text()))
    for code, label in ((0, "exit(ok)"), (1, "exit(err)")):
        other = fresh()
        try:
            other.call("exit", code)
            c.say(label, "returned")
        except Ended as ended:
            c.say(label, f"ended code={ended.code}")
    for code in (0, 7, 255):
        other = fresh()
        try:
            other.call("exit-with-code", code)
            c.say(f"exit-with-code({code})", "returned")
        except Ended as ended:
            c.say(f"exit-with-code({code})", f"ended code={ended.code}")
    other = fresh()
    out3 = scalar(other.call("stdout"))
    try:
        w = other.call("write-flush", out3, D(0), 5000, R(0), data=b"a" * 5000)
        c.say("blocking-write-and-flush of 5000 bytes", "ok" if not window_u8(w, 0) else "err")
    except Exception:  # noqa: BLE001 -- a trap, whatever the engine calls it
        c.say("blocking-write-and-flush of 5000 bytes", "trap")
    c.say("bytes written by it", str(len(other.stdout_text())))
    other = fresh()
    out4 = scalar(other.call("stdout"))
    w = other.call("write-flush", out4, D(0), 4096, R(0), data=b"b" * 4096)
    c.say("blocking-write-and-flush of 4096 bytes", "ok" if not window_u8(w, 0) else "err")
    return c.steps


def clocks_p2(rt: Rt, fresh: Fresh) -> Steps:
    c = P2(rt)
    now = time.time()
    w = rt.call("wall-now", R(0))
    sec, ns = window_u64(w, 0), window_u32(w, 8)
    c.say("wall-clock now", f"seconds near the host clock={abs(sec - now) < 5} nanoseconds<1e9={ns < 10**9}")
    w = rt.call("wall-res", R(0))
    c.say("wall-clock resolution", f"seconds={window_u64(w, 0)} nanoseconds={window_u32(w, 8)}")
    a = scalar(rt.call("mono-now"))
    time.sleep(0.01)
    b = scalar(rt.call("mono-now"))
    c.say("monotonic now", f"nondecreasing={b >= a} advanced>=10ms={b - a >= 9_000_000}")
    c.say("monotonic resolution", str(scalar(rt.call("mono-res"))))
    p = scalar(rt.call("subscribe-duration", 0))
    c.say("subscribe-duration(0) ready", str(bool(scalar(rt.call("pollable-ready", p)))))
    rt.call("drop-pollable", p)
    p = scalar(rt.call("subscribe-duration", 50_000_000))
    c.say("subscribe-duration(50ms) ready at once", str(bool(scalar(rt.call("pollable-ready", p)))))
    t0 = scalar(rt.call("mono-now"))
    rt.call("pollable-block", p)
    t1 = scalar(rt.call("mono-now"))
    c.say("block on 50ms", f"waited>=45ms={t1 - t0 >= 45_000_000} waited<500ms={t1 - t0 < 500_000_000}")
    c.say("ready after block", str(bool(scalar(rt.call("pollable-ready", p)))))
    p2 = scalar(rt.call("subscribe-instant", 1))
    c.say("subscribe-instant(past) ready", str(bool(scalar(rt.call("pollable-ready", p2)))))
    p3 = scalar(rt.call("subscribe-duration", 10**15))
    handles = b"".join(int(x).to_bytes(4, "little") for x in (p3, p2, p))
    w = rt.call("poll", D(0), 3, R(0), data=handles)
    ptr, n = window_u32(w, 0), window_u32(w, 4)
    ready = [int.from_bytes(rt.peek(ptr + 4 * i, 4), "little") for i in range(n)]
    c.say("poll [far, past, done]", f"ready indexes={ready}")
    for x in (p, p2, p3):
        rt.call("drop-pollable", x)
    return c.steps


def random_p2(rt: Rt, fresh: Fresh) -> Steps:
    c = P2(rt)
    for n in (0, 1, 3, 16, 1000, 100000):
        w = rt.call("random-bytes", n, R(0))
        ptr, ln = window_u32(w, 0), window_u32(w, 4)
        data = rt.peek(ptr, ln)
        w2 = rt.call("random-bytes", n, R(0))
        again = rt.peek(window_u32(w2, 0), window_u32(w2, 4))
        c.say(f"get-random-bytes({n})", f"length={ln} two calls differ={data != again if n >= 16 else 'n/a'}")
    a, b = scalar(rt.call("random-u64")), scalar(rt.call("random-u64"))
    c.say("get-random-u64 twice", f"differ={a != b}")
    w = rt.call("insecure-bytes", 16, R(0))
    c.say("get-insecure-random-bytes(16)", f"length={window_u32(w, 4)}")
    a, b = scalar(rt.call("insecure-u64")), scalar(rt.call("insecure-u64"))
    c.say("get-insecure-random-u64 twice", f"differ={a != b}")
    w = rt.call("insecure-seed", R(0))
    s1 = (window_u64(w, 0), window_u64(w, 8))
    w = rt.call("insecure-seed", R(0))
    s2 = (window_u64(w, 0), window_u64(w, 8))
    c.say("insecure-seed", f"nonzero={any(s1)} stable between calls={s1 == s2}")
    other = fresh()
    try:
        w = other.call("random-bytes", 1 << 27, R(0))
        c.say("get-random-bytes(128 MiB)", f"length={window_u32(w, 4)}")
    except Exception:  # noqa: BLE001 -- a trap, whatever the engine calls it
        c.say("get-random-bytes(128 MiB)", "trap")
    return c.steps


# ---- WASI 0.1 -----------------------------------------------------------------------------------------------------------------

ERRNO_NAMES = [
    "SUCCESS",
    "2BIG",
    "ACCES",
    "ADDRINUSE",
    "ADDRNOTAVAIL",
    "AFNOSUPPORT",
    "AGAIN",
    "ALREADY",
    "BADF",
    "BADMSG",
    "BUSY",
    "CANCELED",
    "CHILD",
    "CONNABORTED",
    "CONNREFUSED",
    "CONNRESET",
    "DEADLK",
    "DESTADDRREQ",
    "DOM",
    "DQUOT",
    "EXIST",
    "FAULT",
    "FBIG",
    "HOSTUNREACH",
    "IDRM",
    "ILSEQ",
    "INPROGRESS",
    "INTR",
    "INVAL",
    "IO",
    "ISCONN",
    "ISDIR",
    "LOOP",
    "MFILE",
    "MLINK",
    "MSGSIZE",
    "MULTIHOP",
    "NAMETOOLONG",
    "NETDOWN",
    "NETRESET",
    "NETUNREACH",
    "NFILE",
    "NOBUFS",
    "NODEV",
    "NOENT",
    "NOEXEC",
    "NOLCK",
    "NOLINK",
    "NOMEM",
    "NOMSG",
    "NOPROTOOPT",
    "NOSPC",
    "NOSYS",
    "NOTCONN",
    "NOTDIR",
    "NOTEMPTY",
    "NOTRECOVERABLE",
    "NOTSOCK",
    "NOTSUP",
    "NOTTY",
    "NXIO",
    "OVERFLOW",
    "OWNERDEAD",
    "PERM",
    "PIPE",
    "PROTO",
    "PROTONOSUPPORT",
    "PROTOTYPE",
    "RANGE",
    "ROFS",
    "SPIPE",
    "SRCH",
    "STALE",
    "TIMEDOUT",
    "TXTBSY",
    "XDEV",
    "NOTCAPABLE",
]
RIGHTS_ALL = 536870911
# what wasi-libc asks for a directory: no right to write data into it (bits 0 fd_datasync, 4 fd_sync, 6 fd_write, 8 fd_allocate, 22 fd_filestat_set_size)
RIGHTS_DIR = RIGHTS_ALL & ~((1 << 0) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 22))
O_CREAT, O_DIRECTORY, O_EXCL, O_TRUNC = 1, 2, 4, 8
FILETYPES = ["unknown", "block", "char", "directory", "regular", "dgram", "stream", "symlink"]


class P1:
    def __init__(self, rt: Rt) -> None:
        self.rt = rt
        self.steps: Steps = []

    def say(self, label: str, text: str) -> None:
        self.steps.append((label, text))

    @staticmethod
    def errno(w: bytes) -> str:
        n = scalar(w)
        return ERRNO_NAMES[n] if n < len(ERRNO_NAMES) else f"?{n}"

    def path(self, op: str, path: str, *, dirfd: int = 3) -> str:
        raw = path.encode()
        return self.errno(self.rt.call(op, dirfd, D(0), len(raw), data=raw))

    def open(
        self,
        label: str,
        path: str,
        flags: int = 0,
        oflags: int = 0,
        fdflags: int = 0,
        dirfd: int = 3,
        rights: int = RIGHTS_ALL,
    ) -> int | None:
        raw = path.encode()
        w = self.rt.call("path_open", dirfd, flags, D(0), len(raw), oflags, rights, rights, fdflags, R(0), data=raw)
        self.say(label, self.errno(w))
        return window_u32(w, 0) if scalar(w) == 0 else None

    def filestat(self, w: bytes) -> str:
        if scalar(w):
            return self.errno(w)
        return f"type={FILETYPES[window_u8(w, 16)]} size={window_u64(w, 32)}"

    def iov_write(self, fd: int, data: bytes) -> str:
        iov = (DATA + 64).to_bytes(4, "little") + len(data).to_bytes(4, "little")
        # the iovec sits at DATA+0, the bytes at DATA+64
        blob = iov + bytes(56) + data
        w = self.rt.call("fd_write", fd, D(0), 1, R(0), data=blob)
        return f"{self.errno(w)} written={window_u32(w, 0)}" if scalar(w) == 0 else self.errno(w)

    def iov_read(self, fd: int, n: int) -> str:
        iov = (RET + 128).to_bytes(4, "little") + n.to_bytes(4, "little")
        w = self.rt.call("fd_read", fd, D(0), 1, R(0), data=iov)
        if scalar(w):
            return self.errno(w)
        got = window_u32(w, 0)
        return f"{self.rt.peek(RET + 128, got)!r}"


def p1(rt: Rt, fresh: Fresh) -> Steps:
    c = P1(rt)
    w = rt.call("args_sizes_get", R(0), R(4))
    c.say("args_sizes_get", f"{c.errno(w)} count={window_u32(w, 0)} size={window_u32(w, 4)}")
    w = rt.call("args_get", R(100), R(200))
    c.say("args_get", f"{c.errno(w)} {rt.peek(RET + 200, 16)!r}")
    w = rt.call("environ_sizes_get", R(0), R(4))
    c.say("environ_sizes_get", f"{c.errno(w)} count={window_u32(w, 0)} size={window_u32(w, 4)}")
    w = rt.call("environ_get", R(100), R(200))
    c.say("environ_get", f"{c.errno(w)} {rt.peek(RET + 200, 16)!r}")
    for ident in range(6):
        other = rt if ident < 4 else fresh()
        try:
            w = other.call("clock_time_get", ident, 1, R(0))
            c.say(f"clock_time_get({ident})", f"{c.errno(w)} nonzero={window_u64(w, 0) != 0}")
        except Exception:  # noqa: BLE001 -- a trap, whatever the engine calls it
            c.say(f"clock_time_get({ident})", "trap")
        other = rt if ident < 4 else fresh()
        try:
            w = other.call("clock_res_get", ident, R(0))
            c.say(f"clock_res_get({ident})", f"{c.errno(w)} res={window_u64(w, 0)}")
        except Exception:  # noqa: BLE001
            c.say(f"clock_res_get({ident})", "trap")
    w = rt.call("clock_time_get", 0, 1, R(0))
    c.say("clock_time_get(realtime) near the host clock", str(abs(window_u64(w, 0) / 1e9 - time.time()) < 5))
    for n in (0, 8, 4096):
        w = rt.call("random_get", R(0), n)
        c.say(f"random_get({n})", c.errno(w))
    for label, op, args in (
        ("random_get outside the memory", "random_get", (0x7FFFFFF0, 16)),
        ("args_sizes_get with pointers outside the memory", "args_sizes_get", (0x7FFFFFF0, 0x7FFFFFF0)),
        ("fd_write with an iovec outside the memory", "fd_write", (1, 0x7FFFFFF0, 1, R(0))),
    ):
        other = fresh()
        try:
            c.say(label, c.errno(other.call(op, *args)))
        except Exception:  # noqa: BLE001 -- a trap, whatever the engine calls it
            c.say(label, "trap")
    c.say("sched_yield", c.errno(rt.call("sched_yield")))

    # descriptors
    c.say("fd_write stdout", c.iov_write(1, b"hello\n"))
    c.say("stdout bytes seen by the host", repr(rt.stdout_text()))
    c.say("fd_write bad fd 99", c.iov_write(99, b"x"))
    c.say("fd_write to the preopen (a directory)", c.iov_write(3, b"x"))
    c.say("fd_read stdin (empty)", c.iov_read(0, 8))
    c.say("fd_read the preopen (a directory)", c.iov_read(3, 8))
    c.say("fd_close bad fd", c.errno(rt.call("fd_close", 99)))
    w = rt.call("fd_prestat_get", 3, R(0))
    c.say("fd_prestat_get 3", f"{c.errno(w)} tag={window_u8(w, 0)} name length={window_u32(w, 4)}")
    c.say("fd_prestat_get 4", c.errno(rt.call("fd_prestat_get", 4, R(0))))
    w = rt.call("fd_prestat_dir_name", 3, R(0), 1)
    c.say("fd_prestat_dir_name 3", f"{c.errno(w)} {rt.peek(RET, 1)!r}")
    w = rt.call("fd_prestat_dir_name", 3, R(0), 0)
    c.say("fd_prestat_dir_name with room 0", c.errno(w))
    w = rt.call("fd_fdstat_get", 3, R(0))
    c.say("fd_fdstat_get 3", f"{c.errno(w)} type={FILETYPES[window_u8(w, 0)]}")
    w = rt.call("fd_fdstat_get", 1, R(0))
    c.say("fd_fdstat_get 1", f"{c.errno(w)} type={FILETYPES[window_u8(w, 0)]}")
    c.say("fd_fdstat_get 99", c.errno(rt.call("fd_fdstat_get", 99, R(0))))
    w = rt.call("fd_filestat_get", 3, R(0))
    c.say("fd_filestat_get 3", c.filestat(w))
    w = rt.call("fd_seek", 3, 0, 1, R(0))
    c.say("fd_seek on a directory", c.errno(w))
    w = rt.call("fd_seek", 1, 0, 1, R(0))
    c.say("fd_seek on stdout", c.errno(w))

    # paths
    h = c.open("open hello.txt", "hello.txt")
    if h is not None:
        c.say("fd_read hello.txt", c.iov_read(h, 5))
        c.say("fd_write on a file opened with all rights", c.iov_write(h, b"HE"))
        w = rt.call("fd_seek", h, 0, 0, R(0))
        c.say("fd_seek set 0", f"{c.errno(w)} pos={window_u64(w, 0)}")
        w = rt.call("fd_filestat_get", h, R(0))
        c.say("fd_filestat_get hello.txt", c.filestat(w))
        c.say("fd_close", c.errno(rt.call("fd_close", h)))
        c.say("fd_close again", c.errno(rt.call("fd_close", h)))
        c.say("fd_read after close", c.iov_read(h, 5))
    for label, path, fl, of in [
        ("open '.' (directory rights)", ".", 0, O_DIRECTORY),
        ("open 'sub/..' (directory rights)", "sub/..", 0, O_DIRECTORY),
        ("open 'ln' follow (directory rights)", "ln", 1, O_DIRECTORY),
        ("open 'ln' no follow (directory rights)", "ln", 0, O_DIRECTORY),
        ("open 'sub' (directory rights)", "sub", 0, O_DIRECTORY),
        ("open 'hello.txt' (directory rights, no directory flag)", "hello.txt", 0, 0),
        ("open '..' (directory rights)", "..", 0, O_DIRECTORY),
        ("open 'outlink' follow (directory rights)", "outlink", 1, O_DIRECTORY),
        ("open '/' (directory rights)", "/", 0, O_DIRECTORY),
    ]:
        x = c.open(label, path, fl, of, rights=RIGHTS_DIR)
        if x is not None:
            rt.call("fd_close", x)
    # fd flags are 0 and the rights are all; the follow flag (lookupflags) is 1
    for label, path, fl, of in [
        ("open missing", "missing", 0, 0),
        ("open empty path", "", 0, 0),
        ("open '.'", ".", 0, 0),
        ("open 'sub/..'", "sub/..", 0, 0),
        ("open 'sub/../..'", "sub/../..", 0, 0),
        ("open '..'", "..", 0, 0),
        ("open '../outdir/secret'", "../outdir/secret", 0, 0),
        ("open '/etc/passwd' (absolute)", "/etc/passwd", 0, 0),
        ("open 'outlink/secret' (link out, follow)", "outlink/secret", 1, 0),
        ("open 'outlink/secret' (link out, no follow)", "outlink/secret", 0, 0),
        ("open 'outfile' (follow)", "outfile", 1, 0),
        ("open 'abs' (link to /etc, follow)", "abs", 1, 0),
        ("open 'up' (follow)", "up", 1, 0),
        ("open 'ln' (follow)", "ln", 1, 0),
        ("open 'ln' (no follow)", "ln", 0, 0),
        ("open 'ln' (no follow, directory)", "ln", 0, O_DIRECTORY),
        ("open 'filelink' (follow)", "filelink", 1, 0),
        ("open 'filelink' (no follow)", "filelink", 0, 0),
        ("open 'dangling' (follow)", "dangling", 1, 0),
        ("open 'dangling' (no follow)", "dangling", 0, 0),
        ("open 'loop1' (follow)", "loop1", 1, 0),
        ("open 'hello.txt/'", "hello.txt/", 0, 0),
        ("open 'hello.txt/x'", "hello.txt/x", 0, 0),
        ("open 'hello.txt' directory", "hello.txt", 0, O_DIRECTORY),
        ("open 'hello.txt' create+excl", "hello.txt", 0, O_CREAT | O_EXCL),
        ("open 'ro.txt' for all rights (read-only file)", "ro.txt", 0, 0),
        ("open 'x'*300", "x" * 300, 0, 0),
        ("create 'missing/x'", "missing/x", 0, O_CREAT),
        ("create '../outdir/new'", "../outdir/new", 0, O_CREAT),
        ("create 'new.txt'", "new.txt", 0, O_CREAT),
        ("trunc 'new.txt'", "new.txt", 0, O_TRUNC),
    ]:
        x = c.open(label, path, fl, of)
        if x is not None:
            rt.call("fd_close", x)
    for path, fl in [
        ("hello.txt", 0),
        ("missing", 0),
        ("ln", 0),
        ("ln", 1),
        ("dangling", 0),
        ("dangling", 1),
        ("../outdir", 0),
        ("outlink", 1),
        ("/etc", 0),
    ]:
        raw = path.encode()
        w = rt.call("path_filestat_get", 3, fl, D(0), len(raw), R(0), data=raw)
        c.say(f"path_filestat_get '{path}' follow={fl}", c.filestat(w))
    for path in [
        "empty",
        "full",
        "missing",
        "../outdir/secret",
        "/etc/passwd",
        "outlink/secret",
        "outfile",
        "ln",
        "hello.txt/",
        "ro.txt",
        "other.txt",
        "other.txt",
        "",
        ".",
        "dangling",
    ]:
        c.say(f"path_unlink_file '{path}'", c.path("path_unlink_file", path))
    for path in ["hello.txt", "full", "missing", "empty", ".", "..", "../outdir", "ln2", "full/"]:
        c.say(f"path_remove_directory '{path}'", c.path("path_remove_directory", path))
    for path in [
        "sub",
        "hello.txt",
        "newdir",
        "a/b",
        "../outdir/n",
        "/tmp/fusedwasi-cmp-dir1",
        "",
        "dangling",
        "trail/",
    ]:
        c.say(f"path_create_directory '{path}'", c.path("path_create_directory", path))
    c.say("path_remove_directory 'trail/' (empty, trailing slash)", c.path("path_remove_directory", "trail/"))

    def rename(old: str, new: str) -> str:
        a, b = old.encode(), new.encode()
        return c.errno(rt.call("path_rename", 3, D(0), len(a), 3, D(len(a)), len(b), data=a + b))

    for old, new in [
        ("hello.txt", "h2.txt"),
        ("missing", "m2"),
        ("h2.txt", "../x"),
        ("h2.txt", "/tmp/fusedwasi-cmp-r1"),
        ("../outdir/secret", "stolen"),
        ("newdir", "full"),
        ("h2.txt", "sub"),
        ("sub", "h2.txt"),
        ("h2.txt", "ro.txt"),
        ("sub", "sub/inner"),
        ("filelink", "filelink2"),
        ("ro.txt", "ro.txt"),
        ("", "x"),
    ]:
        c.say(f"path_rename '{old}' -> '{new}'", rename(old, new))
    for path in ["filelink2", "abs", "up", "outlink", "ro.txt", "missing", "sub", "dang2"]:
        raw = path.encode()
        w = rt.call("path_readlink", 3, D(0), len(raw), R(0), 64, R(100), data=raw)
        used = window_u32(w, 100)
        c.say(f"path_readlink '{path}'", f"{c.errno(w)} {rt.peek(RET, used)!r}" if scalar(w) == 0 else c.errno(w))

    def symlink(target: str, name: str) -> str:
        a, b = target.encode(), name.encode()
        return c.errno(rt.call("path_symlink", D(0), len(a), 3, D(len(a)), len(b), data=a + b))

    for target, name in [
        ("ro.txt", "sl1"),
        ("ro.txt", "sl1"),
        ("/etc/passwd", "sl2"),
        ("../x", "sl3"),
        ("ro.txt", "nodir/sl4"),
        ("ro.txt", "../sl5"),
        ("", "sl6"),
    ]:
        c.say(f"path_symlink '{target}' '{name}'", symlink(target, name))

    def link(old: str, new: str, flags: int = 0) -> str:
        a, b = old.encode(), new.encode()
        return c.errno(rt.call("path_link", 3, flags, D(0), len(a), 3, D(len(a)), len(b), data=a + b))

    for old, new, fl in [
        ("ro.txt", "hl1", 0),
        ("ro.txt", "hl1", 0),
        ("sub", "hl2", 0),
        ("missing", "hl3", 0),
        ("ro.txt", "../hl4", 0),
        ("../outdir/secret", "hl5", 0),
        ("sl1", "hl6", 0),
        ("sl1", "hl7", 1),
    ]:
        c.say(f"path_link '{old}' '{new}' flags={fl}", link(old, new, fl))

    # readdir
    h = c.open("open sub", "sub", 0, O_DIRECTORY, rights=RIGHTS_DIR)
    if h is not None:
        w = rt.call("fd_readdir", h, R(0), 256, 0, R(300))
        used = window_u32(w, 300)
        names, off, blob = [], 0, rt.peek(RET, min(used, 256))
        while off + 24 <= len(blob):
            namelen = int.from_bytes(blob[off + 16 : off + 20], "little")
            typ = blob[off + 20]
            name = blob[off + 24 : off + 24 + namelen].decode("latin-1")
            if len(name) == namelen:
                names.append(f"{name}:{FILETYPES[typ]}")
            off += 24 + namelen
        c.say("fd_readdir sub", f"{c.errno(w)} {sorted(names)}")
        w = rt.call("fd_readdir", h, R(0), 256, 99999, R(300))
        c.say("fd_readdir sub past the end", f"{c.errno(w)} used={window_u32(w, 300)}")
        rt.call("fd_close", h)
    w = rt.call("fd_readdir", 3, R(0), 4096, 0, R(300))
    c.say("fd_readdir preopen: bytes used > 0", f"{c.errno(w)} {window_u32(w, 300) > 0}")
    w = rt.call("fd_readdir", 1, R(0), 256, 0, R(300))
    c.say("fd_readdir stdout", c.errno(w))

    # the end
    for code in (0, 3):
        other = fresh()
        try:
            other.call("proc_exit", code)
            c.say(f"proc_exit({code})", "returned")
        except Ended as ended:
            c.say(f"proc_exit({code})", f"ended code={ended.code}")
    return c.steps


SCENARIOS_P2: dict[str, Callable[[Rt, Fresh], Steps]] = {
    "p2 cli": cli_p2,
    "p2 clocks": clocks_p2,
    "p2 random": random_p2,
    "p2 filesystem": fs_p2,
}
SCENARIOS_P1: dict[str, Callable[[Rt, Fresh], Steps]] = {"p1": p1}
