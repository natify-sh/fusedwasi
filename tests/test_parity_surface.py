"""The surface of the C and of the Python twins that a host touches is the same: `user` (of the state and of the guest), the `use` of the policy of sockets, `terminals`, the preopen, the constants of 0.1,
the memory of a guest, the hooks of a guest (`trap`, `exit`, `signal`) and several guests over one state.

Each test builds a small C program (skipped without a C compiler; not run on Windows, where the libraries to link differ and no runner was available to try) and compares it with the Python.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from cbuild import build

from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p1.wasi_p1_core import Guest as P1Guest
from fusedwasi.p1.wasi_p1_core import State as P1State
from fusedwasi.p2 import wasi_p2_cli as cli
from fusedwasi.p2 import wasi_p2_core as core
from fusedwasi.p2 import wasi_p2_io as io
from fusedwasi.p2 import wasi_p2_sockets as sk
from fusedwasi.p2.wasi_p2_core import Guest, State

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
            str(path),
            "-o",
            str(binary),
        ],
    )
    return subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout


# ---- user ----------------------------------------------------------------------------------------------------------------------


def test_user_is_the_makers_and_untouched_in_python() -> None:
    """Two `user`s: the state's (whoever made the state, given back to the hooks of the world) and the guest's (whoever made the guest, given back to the hooks of the guest)."""
    world_marker, guest_marker = object(), object()
    state = State(args=["a"], user=world_marker)
    guest = Guest(state, [bytearray(64)], user=guest_marker)
    assert state.user is world_marker and guest.user is guest_marker
    assert State().user is None and Guest(State()).user is None
    p1_state = P1State(core=state, user=world_marker)
    p1_guest = P1Guest(p1_state, bytearray(64), user=guest_marker)
    assert wasi_p1.args_sizes_get(p1_guest, 0, 4) == 0
    assert p1_state.user is world_marker and p1_guest.user is guest_marker
    assert state.user is world_marker and guest.user is guest_marker


def test_user_is_the_makers_and_untouched_in_c(tmp_path: Path) -> None:
    out = build_and_run(
        tmp_path,
        """#include <stdio.h>
#include "wasi_p1_core.h"
int main(void) {
    static uint8_t memory[256];
    uint8_t *base = memory;
    uint64_t size = sizeof memory;
    int marker_a = 1, marker_b = 2, marker_c = 3, err = 0;
    size_t bad;
    fusedwasi_p2_state s;
    fusedwasi_p2_init(&s);
    printf("init %d\\n", s.user == NULL);
    s.user = &marker_a;
    fusedwasi_p1_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = &marker_b;
    fusedwasi_p1_state *w = fusedwasi_p1_create(&cfg, &s, &err, &bad);
    fusedwasi_p1_guest g;
    fusedwasi_p1_guest_init(&g, w);
    printf("guest init %d\\n", g.core.user == NULL);
    g.core.user = &marker_c;
    fusedwasi_p1_bind(&g, &base, &size);
    printf("create %d %d %d\\n", w->user == &marker_b, s.user == &marker_a, g.core.user == &marker_c);
    fusedwasi_p1_args_sizes_get(&g, 0, 8, &err);
    printf("call %d %d %d\\n", w->user == &marker_b, s.user == &marker_a, g.core.user == &marker_c);
    fusedwasi_p1_destroy(w);
    fusedwasi_p2_free(&s);
    printf("free %d\\n", s.user == &marker_a);
    return 0;
}
""",
    )
    assert out.split("\n")[:6] == ["init 1", "guest init 1", "create 1 1 1", "call 1 1 1", "free 1", ""]


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
    guest = Guest(State(socket_addr_check=lambda ip, port, use: asked.append((ip, port, use)) or False), [memory])
    net = sk.wasi_sockets_instance_network_instance_network(guest)
    sk.wasi_sockets_tcp_create_socket_create_tcp_socket(guest, 0, 0, 100)
    tcp = int.from_bytes(memory[104:108], "little")
    sk.wasi_sockets_tcp_tcp_socket_start_bind(guest, 0, tcp, net, 0, 0, 127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 120)
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
        fusedwasi_p2_state world;
        fusedwasi_p2_guest g;
        fusedwasi_p2_init(&world);
        fusedwasi_p2_guest_init(&g, &world);
        fusedwasi_p2_set_memory(&g, 0, memory, sizeof memory);
        world.terminals = mask;
        memset(memory, 0xFF, sizeof memory);
        fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(&g, 0, 0);
        fusedwasi_wasi_cli_terminal_stdout_get_terminal_stdout(&g, 0, 16);
        fusedwasi_wasi_cli_terminal_stderr_get_terminal_stderr(&g, 0, 32);
        printf("%u %d %d %d\\n", mask, memory[0], memory[16], memory[32]);
        fusedwasi_p2_free(&world);
    }
    return 0;
}
""",
    ).splitlines()
    assert out[0] == "1 2 4"
    for mask in range(8):
        memory = bytearray(256)
        guest = Guest(State(terminals=mask), [memory])
        cli.wasi_cli_terminal_stdin_get_terminal_stdin(guest, 0, 0)
        cli.wasi_cli_terminal_stdout_get_terminal_stdout(guest, 0, 16)
        cli.wasi_cli_terminal_stderr_get_terminal_stderr(guest, 0, 32)
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
    world = State(preopens=items)
    assert world.preopens == items
    state = P1State(core=world, preopens=items)
    assert state.entries[3].preopen == b"/seen"
    assert not state.entries[3].rights & wasi_p1_core.R_FD_WRITE  # writable False: the right to write is taken away


# ---- the constants of 0.1 ------------------------------------------------------------------------------------------------------


def test_every_constant_of_0_1_in_python_is_in_c_with_the_same_value(tmp_path: Path) -> None:
    pattern = re.compile(r"^(E|FT|R|O|T|FD|K)_[A-Z0-9_]+$|^(LOOKUP_FOLLOW|RIGHTS_STDIN|RIGHTS_STDOUT)$")
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
static void on_trap(fusedwasi_p2_guest *g, const char *m) { (void)g; last = m; longjmp(jb, 1); }
static void terminal(fusedwasi_p2_guest *s, uint8_t *base, uint32_t at, const char *label) {
    last = "none";
    if (setjmp(jb) == 0) {
        fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(s, 0, at);
        printf("%s ok %d\\n", label, base ? base[at] : -1);
    } else printf("%s trap %s\\n", label, last);
}
int main(void) {
    fusedwasi_p2_state world;
    fusedwasi_p2_guest s;
    fusedwasi_p2_init(&world);
    fusedwasi_p2_guest_init(&s, &world);
    s.trap = on_trap;
    world.terminals = FUSEDWASI_P2_TERMINAL_STDIN;
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
    fusedwasi_p2_free(&world);
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
    host = Guest(State(terminals=core.TERMINAL_STDIN), [lambda: cell[0]])
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
    mixed = Guest(State(terminals=core.TERMINAL_STDIN), [lambda: cell[0], plain])
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

    def step(host: Guest, comp: int, at: int) -> str:
        try:
            cli.wasi_cli_terminal_stdin_get_terminal_stdin(host, comp, at)
        except core.Trap as trap:
            return "outside" if "outside" in str(trap) else "unbound"
        return "ok 1"

    cell = [bytearray(16)]
    host = Guest(State(terminals=core.TERMINAL_STDIN), [lambda: cell[0]])
    first = step(host, 0, 8)
    outside = step(host, 0, 40)
    cell[0] = bytearray(64)  # what C does with realloc of the cell: no rebind
    grown = step(host, 0, 40)
    unbound = step(host, 1, 0)
    assert {"first": first, "outside": outside, "grown": grown, "unbound": unbound} == c_outcomes


# ---- several guests over one state -----------------------------------------------------------------------------------------------

TWO_GUESTS_P2_C = """#include <stdio.h>
#include <setjmp.h>
#include "wasi_p2_cli.h"
static jmp_buf jb;
static const char *last;
static void on_trap(fusedwasi_p2_guest *g, const char *m) { (void)g; last = m; longjmp(jb, 1); }
static void hook_exit(fusedwasi_p2_guest *g, uint32_t code) { printf("hook exit %d %u\\n", *(int *)g->user, code); g->exited = 1; g->exit_code = code; }
int main(void) {
    static uint8_t m1[32], m2[32];
    int id1 = 1, id2 = 2;
    fusedwasi_p2_state world;
    fusedwasi_p2_guest g1, g2;
    fusedwasi_p2_init(&world);
    world.terminals = FUSEDWASI_P2_TERMINAL_STDIN | FUSEDWASI_P2_TERMINAL_STDOUT;
    fusedwasi_p2_guest_init(&g1, &world);
    fusedwasi_p2_guest_init(&g2, &world);
    g1.trap = g2.trap = on_trap;
    g1.user = &id1, g2.user = &id2;
    g2.exit = hook_exit;
    fusedwasi_p2_set_memory(&g1, 0, m1, sizeof m1);
    fusedwasi_p2_set_memory(&g2, 0, m2, sizeof m2);
    printf("same state %d\\n", g1.state == g2.state && g1.state == &world);
    uint32_t h = fusedwasi_wasi_cli_stdout_get_stdout(&g1);
    printf("handle %u\\n", h);
    fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(&g1, 0, 0);
    fusedwasi_wasi_cli_terminal_stdout_get_terminal_stdout(&g2, 0, 8);
    printf("memories %d %d %d %d\\n", m1[0], m1[8], m2[0], m2[8]);
    fusedwasi_wasi_io_streams_output_stream_drop(&g2, h);
    printf("dropped by the other guest\\n");
    if (setjmp(jb) == 0) fusedwasi_wasi_io_streams_output_stream_drop(&g1, h);
    else printf("trap %.10s\\n", last);
    if (setjmp(jb) == 0) fusedwasi_wasi_cli_exit_exit_with_code(&g1, 7);
    else printf("trap %s\\n", last);
    printf("g1 %d %u g2 %d %u\\n", g1.exited, (unsigned)g1.exit_code, g2.exited, (unsigned)g2.exit_code);
    fusedwasi_wasi_cli_exit_exit_with_code(&g2, 0x1ff);
    printf("g1 %d %u g2 %d %u\\n", g1.exited, (unsigned)g1.exit_code, g2.exited, (unsigned)g2.exit_code);
    fusedwasi_p2_free(&world);
    return 0;
}
"""


def two_guests_p2_python() -> list[str]:
    lines: list[str] = []
    m1, m2 = bytearray(32), bytearray(32)
    last: list[str] = []
    world = State(terminals=core.TERMINAL_STDIN | core.TERMINAL_STDOUT)
    g1 = Guest(world, [m1], trap=last.append, user=1)
    g2 = Guest(world, [m2], trap=last.append, user=2)

    def hook_exit(code: int) -> None:
        lines.append(f"hook exit {g2.user} {code}")
        g2.exited, g2.exit_code = True, code

    g2.exit = hook_exit
    lines.append(f"same state {int(g1.state is g2.state is world)}")
    h = cli.wasi_cli_stdout_get_stdout(g1)
    lines.append(f"handle {h}")
    cli.wasi_cli_terminal_stdin_get_terminal_stdin(g1, 0, 0)
    cli.wasi_cli_terminal_stdout_get_terminal_stdout(g2, 0, 8)
    lines.append(f"memories {m1[0]} {m1[8]} {m2[0]} {m2[8]}")
    io.wasi_io_streams_output_stream_drop(g2, h)
    lines.append("dropped by the other guest")
    try:
        io.wasi_io_streams_output_stream_drop(g1, h)
    except core.Trap:
        lines.append(f"trap {last[-1][:10]}")
    try:
        cli.wasi_cli_exit_exit_with_code(g1, 7)
    except core.Exit:
        lines.append("trap exit")
    lines.append(f"g1 {int(g1.exited)} {g1.exit_code} g2 {int(g2.exited)} {g2.exit_code}")
    cli.wasi_cli_exit_exit_with_code(g2, 0x1FF)
    lines.append(f"g1 {int(g1.exited)} {g1.exit_code} g2 {int(g2.exited)} {g2.exit_code}")
    return lines


def test_two_guests_over_one_state_in_c_and_python(tmp_path: Path) -> None:
    """Separate memories, one table of handles, and the end of one guest's call is not the other's."""
    c_lines = build_and_run(tmp_path, TWO_GUESTS_P2_C).splitlines()
    assert c_lines == two_guests_p2_python()
    assert c_lines == [
        "same state 1",
        "handle 1048577",
        "memories 1 0 0 1",  # each guest wrote its own memory
        "dropped by the other guest",  # a handle of one guest is a handle of the state
        "trap bad handle",  # ... and it is spent for both
        "trap exit",
        "g1 1 7 g2 0 0",  # the exit of g1 left g2 alone
        "hook exit 2 255",  # the hook of g2 replaces the default
        "g1 1 7 g2 1 255",
    ]


TWO_GUESTS_P1_C = """#include <stdio.h>
#include "wasi_p1.h"
static uint32_t on_signal(fusedwasi_p1_guest *g, uint32_t number) { printf("signal %u of guest %d\\n", (unsigned)number, *(int *)g->core.user); return 0; }
int main(void) {
    static uint8_t m1[64], m2[64];
    uint8_t *b1 = m1, *b2 = m2;
    uint64_t s1 = sizeof m1, s2 = sizeof m2;
    int id1 = 1, id2 = 2, err = 0, trapped = 0;
    size_t bad;
    static const char *const args[] = {"prog", "x"};
    fusedwasi_p2_state world;
    fusedwasi_p2_init(&world);
    world.args = args, world.arg_count = 2;
    fusedwasi_p1_config cfg;
    memset(&cfg, 0, sizeof cfg);
    fusedwasi_p1_state *state = fusedwasi_p1_create(&cfg, &world, &err, &bad);
    fusedwasi_p1_guest g1, g2;
    fusedwasi_p1_guest_init(&g1, state);
    fusedwasi_p1_guest_init(&g2, state);
    g1.core.user = &id1, g2.core.user = &id2;
    fusedwasi_p1_bind(&g1, &b1, &s1);
    fusedwasi_p1_bind(&g2, &b2, &s2);
    g1.signal = on_signal;
    printf("same state %d\\n", g1.state == g2.state && g1.state == state);
    printf("args_sizes_get %u\\n", (unsigned)fusedwasi_p1_args_sizes_get(&g1, 0, 4, &trapped));
    printf("memories %d %d\\n", m1[0], m2[0]);
    printf("fd_close %u\\n", (unsigned)fusedwasi_p1_fd_close(&g1, 2, &trapped));
    printf("other guest, closed fd %u\\n", (unsigned)fusedwasi_p1_fd_fdstat_get(&g2, 2, 16, &trapped));
    printf("other guest, open fd %u\\n", (unsigned)fusedwasi_p1_fd_fdstat_get(&g2, 1, 16, &trapped));
    printf("raise %u\\n", (unsigned)fusedwasi_p1_proc_raise(&g1, 15, &trapped));
    printf("raise %u\\n", (unsigned)fusedwasi_p1_proc_raise(&g2, 15, &trapped));
    fusedwasi_p1_proc_exit(&g1, 5, &trapped);
    printf("g1 %d %u trapped %d g2 %d %u\\n", g1.core.exited, (unsigned)g1.core.exit_code, trapped, g2.core.exited, (unsigned)g2.core.exit_code);
    trapped = 0;
    fusedwasi_p1_proc_exit(&g2, 9, &trapped);
    printf("g1 %d %u g2 %d %u\\n", g1.core.exited, (unsigned)g1.core.exit_code, g2.core.exited, (unsigned)g2.core.exit_code);
    fusedwasi_p1_destroy(state);
    fusedwasi_p2_free(&world);
    return 0;
}
"""


def two_guests_p1_python() -> list[str]:
    lines: list[str] = []
    m1, m2 = bytearray(64), bytearray(64)
    state = P1State(core=State(args=["prog", "x"]))
    g1 = P1Guest(state, m1, user=1)
    g2 = P1Guest(state, m2, user=2)

    def on_signal(number: int) -> int:
        lines.append(f"signal {number} of guest {g1.user}")
        return 0

    g1.signal = on_signal
    lines.append(f"same state {int(g1.state is g2.state is state)}")
    lines.append(f"args_sizes_get {wasi_p1.args_sizes_get(g1, 0, 4)}")
    lines.append(f"memories {m1[0]} {m2[0]}")
    lines.append(f"fd_close {wasi_p1.fd_close(g1, 2)}")
    lines.append(f"other guest, closed fd {wasi_p1.fd_fdstat_get(g2, 2, 16)}")
    lines.append(f"other guest, open fd {wasi_p1.fd_fdstat_get(g2, 1, 16)}")
    lines.append(f"raise {wasi_p1.proc_raise(g1, 15)}")
    lines.append(f"raise {wasi_p1.proc_raise(g2, 15)}")
    with pytest.raises(wasi_p1_core.Exit):
        wasi_p1.proc_exit(g1, 5)
    lines.append(f"g1 {int(g1.exited)} {g1.exit_code} trapped 1 g2 {int(g2.exited)} {g2.exit_code}")
    with pytest.raises(wasi_p1_core.Exit):
        wasi_p1.proc_exit(g2, 9)
    lines.append(f"g1 {int(g1.exited)} {g1.exit_code} g2 {int(g2.exited)} {g2.exit_code}")
    state.close_all()
    return lines


def test_two_guests_of_0_1_over_one_state_in_c_and_python(tmp_path: Path) -> None:
    """Separate memories, one table of descriptors, a signal hook and an exit for each guest."""
    c_lines = build_and_run(tmp_path, TWO_GUESTS_P1_C).splitlines()
    assert c_lines == two_guests_p1_python()
    assert c_lines == [
        "same state 1",
        "args_sizes_get 0",
        "memories 2 0",  # two arguments written to the memory of g1; g2 has its own
        "fd_close 0",
        "other guest, closed fd 8",  # BADF: the descriptor is closed for both
        "other guest, open fd 0",
        "signal 15 of guest 1",
        "raise 0",  # the hook of g1 answers
        "raise 58",  # g2 has none: NOTSUP
        "g1 1 5 trapped 1 g2 0 0",
        "g1 1 5 g2 1 9",
    ]


def test_the_hooks_of_the_guest_in_python() -> None:
    """`trap` is told before `Trap` is raised (and may raise its own); `exit` replaces the default; `signal` is asked with the number, and 0 and 31 never reach it."""
    told: list[str] = []
    guest = Guest(State(), [bytearray(16)], trap=told.append)
    with pytest.raises(core.Trap, match="outside"):
        core.store_u32(guest, 0, 100, 1)
    assert len(told) == 1 and "outside" in told[0]

    class Mine(Exception):
        pass

    def own(message: str) -> None:
        raise Mine(message)

    with pytest.raises(Mine):
        core.store_u32(Guest(State(), [bytearray(16)], trap=own), 0, 100, 1)
    plain = Guest(State(), [bytearray(16)])
    with pytest.raises(core.Exit) as caught:
        cli.wasi_cli_exit_exit(plain, 1)
    assert caught.value.code == 1 and plain.exited and plain.exit_code == 1
    asked: list[int] = []
    p1 = P1Guest(P1State(), bytearray(16), signal=lambda number: asked.append(number) or wasi_p1_core.E_PERM)
    assert [wasi_p1.proc_raise(p1, n) for n in (0, 9, 31)] == [0, wasi_p1_core.E_PERM, wasi_p1_core.E_INVAL]
    assert asked == [9]
    assert wasi_p1.proc_raise(P1Guest(P1State(), bytearray(16)), 9) == wasi_p1_core.E_NOTSUP


# ---- the objects of the world need no guest --------------------------------------------------------------------------------------

BARE_STATE_C = """#include <stdio.h>
#include "wasi_p2_cli.h"
static void on_trap(fusedwasi_p2_guest *g, const char *m) { (void)g; printf("TRAP %s\\n", m); exit(0); }
static uint32_t at_44(fusedwasi_p2_guest *g, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size) { (void)g, (void)comp, (void)old_ptr, (void)old_size, (void)align, (void)new_size; return 44; }
int main(void) {
    static uint8_t memory[64];
    fusedwasi_p2_state world;
    fusedwasi_p2_init(&world);
    /* no guest yet: the streams are made and given to the state while it is configured */
    fusedwasi_p2_memory_input mi; /* the owner of a stream has its pointer; the public *_add puts it in the table of the state */
    memset(&mi, 0, sizeof mi);
    mi.base.read = fusedwasi_p2_memory_input_read, mi.base.ready = fusedwasi_p2_always_ready;
    mi.data = (const uint8_t *)"in!", mi.len = 3;
    uint32_t hin = fusedwasi_p2_input_stream_add(&world, &mi.base), hout;
    fusedwasi_p2_buffer_output *bo = fusedwasi_p2_buffer_output_new(&world, 100, &hout);
    world.std[0] = &mi.base;
    world.std[1] = &bo->base;
    fusedwasi_p2_fd_stream *fd = fusedwasi_p2_fd_output_new(&world, 2);
    world.std[2] = &fd->base;
    printf("made %d %d %d\\n", hin != 0, hout != 0, bo != NULL && fd != NULL);
    fusedwasi_p2_guest g;
    fusedwasi_p2_guest_init(&g, &world);
    g.trap = on_trap;
    g.realloc = at_44;
    fusedwasi_p2_set_memory(&g, 0, memory, sizeof memory);
    memcpy(memory, "hi", 2);
    uint32_t in = fusedwasi_wasi_cli_stdin_get_stdin(&g), out = fusedwasi_wasi_cli_stdout_get_stdout(&g);
    fusedwasi_wasi_io_streams_output_stream_write(&g, 0, out, 0, 2, 16);
    fusedwasi_wasi_io_streams_input_stream_read(&g, 0, in, 10, 32);
    printf("tag %d len %u data %.3s\\n", memory[32], (unsigned)fusedwasi_p2_load_u32(&g, 0, 40), (const char *)memory + 44);
    printf("stdout %.*s\\n", (int)bo->len, bo->buf);
    fusedwasi_p2_free(&world);
    fusedwasi_p2_buffer_output_release(bo);
    free(fd);
    return 0;
}
"""


def test_streams_are_made_and_given_to_a_bare_state_with_no_guest(tmp_path: Path) -> None:
    out = build_and_run(tmp_path, BARE_STATE_C).splitlines()
    assert out[0] == "made 1 1 1"
    stdin, stdout = io.MemoryInput(b"in!"), io.BufferOutput(100)
    state = State(stdin=stdin, stdout=stdout)  # no guest: only the world
    assert io.input_stream_add(state, io.MemoryInput(b"x")) != 0 and io.output_stream_add(state, io.BufferOutput()) != 0
    memory = bytearray(64)
    memory[0:2] = b"hi"
    guest = Guest(state, [memory], realloc=lambda comp, old_ptr, old_size, align, size: 44)
    h_in, h_out = cli.wasi_cli_stdin_get_stdin(guest), cli.wasi_cli_stdout_get_stdout(guest)
    io.wasi_io_streams_output_stream_write(guest, 0, h_out, 0, 2, 16)
    io.wasi_io_streams_input_stream_read(guest, 0, h_in, 10, 32)
    assert out[2] == f"stdout {bytes(stdout.data).decode()}" == "stdout hi"
    assert memory[16] == 0 and memory[32] == 0 and bytes(memory[44:47]) == b"in!"
    assert out[1] == "tag 0 len 3 data in!"
