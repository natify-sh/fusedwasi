"""Tests that pin the known limits of `docs/host_scope.md` ("Known limits"): what a guest sees today. When one of them fails, a limit has changed: change the document with it, on purpose.

Nothing here is a feature; each test says what the host does where it does not do more.
"""

import importlib
import inspect
import os
import re
import socket
import subprocess
import sys
import threading
import time
from collections.abc import Iterator
from pathlib import Path

import pytest
from cbuild import CC, compile_c
from guests import guest_of

from fusedwasi.p2 import wasi_p2_http as H
from fusedwasi.p2 import wasi_p2_io as io
from fusedwasi.p2 import wasi_p2_sockets as sk
from fusedwasi.p2.wasi_p2_core import _store_u32 as store_u32  # pyright: ignore[reportPrivateUsage]

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
PACKAGES = ("cli", "clocks", "filesystem", "http", "io", "random", "sockets")


# ---- an import with no function ------------------------------------------------------------------------------------------------


def test_the_c_and_the_python_have_a_function_for_the_same_imports() -> None:
    """There is a function for every import of WASI 0.2 that the host knows, in both, and no catch-all: a name that is not here is not answered by the library (the
    generator of the module decides what that means: a generator of glue may refuse to build it)."""
    c_names: set[str] = set()
    for header in C_P2.glob("wasi_p2_*.h"):
        for m in re.finditer(
            r"^FUSEDWASI_P2_FN\s+[\w\s\*]+?\s+fusedwasi_(wasi_\w+)\s*\(", header.read_text(), re.MULTILINE
        ):
            c_names.add(m.group(1))
    py_names: set[str] = set()
    for package in PACKAGES:
        module = importlib.import_module(f"fusedwasi.p2.wasi_p2_{package}")
        assert not hasattr(module, "__getattr__")  # no function that answers any name
        py_names |= {
            k
            for k, v in vars(module).items()
            if k.startswith("wasi_") and inspect.isfunction(v) and v.__module__ == module.__name__
        }
    assert c_names == py_names
    assert len(c_names) == 201  # the 200 imports of host_scope.md and one more; a new one is added here on purpose


def test_wasi_p1_has_the_calls_of_the_snapshot_and_four_sockets_that_answer_notsock() -> None:
    from fusedwasi.p1 import wasi_p1
    from fusedwasi.p1.wasi_p1_core import E_BADF, E_NOTSOCK, Guest, State

    names = {
        k
        for k, v in vars(wasi_p1).items()
        if inspect.isfunction(v) and not k.startswith("_") and v.__module__ == wasi_p1.__name__
    }
    assert {"sock_accept", "sock_recv", "sock_send", "sock_shutdown", "proc_exit", "fd_write", "path_open"} <= names
    state = Guest(State(), bytearray(64))
    assert wasi_p1.sock_shutdown(state, 1, 0) == E_NOTSOCK  # a descriptor that exists is not a socket
    assert wasi_p1.sock_shutdown(state, 99, 0) == E_BADF


# ---- IPv6 ----------------------------------------------------------------------------------------------------------------------


def test_ipv6_sockets_follow_the_system() -> None:
    """The host makes the socket the system makes: where the system has no IPv6 the guest gets `not-supported`; where it has, a socket of the family. (Only the checks of
    addresses are tested beyond this: the loopback of IPv6 is not exercised, a machine may have none.)"""
    memory = bytearray(256)
    host = guest_of([memory], network=True)
    sk.wasi_sockets_tcp_create_socket_create_tcp_socket(host, 0, 1, 100)
    try:
        socket.socket(socket.AF_INET6, socket.SOCK_STREAM).close()
        has_ipv6 = True
    except OSError:
        has_ipv6 = False
    if has_ipv6:
        assert memory[100] == 0
        assert sk.wasi_sockets_tcp_tcp_socket_address_family(host, int.from_bytes(memory[104:108], "little")) == 1
    else:
        assert memory[100] == 1 and sk._NETWORK_ERRORS[memory[104]] == "not-supported"


def _has_ipv6_loopback() -> bool:
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as probe:
            probe.bind(("::1", 0))
    except OSError:
        return False
    return True


def test_ipv6_loopback_session() -> None:
    """A whole session over `::1`, in Python: bind, listen, connect, accept, data both ways, and a datagram each way between two UDP sockets. It is skipped where the machine
    has no IPv6 loopback (the checks of addresses and the family of a new socket are tested above on every machine). The C twin has no such test: it is not run over IPv6."""
    if not _has_ipv6_loopback():
        pytest.skip("no IPv6 loopback on this machine")

    memory = bytearray(4096)
    next_free = [3000]

    def bump(comp: int, old_ptr: int, old_size: int, align: int, new_size: int) -> int:
        ptr = (next_free[0] + align - 1) // align * align
        next_free[0] = ptr + new_size
        return ptr

    host = guest_of([memory], realloc=bump, socket_addr_check=lambda ip, port, use: True)
    r = 100

    def loop(port: int) -> tuple[int, ...]:  # the flat form: tag, port, flow, the eight 16-bit numbers of ::1, scope
        return (1, port, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0)

    def u16(off: int) -> int:
        return int.from_bytes(memory[off : off + 2], "little")

    def u32(off: int) -> int:
        return int.from_bytes(memory[off : off + 4], "little")

    def clear() -> None:
        memory[r : r + 64] = bytes(64)

    def ok() -> None:
        assert memory[r] == 0, f"result is an error: {bytes(memory[r : r + 8]).hex()}"

    def tcp(name: str, *args: int) -> None:
        clear()
        getattr(sk, "wasi_sockets_tcp_tcp_socket_" + name)(host, 0, *args, r)

    def udp(name: str, *args: int) -> None:
        clear()
        getattr(sk, "wasi_sockets_udp_udp_socket_" + name)(host, 0, *args, r)

    def settle(call: str, h: int) -> None:
        for _ in range(3000):
            tcp(call, h)
            if not (memory[r] == 1 and memory[r + 4] == sk._CODE["would-block"]):  # pyright: ignore[reportPrivateUsage]
                return
            time.sleep(0.001)

    def local_port(call: str, h: int) -> int:
        (tcp if call == "tcp" else udp)("local_address", h)
        ok()
        assert memory[r + 4] == 1  # an IPv6 address
        assert [u16(r + 16 + 2 * i) for i in range(8)] == [0, 0, 0, 0, 0, 0, 0, 1]  # ::1
        return u16(r + 8)

    def read_some(h: int) -> bytes:
        for _ in range(3000):
            clear()
            io.wasi_io_streams_input_stream_read(host, 0, h, 100, r)
            if memory[r] == 1 or u32(r + 8) > 0:
                break
            time.sleep(0.001)
        ok()
        return bytes(memory[u32(r + 4) : u32(r + 4) + u32(r + 8)])

    def write_all(h: int, data: bytes) -> None:
        memory[2000 : 2000 + len(data)] = data
        clear()
        io.wasi_io_streams_output_stream_blocking_write_and_flush(host, 0, h, 2000, len(data), r)
        ok()

    net = sk.wasi_sockets_instance_network_instance_network(host)

    # TCP
    clear()
    sk.wasi_sockets_tcp_create_socket_create_tcp_socket(host, 0, 1, r)
    ok()
    srv = u32(r + 4)
    tcp("start_bind", srv, net, *loop(0))
    ok()
    tcp("finish_bind", srv)
    ok()
    tcp("start_listen", srv)
    ok()
    tcp("finish_listen", srv)
    ok()
    port = local_port("tcp", srv)
    assert port != 0

    clear()
    sk.wasi_sockets_tcp_create_socket_create_tcp_socket(host, 0, 1, r)
    ok()
    cli = u32(r + 4)
    tcp("start_connect", cli, net, *loop(port))
    ok()
    settle("finish_connect", cli)
    ok()
    cin, cout = u32(r + 4), u32(r + 8)
    settle("accept", srv)
    ok()
    conn, sin, sout = u32(r + 4), u32(r + 8), u32(r + 12)
    assert sk.wasi_sockets_tcp_tcp_socket_address_family(host, conn) == 1
    write_all(cout, b"ping6")
    assert read_some(sin) == b"ping6"
    write_all(sout, b"pong6")
    assert read_some(cin) == b"pong6"
    tcp("remote_address", cli)
    ok()
    assert memory[r + 4] == 1 and u16(r + 8) == port

    # UDP
    ids: list[int] = []
    for _ in range(2):
        clear()
        sk.wasi_sockets_udp_create_socket_create_udp_socket(host, 0, 1, r)
        ok()
        ids.append(u32(r + 4))
    ports: list[int] = []
    for h in ids:
        udp("start_bind", h, net, *loop(0))
        ok()
        udp("finish_bind", h)
        ok()
        ports.append(local_port("udp", h))
    streams: list[tuple[int, int]] = []
    for h in ids:
        udp("stream", h, 0, *([0] * 12))  # no remote address: the tag and eleven numbers are ignored
        ok()
        streams.append((u32(r + 4), u32(r + 8)))

    def send(out: int, data: bytes, port_to: int) -> None:
        base = 2200
        memory[2000 : 2000 + len(data)] = data
        memory[base : base + 44] = bytes(44)
        store_u32(host, 0, base, 2000)
        store_u32(host, 0, base + 4, len(data))
        memory[base + 8] = 1  # has a remote address
        sk._write_addr(host, 0, base + 12, (1, port_to, (0, 0, 0, 0, 0, 0, 0, 1), 0, 0))  # pyright: ignore[reportPrivateUsage]
        clear()
        sk.wasi_sockets_udp_outgoing_datagram_stream_check_send(host, 0, out, r)
        ok()
        clear()
        sk.wasi_sockets_udp_outgoing_datagram_stream_send(host, 0, out, base, 1, r)
        ok()
        assert u32(r + 8) == 1  # one datagram sent

    def receive(inp: int) -> tuple[bytes, int, int]:
        for _ in range(3000):
            clear()
            sk.wasi_sockets_udp_incoming_datagram_stream_receive(host, 0, inp, 10, r)
            if memory[r] == 1 or u32(r + 8) > 0:
                break
            time.sleep(0.001)
        ok()
        assert u32(r + 8) == 1
        d = u32(r + 4)
        return bytes(memory[u32(d) : u32(d) + u32(d + 4)]), memory[d + 8], u16(d + 12)

    send(streams[0][1], b"v6 hello", ports[1])
    assert receive(streams[1][0]) == (b"v6 hello", 1, ports[0])
    send(streams[1][1], b"v6 back", ports[0])
    assert receive(streams[0][0]) == (b"v6 back", 1, ports[1])


# ---- http ----------------------------------------------------------------------------------------------------------------------


@pytest.fixture
def server() -> Iterator[tuple[int, list[str]]]:
    """A server on the loopback that records the request line of each request and answers /redirect with 302 and /big with 100 bytes."""
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    lines: list[str] = []

    def serve() -> None:
        while True:
            try:
                conn, _ = listener.accept()
            except OSError:
                return
            with conn:
                data = b""
                while b"\r\n\r\n" not in data:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                line = data.split(b"\r\n")[0].decode("latin-1")
                lines.append(line)
                if " /redirect " in line:
                    conn.sendall(
                        b"HTTP/1.1 302 Found\r\nLocation: /target\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    )
                elif " /big " in line:
                    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n" + b"x" * 100)
                else:
                    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        yield listener.getsockname()[1], lines
    finally:
        listener.close()


def get(port: int, path: str) -> H.HttpResponse:
    return H.default_send(H.HttpRequest("GET", "http", f"127.0.0.1:{port}", path, []))


def test_http_a_redirect_is_not_followed(server: tuple[int, list[str]]) -> None:
    port, lines = server
    response = get(port, "/redirect")
    assert response.status == 302 and ("location", b"/target") in [(n.lower(), v) for n, v in response.headers]
    assert lines == ["GET /redirect HTTP/1.1"]  # one request: the guest sees the 302 and decides


def test_http_is_http_1_1_with_one_connection_for_each_request(server: tuple[int, list[str]]) -> None:
    port, lines = server
    assert get(port, "/a").body == b"ok" and get(port, "/b").body == b"ok"
    assert lines == ["GET /a HTTP/1.1", "GET /b HTTP/1.1"]  # no HTTP/2, no reuse (`Connection: close`)


def test_http_proxy_variables_are_not_read(server: tuple[int, list[str]], monkeypatch: pytest.MonkeyPatch) -> None:
    port, lines = server
    for name in ("HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"):
        monkeypatch.setenv(name, "http://127.0.0.1:9")
    monkeypatch.delenv("NO_PROXY", raising=False)
    monkeypatch.delenv("no_proxy", raising=False)
    assert get(port, "/p").body == b"ok" and lines == ["GET /p HTTP/1.1"]  # straight to the server


def test_http_a_body_is_read_whole_and_limited(server: tuple[int, list[str]], monkeypatch: pytest.MonkeyPatch) -> None:
    port, _ = server
    assert H.MAX_BODY == 1 << 28  # 256 MiB
    assert get(port, "/big").body == b"x" * 100  # the whole body is in the answer when the guest first sees it
    monkeypatch.setattr(H, "MAX_BODY", 10)
    with pytest.raises(H.ErrorCode) as caught:
        get(port, "/big")
    assert caught.value.name == "HTTP-response-body-size"  # not a stream that fails halfway: the request fails


def test_http_the_c_has_no_tls(tmp_path: Path) -> None:
    """`https` in C is `internal-error` unless the embedder sets `http_send`; in Python the default transport has TLS (not tested here: it needs a server with a certificate)."""
    if CC is None:
        pytest.skip("no C compiler")
    source = tmp_path / "https.c"
    source.write_text(
        """#include <stdio.h>
#include "wasi_p2_http.h"
int main(void) {
    fusedwasi_p2_state s;
    fusedwasi_p2_http_request req;
    fusedwasi_p2_http_response out;
    fusedwasi_p2_http_error err;
    fusedwasi_p2_init(&s);
    memset(&req, 0, sizeof req), memset(&out, 0, sizeof out), memset(&err, 0, sizeof err);
    req.method = "GET", req.scheme = "https", req.authority = "example.invalid", req.path = "/";
    int rc = fusedwasi_p2_http_default_send(&s, &req, &out, &err);
    printf("%d %u %s\\n", rc, (unsigned)err.tag, err.str[0] ? err.str[0] : "");
    fusedwasi_p2_http_error_free(&err);
    return 0;
}
"""
    )
    binary = tmp_path / ("https.exe" if sys.platform == "win32" else "https")
    compile_c(source, binary, [C_P2])
    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.strip()
    rc, tag, message = out.split(" ", 2)
    assert rc != "0" and int(tag) == H._CASES.index(next(c for c in H._CASES if c[0] == "internal-error"))
    assert "http_send" in message


# ---- the same three limits of http, in C ---------------------------------------------------------------------------------------------------

C_FETCH = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasi_p2_http.h"
/* usage: fetch <port> <path>...: one request for each path, in this order, through the default transport; prints "<status> <location or -> <body>" for each */
int main(int argc, char **argv) {
    char authority[64];
    snprintf(authority, sizeof authority, "127.0.0.1:%s", argv[1]);
    fusedwasi_p2_state s;
    fusedwasi_p2_init(&s);
    for (int i = 2; i < argc; i++) {
        fusedwasi_p2_http_request req;
        fusedwasi_p2_http_response out;
        fusedwasi_p2_http_error err;
        memset(&req, 0, sizeof req), memset(&out, 0, sizeof out), memset(&err, 0, sizeof err);
        req.method = "GET", req.scheme = "http", req.authority = authority, req.path = argv[i]; /* req.headers is NULL: no headers */
        /* a path "!name" leaves that field NULL: the transport answers an error instead of crashing */
        if (strcmp(argv[i], "!method") == 0) req.method = NULL;
        if (strcmp(argv[i], "!scheme") == 0) req.scheme = NULL;
        if (strcmp(argv[i], "!authority") == 0) req.authority = NULL;
        if (strcmp(argv[i], "!path") == 0) req.path = NULL;
        int rc = fusedwasi_p2_http_default_send(&s, &req, &out, &err);
        if (rc != 0) {
            printf("error %u %s\\n", (unsigned)err.tag, err.str[0] ? err.str[0] : "-");
            fusedwasi_p2_http_error_free(&err);
            continue;
        }
        const char *location = "-";
        for (uint32_t k = 0; out.headers && k < out.headers->count; k++)
            if (strcmp(out.headers->e[k].name, "location") == 0) location = (const char *)out.headers->e[k].value;
        printf("%u %.*s %.*s\\n", (unsigned)out.status, location[0] == '-' ? 1 : 7, location, (int)(out.body ? out.body->len : 0), out.body ? (const char *)out.body->data : "");
        fusedwasi_p2_http_fields_unref(out.headers);
        if (out.body) fusedwasi_p2_http_blob_unref(out.body);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def c_fetch(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    where = tmp_path_factory.mktemp("c_fetch")
    source = where / "fetch.c"
    source.write_text(C_FETCH)
    binary = where / ("fetch.exe" if sys.platform == "win32" else "fetch")
    compile_c(source, binary, [C_P2])
    return binary


def c_get(binary: Path, port: int, *paths: str, env: dict[str, str] | None = None) -> list[str]:
    run = subprocess.run([str(binary), str(port), *paths], capture_output=True, text=True, check=True, env=env)
    return run.stdout.splitlines()


def test_http_a_redirect_is_not_followed_in_c(c_fetch: Path, server: tuple[int, list[str]]) -> None:
    port, lines = server
    assert c_get(c_fetch, port, "/redirect") == ["302 /target "]
    assert lines == ["GET /redirect HTTP/1.1"]  # one request: the guest sees the 302 and decides


def test_http_is_http_1_1_with_one_connection_for_each_request_in_c(
    c_fetch: Path, server: tuple[int, list[str]]
) -> None:
    port, lines = server
    assert c_get(c_fetch, port, "/a", "/b") == ["200 - ok", "200 - ok"]
    assert lines == ["GET /a HTTP/1.1", "GET /b HTTP/1.1"]  # no HTTP/2, no reuse (`Connection: close`)


def test_http_proxy_variables_are_not_read_in_c(c_fetch: Path, server: tuple[int, list[str]]) -> None:
    port, lines = server
    env = {k: v for k, v in os.environ.items() if k.lower() not in ("no_proxy", "http_proxy", "all_proxy")}
    for name in ("HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"):
        env[name] = "http://127.0.0.1:9"
    assert c_get(c_fetch, port, "/p", env=env) == ["200 - ok"] and lines == [
        "GET /p HTTP/1.1"
    ]  # straight to the server


def test_http_a_request_with_a_missing_field_is_an_error_in_c(c_fetch: Path, server: tuple[int, list[str]]) -> None:
    """The method, the scheme, the authority and the path of a request must be there; a NULL is answered with an error, and nothing is sent. (`headers` NULL is no headers.)"""
    port, lines = server
    uri_invalid = H._CASES.index(next(c for c in H._CASES if c[0] == "HTTP-request-URI-invalid"))
    internal = H._CASES.index(next(c for c in H._CASES if c[0] == "internal-error"))
    assert c_get(c_fetch, port, "!method", "!scheme", "!authority", "!path") == [
        f"error {internal} the request has no method",
        f"error {internal} the request has no scheme",
        f"error {uri_invalid} -",
        f"error {uri_invalid} -",
    ]
    assert lines == []  # no connection was made
