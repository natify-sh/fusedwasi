"""The surface of the C and of the Python twins that a host touches is the same: `user`, the `use` of the policy of sockets, `terminals`, the preopen, the constants of 0.1.

Each test builds a small C program (skipped without a C compiler; not run on Windows, where the libraries to link differ and no runner was available to try) and compares it with the Python.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p1.wasi_p1_core import State
from fusedwasi.p2 import wasi_p2_cli as cli
from fusedwasi.p2 import wasi_p2_core as core
from fusedwasi.p2 import wasi_p2_sockets as sk
from fusedwasi.p2.wasi_p2_core import Host

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
C_P1 = C_P2.parent / "p1"
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="the C programs are built and run on POSIX only")


def build_and_run(tmp_path: Path, source: str, name: str = "prog") -> str:
    if CC is None:
        pytest.skip("no C compiler")
    path = tmp_path / f"{name}.c"
    path.write_text(source)
    binary = tmp_path / name
    subprocess.run(
        [
            CC,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Werror",
            "-I",
            str(C_P2),
            "-I",
            str(C_P1),
            str(path),
            "-o",
            str(binary),
        ],
        check=True,
    )
    return subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout


# ---- user ----------------------------------------------------------------------------------------------------------------------


def test_user_is_the_hosts_and_untouched_in_python() -> None:
    marker = object()
    host = Host([bytearray(64)], args=["a"], user=marker)
    assert host.user is marker
    assert Host().user is None
    state = State(bytearray(64), core=host, user=marker)
    assert state.user is marker
    assert wasi_p1.args_sizes_get(state, 0, 4) == 0
    assert state.user is marker and host.user is marker


def test_user_is_the_hosts_and_untouched_in_c(tmp_path: Path) -> None:
    out = build_and_run(
        tmp_path,
        """#include <stdio.h>
#include "wasi_p1_core.h"
int main(void) {
    static uint8_t memory[256];
    uint8_t *base = memory;
    uint64_t size = sizeof memory;
    int marker_a = 1, marker_b = 2, err = 0;
    size_t bad;
    fusedwasi_p2_state s;
    fusedwasi_p2_init(&s);
    printf("init %d\\n", s.user == NULL);
    s.user = &marker_a;
    fusedwasi_p1_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = &marker_b;
    fusedwasi_p1_state *w = fusedwasi_p1_create(&cfg, &s, &err, &bad);
    fusedwasi_p1_bind(w, &base, &size);
    printf("create %d %d\\n", w->user == &marker_b, s.user == &marker_a);
    fusedwasi_p1_args_sizes_get(w, 0, 8, &err);
    printf("call %d %d\\n", w->user == &marker_b, s.user == &marker_a);
    fusedwasi_p1_destroy(w);
    fusedwasi_p2_free(&s);
    printf("free %d\\n", s.user == &marker_a);
    return 0;
}
""",
    )
    assert out.split("\n")[:5] == ["init 1", "create 1 1", "call 1 1", "free 1", ""]


# ---- use -----------------------------------------------------------------------------------------------------------------------


NAMES = ["tcp-bind", "tcp-connect", "udp-bind", "udp-connect", "udp-outgoing"]


def test_use_numbers_and_names_are_the_same_in_c_and_python(tmp_path: Path) -> None:
    out = build_and_run(
        tmp_path,
        """#include <stdio.h>
#include "wasi_p2_core.h"
int main(void) {
    printf("%d %d %d %d %d\\n", FUSEDWASI_P2_USE_TCP_BIND, FUSEDWASI_P2_USE_TCP_CONNECT, FUSEDWASI_P2_USE_UDP_BIND, FUSEDWASI_P2_USE_UDP_CONNECT, FUSEDWASI_P2_USE_UDP_OUTGOING);
    for (int i = -1; i < 7; i++) printf("%s\\n", fusedwasi_p2_use_name(i));
    return 0;
}
""",
    ).splitlines()
    numbers = (core.USE_TCP_BIND, core.USE_TCP_CONNECT, core.USE_UDP_BIND, core.USE_UDP_CONNECT, core.USE_UDP_OUTGOING)
    assert out[0] == " ".join(map(str, numbers)) and numbers == (0, 1, 2, 3, 4)
    assert out[1:] == [core.use_name(i) for i in range(-1, 7)]
    assert [core.use_name(i) for i in range(5)] == NAMES and core.use_name(-1) == core.use_name(5) == "unknown"


def test_the_policy_of_sockets_is_asked_with_the_number_in_python() -> None:
    asked: list[tuple[str, int, int]] = []
    memory = bytearray(512)
    host = Host([memory], socket_addr_check=lambda ip, port, use: asked.append((ip, port, use)) or False)
    net = sk.wasi_sockets_instance_network_instance_network(host)
    sk.wasi_sockets_tcp_create_socket_create_tcp_socket(host, 0, 0, 100)
    tcp = int.from_bytes(memory[104:108], "little")
    sk.wasi_sockets_tcp_tcp_socket_start_bind(host, 0, tcp, net, 0, 0, 127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 120)
    assert asked == [("127.0.0.1", 0, core.USE_TCP_BIND)] and isinstance(asked[0][2], int)
    assert memory[120] == 1 and sk.NETWORK_ERRORS[memory[121]] == "access-denied"


# ---- terminals -----------------------------------------------------------------------------------------------------------------


def test_terminals_are_a_mask_in_c_and_python(tmp_path: Path) -> None:
    assert (core.TERMINAL_STDIN, core.TERMINAL_STDOUT, core.TERMINAL_STDERR) == (1, 2, 4)
    out = build_and_run(
        tmp_path,
        """#include <stdio.h>
#include "wasi_p2_cli.h"
int main(void) {
    static uint8_t memory[256];
    printf("%d %d %d\\n", FUSEDWASI_P2_TERMINAL_STDIN, FUSEDWASI_P2_TERMINAL_STDOUT, FUSEDWASI_P2_TERMINAL_STDERR);
    for (unsigned mask = 0; mask < 8; mask++) {
        fusedwasi_p2_state s;
        fusedwasi_p2_init(&s);
        fusedwasi_p2_set_memory(&s, 0, memory, sizeof memory);
        s.terminals = mask;
        memset(memory, 0xFF, sizeof memory);
        fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(&s, 0, 0);
        fusedwasi_wasi_cli_terminal_stdout_get_terminal_stdout(&s, 0, 16);
        fusedwasi_wasi_cli_terminal_stderr_get_terminal_stderr(&s, 0, 32);
        printf("%u %d %d %d\\n", mask, memory[0], memory[16], memory[32]);
        fusedwasi_p2_free(&s);
    }
    return 0;
}
""",
    ).splitlines()
    assert out[0] == "1 2 4"
    for mask in range(8):
        memory = bytearray(256)
        host = Host([memory], terminals=mask)
        cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, 0, 0)
        cli.wasi_cli_terminal_stdout_get_terminal_stdout(host, 0, 16)
        cli.wasi_cli_terminal_stderr_get_terminal_stderr(host, 0, 32)
        assert out[1 + mask] == f"{mask} {memory[0]} {memory[16]} {memory[32]}"
        assert (memory[0], memory[16], memory[32]) == (mask & 1, mask >> 1 & 1, mask >> 2 & 1)


# ---- the preopen ---------------------------------------------------------------------------------------------------------------


def test_the_preopen_of_0_1_is_the_preopen_of_0_2(tmp_path: Path) -> None:
    out = build_and_run(
        tmp_path,
        """#include <stdio.h>
#include "wasi_p1_core.h"
int main(void) {
    fusedwasi_p1_preopen p = {"/real/path", "/seen", 0};              /* path, name, writable: the order of 0.2 */
    struct fusedwasi_p2_preopen *same = &p;                           /* the same type */
    printf("%s %s %d %d\\n", same->path, same->name, same->writable, (int)(sizeof(fusedwasi_p1_preopen) == sizeof(struct fusedwasi_p2_preopen)));
    return 0;
}
""",
    )
    assert out == "/real/path /seen 0 1\n"


def test_the_preopens_of_python_are_path_name_writable_in_both_states(tmp_path: Path) -> None:
    (tmp_path / "d").mkdir()
    items = [(str(tmp_path / "d"), "/seen", False)]
    host = Host(preopens=items)
    assert host.preopens == items
    state = State(bytearray(64), core=host, preopens=items)
    assert state.entries[3].preopen == b"/seen"
    assert not state.entries[3].rights & wasi_p1_core.R_FD_WRITE  # writable False: the right to write is taken away


# ---- the constants of 0.1 ------------------------------------------------------------------------------------------------------


def test_every_constant_of_0_1_in_python_is_in_c_with_the_same_value(tmp_path: Path) -> None:
    pattern = re.compile(r"^(E|FT|R|O|T|FD|K|END)_[A-Z0-9_]+$|^(LOOKUP_FOLLOW|RIGHTS_STDIN|RIGHTS_STDOUT)$")
    names = sorted(n for n in vars(wasi_p1_core) if pattern.match(n))
    assert len(names) > 100
    body = "".join(f'    printf("{n} %llu\\n", (unsigned long long)FUSEDWASI_P1_{n});\n' for n in names)
    out = build_and_run(
        tmp_path, '#include <stdio.h>\n#include "wasi_p1_core.h"\nint main(void) {\n' + body + "    return 0;\n}\n"
    )
    c_values = {line.split()[0]: int(line.split()[1]) for line in out.splitlines()}
    for n in names:
        assert c_values[n] == getattr(wasi_p1_core, n), n


# ---- the memory: bound by pointer (primary) or set by value ----------------------------------------------------------------------

BIND_C = """#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include "wasi_p2_cli.h"
static jmp_buf jb;
static const char *last;
static void on_trap(fusedwasi_p2_state *s, const char *m) { (void)s; last = m; longjmp(jb, 1); }
static void terminal(fusedwasi_p2_state *s, uint8_t *base, uint32_t at, const char *label) {
    last = "none";
    if (setjmp(jb) == 0) {
        fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(s, 0, at);
        printf("%s ok %d\\n", label, base ? base[at] : -1);
    } else printf("%s trap %s\\n", label, last);
}
int main(void) {
    fusedwasi_p2_state s;
    fusedwasi_p2_init(&s);
    s.trap = on_trap;
    s.terminals = FUSEDWASI_P2_TERMINAL_STDIN;
    printf("bind %d %d\\n", fusedwasi_p2_bind_memory(&s, 0, NULL, NULL), fusedwasi_p2_bind_memory(&s, FUSEDWASI_P2_MAX_MEMORIES, NULL, NULL));
    terminal(&s, NULL, 0, "null");
    /* the cells are the consumer's: bind once, then realloc the cell */
    uint8_t *data = calloc(16, 1);
    uint64_t size = 16;
    printf("bind %d\\n", fusedwasi_p2_bind_memory(&s, 0, &data, &size));
    terminal(&s, data, 8, "first");
    terminal(&s, data, 40, "outside");
    uint8_t *moved = calloc(64, 1);
    free(data);
    data = moved, size = 64;
    terminal(&s, data, 40, "grown");
    /* memory 2 alone: 1 was never bound */
    printf("bind %d\\n", fusedwasi_p2_bind_memory(&s, 2, &data, &size));
    last = "none";
    if (setjmp(jb) == 0) fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(&s, 1, 0);
    printf("unbound %s\\n", last);
    /* set_memory keeps its meaning, and takes the number over from the binding */
    static uint8_t fixed[32];
    printf("set %d\\n", fusedwasi_p2_set_memory(&s, 0, fixed, sizeof fixed));
    terminal(&s, fixed, 20, "set");
    terminal(&s, fixed, 40, "set-outside");
    data[0] = 7;
    printf("untouched %d\\n", fixed[0]);
    free(data);
    fusedwasi_p2_free(&s);
    return 0;
}
"""


def test_bind_memory_follows_a_moved_base_in_c_and_python(tmp_path: Path) -> None:
    out = build_and_run(tmp_path, BIND_C).splitlines()
    assert out == [
        "bind 0 -1",
        "null trap no such memory",
        "bind 0",
        "first ok 1",
        "outside trap pointer outside the memory",
        "grown ok 1",
        "bind 0",
        "unbound no such memory",
        "set 0",
        "set ok 1",
        "set-outside trap pointer outside the memory",
        "untouched 0",
    ]
    # the same, in Python: a function that gives the buffer, and the buffer is replaced
    cell = [bytearray(16)]
    host = Host([lambda: cell[0]], terminals=core.TERMINAL_STDIN)
    cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, 0, 8)
    assert cell[0][8] == 1
    with pytest.raises(core.Trap, match="outside the memory"):
        cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, 0, 40)
    cell[0] = bytearray(64)
    cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, 0, 40)
    assert cell[0][40] == 1
    with pytest.raises(core.Trap, match="no memory"):
        cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, 1, 0)
    # buffers and functions mix, and a buffer still works as before
    plain = bytearray(32)
    mixed = Host([lambda: cell[0], plain], terminals=core.TERMINAL_STDIN)
    cli.wasi_cli_terminal_stdin_get_terminal_stdin(mixed, 1, 20)
    assert plain[20] == 1
    with pytest.raises(core.Trap, match="outside the memory"):
        cli.wasi_cli_terminal_stdin_get_terminal_stdin(mixed, 1, 40)


def test_c_and_python_see_a_moved_memory_the_same_way(tmp_path: Path) -> None:
    """The same steps in both: the outcome of each (`ok <byte>`, or a trap of the kind `outside` or `unbound`) is equal."""
    c_lines = build_and_run(tmp_path, BIND_C).splitlines()
    c_outcomes = {
        line.split()[0]: ("ok " + line.split()[2] if " ok " in line else "outside" if "outside" in line else "unbound")
        for line in c_lines
        if line.split()[0] in {"first", "outside", "grown", "unbound"}
    }

    def step(host: Host, comp: int, at: int) -> str:
        try:
            cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, comp, at)
        except core.Trap as trap:
            return "outside" if "outside" in str(trap) else "unbound"
        return "ok 1"

    cell = [bytearray(16)]
    host = Host([lambda: cell[0]], terminals=core.TERMINAL_STDIN)
    first = step(host, 0, 8)
    outside = step(host, 0, 40)
    cell[0] = bytearray(64)  # what C does with realloc of the cell: no rebind
    grown = step(host, 0, 40)
    unbound = step(host, 1, 0)
    assert {"first": first, "outside": outside, "grown": grown, "unbound": unbound} == c_outcomes
