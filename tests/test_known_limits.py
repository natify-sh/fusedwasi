"""Tests that pin the known limits of `docs/host_scope.md` ("Known limits"): what a guest sees today. When one of them fails, a limit has changed: change the document with it, on purpose.

Nothing here is a feature; each test says what the host does where it does not do more.
"""

import importlib
import inspect
import re
import socket
import subprocess
import sys
import threading
from collections.abc import Iterator
from pathlib import Path

import pytest
from cbuild import CC, compile_c
from guests import guest_of

from fusedwasi.p2 import wasi_p2_http as H
from fusedwasi.p2 import wasi_p2_sockets as sk

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
