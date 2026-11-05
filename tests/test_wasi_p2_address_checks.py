"""The checks that the host makes on an address before a bind or a connect, for IPv4 and for IPv6, in C and in Python (`docs/host_scope.md`, Known limits, "IPv6 sockets").

The rules: a bind needs a unicast address of the family of the socket (not multicast, not the IPv4 broadcast, not an IPv4-mapped IPv6 address; the unspecified address is allowed); a
connect needs the same, and an address that is not the unspecified one, and a port that is not 0; then the policy of the embedder (`socket_addr_check`) answers `access-denied`.

The checks are pure: no socket is made, so this runs on a machine with no IPv6, which the scenario `ipv6` of `test_wasi_p2_sockets.py` (a whole session) cannot. The C program
(`tests/data/wasi_p2_address_checks_driver.c`) and the Python twin are given the same table and must print the same lines, and the lines must be what the rules say.
"""

import subprocess
import sys
from pathlib import Path

import pytest
from cbuild import CC, compile_c
from guests import guest_of

from fusedwasi.p2 import wasi_p2_sockets as sk
from fusedwasi.p2.wasi_p2_core import USE_TCP_BIND, USE_TCP_CONNECT

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
DRIVER = Path(__file__).parent / "data" / "wasi_p2_address_checks_driver.c"

# (label, family of the socket, address); an address is (family, port, parts, flow, scope): four bytes for IPv4, eight 16-bit numbers for IPv6. The table of the C driver, in the same order.
V4 = tuple[int, int, tuple[int, ...], int, int]
Z4, Z6 = (0, 0, 0, 0), (0, 0, 0, 0, 0, 0, 0, 0)
LOOP6 = (0, 0, 0, 0, 0, 0, 0, 1)
ITEMS: list[tuple[str, int, V4]] = [
    ("v6 loopback", 1, (1, 80, LOOP6, 0, 0)),
    ("v6 unspecified", 1, (1, 80, Z6, 0, 0)),
    ("v6 unspecified, port 0", 1, (1, 0, Z6, 0, 0)),
    ("v6 loopback, port 0", 1, (1, 0, LOOP6, 0, 0)),
    ("v6 multicast", 1, (1, 80, (0xFF02, 0, 0, 0, 0, 0, 0, 1), 0, 0)),
    ("v6 ipv4-mapped", 1, (1, 80, (0, 0, 0, 0, 0, 0xFFFF, 0x7F00, 0x0001), 0, 0)),
    ("v6 documentation", 1, (1, 80, (0x2001, 0x0DB8, 0, 0, 0, 0, 0, 1), 0, 0)),
    ("v4 on a v6 socket", 1, (0, 80, (127, 0, 0, 1), 0, 0)),
    ("v6 on a v4 socket", 0, (1, 80, LOOP6, 0, 0)),
    ("v4 loopback", 0, (0, 80, (127, 0, 0, 1), 0, 0)),
    ("v4 unspecified", 0, (0, 80, Z4, 0, 0)),
    ("v4 broadcast", 0, (0, 80, (255, 255, 255, 255), 0, 0)),
    ("v4 multicast", 0, (0, 80, (224, 0, 0, 1), 0, 0)),
    ("v4 loopback, port 0", 0, (0, 0, (127, 0, 0, 1), 0, 0)),
    ("v6 loopback, refused by the policy", 1, (1, 9, LOOP6, 0, 0)),
    ("v4 loopback, refused by the policy", 0, (0, 9, (127, 0, 0, 1), 0, 0)),
]

# what the rules say, for each line: (bind, connect)
EXPECTED = {
    "v6 loopback": ("ok", "ok"),
    "v6 unspecified": ("ok", "invalid-argument"),  # the unspecified address may be bound, not connected to
    "v6 unspecified, port 0": ("ok", "invalid-argument"),
    "v6 loopback, port 0": ("ok", "invalid-argument"),  # port 0 is for a bind (the system chooses), not for a connect
    "v6 multicast": ("invalid-argument", "invalid-argument"),
    "v6 ipv4-mapped": ("invalid-argument", "invalid-argument"),
    "v6 documentation": ("ok", "ok"),
    "v4 on a v6 socket": ("invalid-argument", "invalid-argument"),
    "v6 on a v4 socket": ("invalid-argument", "invalid-argument"),
    "v4 loopback": ("ok", "ok"),
    "v4 unspecified": ("ok", "invalid-argument"),
    "v4 broadcast": ("invalid-argument", "invalid-argument"),
    "v4 multicast": ("invalid-argument", "invalid-argument"),
    "v4 loopback, port 0": ("ok", "invalid-argument"),
    "v6 loopback, refused by the policy": ("access-denied", "access-denied"),
    "v4 loopback, refused by the policy": ("access-denied", "access-denied"),
}


def python_lines() -> list[str]:
    guest = guest_of([bytearray(64)], socket_addr_check=lambda ip, port, use: port != 9)

    def result(check: object, family: int, address: V4, use: int) -> str:
        try:
            check(guest, family, address, use)  # pyright: ignore[reportCallIssue]
        except sk._NetError as refusal:  # pyright: ignore[reportPrivateUsage]
            return str(refusal)
        return "ok"

    return [
        f"{label}: bind={result(sk._check_bind, family, address, USE_TCP_BIND)} connect={result(sk._check_connect, family, address, USE_TCP_CONNECT)}"  # pyright: ignore[reportPrivateUsage]
        for label, family, address in ITEMS
    ]


def test_the_python_twin_follows_the_rules() -> None:
    lines = python_lines()
    assert lines == [f"{label}: bind={EXPECTED[label][0]} connect={EXPECTED[label][1]}" for label, _, _ in ITEMS]


def test_the_c_twin_gives_the_same_lines(tmp_path: Path) -> None:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path / ("driver.exe" if sys.platform == "win32" else "driver")
    compile_c(DRIVER, binary, [C_P2])
    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True, timeout=60).stdout
    assert out.splitlines() == python_lines()
