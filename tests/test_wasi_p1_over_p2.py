"""WASI 0.1 over WASI 0.2: one state, one guest, one table. A file opened by a module of 0.1 is the resource of 0.2 that a module of 0.2 sees in the same state; the descriptors of 0.1 are freed
with the state; a preopen that cannot be opened is told; and (Python) a state can be given other files."""

import errno
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from cbuild import build

from fusedwasi.p1 import E_NOENT, PreopenError, open_descriptors, wasi_p1
from fusedwasi.p2 import KIND_DESCRIPTOR, Guest, State, wasi_p2_core
from fusedwasi.p2 import wasi_p2_filesystem as fs

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
C_P1 = C_P2.parent / "p1"
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="the C programs are built and run on POSIX only")


def python_scenario(root: Path) -> list[str]:
    """0.1 opens and writes a file, 0.2 (given the handle from the table) reads it, writes to it, and 0.1 reads what 0.2 wrote; closing it in 0.1 spends the handle."""
    lines: list[str] = []
    state = State(preopens=[(str(root), "/d")])
    memory = bytearray(4096)
    g = Guest(state, [memory], trap=lambda message: None)
    memory[100:105] = b"f.txt"
    lines.append(f"open {wasi_p1.path_open(g, 3, 1, 100, 5, 1, 0x1FFFFFFF, 0x1FFFFFFF, 0, 200)}")
    fd = int.from_bytes(memory[200:204], "little")
    lines.append(f"fd {fd}")
    memory[300:306] = b"hello\n"
    memory[400:408] = (300).to_bytes(4, "little") + (6).to_bytes(4, "little")
    lines.append(f"fd_write {wasi_p1.fd_write(g, fd, 400, 1, 500)} {int.from_bytes(memory[500:504], 'little')}")
    live = [i for i, slot in enumerate(state.slots) if slot.live and slot.kind == KIND_DESCRIPTOR]
    lines.append(f"descriptors in the table {len(live)}")  # the preopen and the file
    index = live[-1]
    handle = (state.slots[index].generation << wasi_p2_core.INDEX_BITS) | index
    fs.wasi_filesystem_types_descriptor_stat(g, 0, handle, 600)  # 0.2 asks about the file 0.1 wrote
    lines.append(f"0.2 sees size {int.from_bytes(memory[624:632], 'little')}")
    memory[700:705] = b"WORLD"
    fs.wasi_filesystem_types_descriptor_write(g, 0, handle, 700, 5, 0, 800)  # 0.2 writes at the start
    lines.append(f"0.2 wrote {memory[800]}")
    lines.append(f"fd_seek {wasi_p1.fd_seek(g, fd, 0, 0, 900)}")
    memory[400:408] = (1000).to_bytes(4, "little") + (16).to_bytes(4, "little")
    lines.append(
        f"fd_read {wasi_p1.fd_read(g, fd, 400, 1, 500)} {bytes(memory[1000 : 1000 + int.from_bytes(memory[500:504], 'little')]).hex()}"
    )
    lines.append(f"fd_close {wasi_p1.fd_close(g, fd)}")
    lines.append(
        f"descriptors after close {sum(1 for slot in state.slots if slot.live and slot.kind == KIND_DESCRIPTOR)}"
    )
    try:
        fs.wasi_filesystem_types_descriptor_stat(g, 0, handle, 600)
    except wasi_p2_core.Trap:
        lines.append("handle spent")
    state.close_all()
    lines.append(
        f"descriptors after close_all {sum(1 for slot in state.slots if slot.live and slot.kind == KIND_DESCRIPTOR)}"
    )
    return lines


C_SCENARIO = r"""#include <stdio.h>
#include <setjmp.h>
#include "wasi_p1.h"
static jmp_buf jb;
static void on_trap(fusedwasi_p2_guest *g, const char *m) { (void)g, (void)m; longjmp(jb, 1); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static unsigned live_descriptors(fusedwasi_p2_state *s) {
    unsigned n = 0;
    for (uint32_t i = 1; i < s->slot_count; i++) n += s->slots[i].live && s->slots[i].kind == FUSEDWASI_P2_KIND_DESCRIPTOR;
    return n;
}
int main(int argc, char **argv) {
    static uint8_t memory[4096];
    uint8_t *base = memory;
    uint64_t size = sizeof memory;
    struct fusedwasi_p2_preopen preopen = {argv[1], "/d", 1};
    fusedwasi_p2_state state;
    fusedwasi_p2_init(&state);
    state.preopens = &preopen, state.preopen_count = 1;
    fusedwasi_p2_guest g;
    fusedwasi_p2_guest_init(&g, &state);
    g.trap = on_trap;
    fusedwasi_p2_bind_memory(&g, 0, &base, &size);
    (void)argc;
    memcpy(memory + 100, "f.txt", 5);
    printf("open %u\n", (unsigned)fusedwasi_p1_path_open(&g, 3, 1, 100, 5, 1, 0x1FFFFFFF, 0x1FFFFFFF, 0, 200));
    uint32_t fd = rd32(memory + 200);
    printf("fd %u\n", (unsigned)fd);
    memcpy(memory + 300, "hello\n", 6);
    wr32(memory + 400, 300), wr32(memory + 404, 6);
    unsigned rc = (unsigned)fusedwasi_p1_fd_write(&g, fd, 400, 1, 500);
    printf("fd_write %u %u\n", rc, (unsigned)rd32(memory + 500));
    printf("descriptors in the table %u\n", live_descriptors(&state));
    uint32_t index = 0;
    for (uint32_t i = 1; i < state.slot_count; i++)
        if (state.slots[i].live && state.slots[i].kind == FUSEDWASI_P2_KIND_DESCRIPTOR) index = i;
    uint32_t handle = (state.slots[index].generation << FUSEDWASI_P2_INDEX_BITS) | index;
    fusedwasi_wasi_filesystem_types_descriptor_stat(&g, 0, handle, 600);
    printf("0.2 sees size %u\n", (unsigned)rd32(memory + 624));
    memcpy(memory + 700, "WORLD", 5);
    fusedwasi_wasi_filesystem_types_descriptor_write(&g, 0, handle, 700, 5, 0, 800);
    printf("0.2 wrote %u\n", memory[800]);
    printf("fd_seek %u\n", (unsigned)fusedwasi_p1_fd_seek(&g, fd, 0, 0, 900));
    wr32(memory + 400, 1000), wr32(memory + 404, 16);
    rc = (unsigned)fusedwasi_p1_fd_read(&g, fd, 400, 1, 500);
    printf("fd_read %u ", rc);
    for (uint32_t i = 0; i < rd32(memory + 500); i++) printf("%02x", memory[1000 + i]);
    printf("\n");
    printf("fd_close %u\n", (unsigned)fusedwasi_p1_fd_close(&g, fd));
    printf("descriptors after close %u\n", live_descriptors(&state));
    if (setjmp(jb) == 0) fusedwasi_wasi_filesystem_types_descriptor_stat(&g, 0, handle, 600);
    else printf("handle spent\n");
    fusedwasi_p2_free(&state);
    printf("descriptors after close_all %u\n", live_descriptors(&state));
    return 0;
}
"""


def test_a_file_of_0_1_is_a_resource_of_0_2_in_the_same_state(tmp_path: Path) -> None:
    if CC is None:
        pytest.skip("no C compiler")
    (tmp_path / "py").mkdir()
    (tmp_path / "c").mkdir()
    source = tmp_path / "scenario.c"
    source.write_text(C_SCENARIO)
    binary = tmp_path / "scenario"
    build(
        [
            CC,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(C_P2),
            "-I",
            str(C_P1),
            str(source),
            "-o",
            str(binary),
        ]
    )
    c_lines = subprocess.run(
        [str(binary), str(tmp_path / "c")], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    py_lines = python_scenario(tmp_path / "py")
    assert py_lines == c_lines
    assert py_lines == [
        "open 0",
        "fd 4",  # 3 is the preopen
        "fd_write 0 6",
        "descriptors in the table 2",  # the preopen and the file, both in the table of the state
        "0.2 sees size 6",
        "0.2 wrote 0",
        "fd_seek 0",
        "fd_read 0 574f524c440a",  # "WORLD\n" read back by 0.1, after 0.2 wrote it
        "fd_close 0",
        "descriptors after close 1",
        "handle spent",
        "descriptors after close_all 0",
    ]
    assert (tmp_path / "py" / "f.txt").read_bytes() == b"WORLD\n" == (tmp_path / "c" / "f.txt").read_bytes()


def test_a_preopen_that_cannot_be_opened_is_told(tmp_path: Path) -> None:
    (tmp_path / "there").mkdir()
    state = State(preopens=[(str(tmp_path / "there"), "/a"), (str(tmp_path / "missing"), "/b")])
    with pytest.raises(PreopenError) as caught:
        open_descriptors(state)
    assert caught.value.index == 1 and caught.value.errno == errno.ENOENT
    assert not any(slot.live and slot.kind == KIND_DESCRIPTOR for slot in state.slots)  # nothing was left open
    # the first call of a function of 0.1 on that state answers the errno instead
    guest = Guest(state, bytearray(64))
    assert wasi_p1.fd_prestat_get(guest, 3, 0) == E_NOENT


def test_the_c_function_tells_a_preopen_that_cannot_be_opened(tmp_path: Path) -> None:
    if CC is None:
        pytest.skip("no C compiler")
    source = tmp_path / "bad.c"
    source.write_text(
        r"""#include <stdio.h>
#include "wasi_p1.h"
int main(int argc, char **argv) {
    struct fusedwasi_p2_preopen p[2] = {{argv[1], "/a", 1}, {argv[2], "/b", 1}};
    fusedwasi_p2_state s;
    fusedwasi_p2_init(&s);
    s.preopens = p, s.preopen_count = 2;
    int err = 0;
    size_t bad = 0;
    int rc = fusedwasi_p1_open_descriptors(&s, &err, &bad);
    unsigned live = 0;
    for (uint32_t i = 1; i < s.slot_count; i++) live += s.slots[i].live && s.slots[i].kind == FUSEDWASI_P2_KIND_DESCRIPTOR;
    printf("%d %d %d %u\n", rc, err == ENOENT, (int)bad, live);
    (void)argc;
    fusedwasi_p2_free(&s);
    return 0;
}
"""
    )
    binary = tmp_path / "bad"
    build(
        [
            CC,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(C_P2),
            "-I",
            str(C_P1),
            str(source),
            "-o",
            str(binary),
        ]
    )
    (tmp_path / "there").mkdir()
    out = subprocess.run(
        [str(binary), str(tmp_path / "there"), str(tmp_path / "missing")], capture_output=True, text=True, check=True
    ).stdout
    assert out == "-1 1 1 0\n"


class Memory(fs.Filesystem):
    """A host with other files: one directory with one file in it, in memory. It answers what the calls of 0.1 below ask, and `unsupported` to the rest."""

    def __init__(self) -> None:
        self.files = {b"hello.txt": b"from memory\n"}

    def open_directory(self, path: str, flags: int) -> fs.Descriptor:
        return fs.Descriptor(-1, flags, True, path)

    def open_at(
        self, d: fs.Descriptor, path: bytes, path_flags: int, open_flags: int, flags: int, p1: bool = False
    ) -> fs.Descriptor:
        if path not in self.files:
            raise fs.FsError("no-entry")
        made = fs.Descriptor(-1, flags, False, path.decode())
        return made

    def stat(self, d: fs.Descriptor) -> os.stat_result:
        mode = 0o040755 if d.is_dir else 0o100644
        size = 0 if d.is_dir else len(self.files[d.path.encode()])
        return os.stat_result((mode, 1, 1, 1, 0, 0, size, 0, 0, 0))

    def read(self, d: fs.Descriptor, length: int, offset: int, p1: bool = False) -> bytes:
        return self.files[d.path.encode()][offset : offset + length]


def test_a_state_can_be_given_other_files(tmp_path: Path) -> None:
    """`State(filesystem=...)`: the files behind 0.1 and 0.2 are the object's (the Python-only feature that took the place of `files=`)."""
    state = State(preopens=[("/memory", "/m")], filesystem=Memory())
    memory = bytearray(1024)
    g = Guest(state, memory)
    memory[100:109] = b"hello.txt"
    assert wasi_p1.path_open(g, 3, 1, 100, 9, 0, 0x1FFFFFFF, 0, 0, 200) == 0
    fd = int.from_bytes(memory[200:204], "little")
    memory[300:308] = (400).to_bytes(4, "little") + (64).to_bytes(4, "little")
    assert wasi_p1.fd_read(g, fd, 300, 1, 500) == 0
    assert bytes(memory[400 : 400 + int.from_bytes(memory[500:504], "little")]) == b"from memory\n"
    memory[100:104] = b"nope"
    assert wasi_p1.path_open(g, 3, 1, 100, 4, 0, 0x1FFFFFFF, 0, 0, 200) == E_NOENT


def test_a_host_without_files_has_no_preopens() -> None:
    state = State(preopens=[("/x", "/x")], filesystem=fs.Filesystem())
    with pytest.raises(PreopenError):
        open_descriptors(state)
    assert (
        wasi_p1.fd_prestat_get(Guest(State(filesystem=fs.Filesystem()), bytearray(64)), 3, 0) == 8
    )  # BADF: no directory
