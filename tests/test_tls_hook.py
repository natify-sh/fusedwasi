"""The TLS hook of the C transport of `wasi:http` (docs/specs/S0001.md, step 2).

The hook here has no TLS: it answers from a script (`tests/data/wasi_p2_tls_hook_driver.c`), so what is tested is how the library uses the hook (the descriptor and the host it gives,
the pieces it reads and writes, the closing) and how it maps what the hook reports to the `error-code` of wasi:http. A real TLS library behind the hook is the later step. The
Python twin has no hook (S0001, 3.3): its TLS is `ssl`, and `test_known_limits.py` pins that.
"""

import socket
import subprocess
import sys
import threading
from collections.abc import Iterator
from pathlib import Path

import pytest
from cbuild import CC, compile_c

from fusedwasi.p2 import wasi_p2_http as H

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
DRIVER = Path(__file__).parent / "data" / "wasi_p2_tls_hook_driver.c"


def tag(name: str) -> int:
    return H._CASES.index(next(c for c in H._CASES if c[0] == name))


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("tls_hook") / ("driver.exe" if sys.platform == "win32" else "driver")
    compile_c(DRIVER, binary, [C_P2])
    return binary


@pytest.fixture
def idle_port() -> Iterator[int]:
    """A listener that accepts connections (the kernel does it) and says nothing: the hook has no TLS, so nothing is read from it or written to it."""
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    try:
        yield listener.getsockname()[1]
    finally:
        listener.close()


@pytest.fixture
def plain_port() -> Iterator[int]:
    """A plain HTTP server: /x answers 200 `ok`; /truncated promises 10 bytes, sends 3 and closes."""
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)

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
                if b" /truncated " in data:
                    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc")
                else:
                    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")

    threading.Thread(target=serve, daemon=True).start()
    try:
        yield listener.getsockname()[1]
    finally:
        listener.close()


def run(driver: Path, scenario: str, port: int) -> dict[str, str]:
    out = subprocess.run(
        [str(driver), scenario, str(port)], capture_output=True, text=True, check=True, timeout=60
    ).stdout
    lines = out.splitlines()
    return {
        "result": lines[0],
        "calls": lines[1],
        "request": lines[2].removeprefix("request="),
    }


def test_the_hook_carries_the_request_and_the_answer(driver: Path, idle_port: int) -> None:
    got = run(driver, "ok", idle_port)
    assert got["result"] == "ok status=200 body=ok"
    assert (
        got["calls"] == "calls connect=1 read=1 write=1 close=1 fd_ok=1 host=127.0.0.1"
    )  # the host without the port; a descriptor and a time were given
    request = got["request"]
    assert request.startswith(
        f"GET /x HTTP/1.1\\r\\nHost: 127.0.0.1:{idle_port}\\r\\n"
    )  # the same request as over a socket, written in pieces of 16 bytes
    assert request.endswith("\\r\\n\\r\\n")


ERRORS = [
    # what the hook reports at connect: no session, so no close
    ("connect_certificate", "TLS-certificate-error", None, 0),
    ("connect_protocol", "TLS-protocol-error", None, 0),
    ("connect_timeout", "connection-timeout", None, 0),
    ("connect_terminated", "connection-terminated", None, 0),
    ("connect_other", "internal-error", "the library said no", 0),
    ("connect_unset_kind", "internal-error", "the TLS hook failed", 0),
    # at read and at write: the session is closed
    ("read_timeout", "connection-read-timeout", None, 1),
    ("read_certificate", "TLS-certificate-error", None, 1),
    ("read_protocol", "TLS-protocol-error", None, 1),
    ("read_terminated", "connection-terminated", None, 1),
    ("read_other", "internal-error", "a failure of the record layer", 1),
    ("read_other_no_message", "internal-error", "the TLS hook failed", 1),
    ("read_too_much", "internal-error", "the TLS hook read more than it was asked for", 1),
    ("write_timeout", "connection-write-timeout", None, 1),
    ("write_terminated", "connection-terminated", None, 1),
    ("write_zero", "internal-error", "the TLS hook wrote nothing", 1),
    ("write_too_much", "internal-error", "the TLS hook wrote more than it was given", 1),
]


@pytest.mark.parametrize(("scenario", "case", "message", "closes"), ERRORS)
def test_what_the_hook_reports_is_the_error_code(
    driver: Path, idle_port: int, scenario: str, case: str, message: str | None, closes: int
) -> None:
    got = run(driver, scenario, idle_port)
    assert got["result"] == f"error tag={tag(case)} message={message or '-'}"
    assert f"close={closes} " in got["calls"], got["calls"]


def test_a_session_that_ends_early_is_as_the_plain_path(driver: Path, idle_port: int, plain_port: int) -> None:
    """The answer is cut short; the library says what it says over a socket, whatever carries the bytes."""
    over_the_hook = run(driver, "truncated", idle_port)["result"]
    over_a_socket = run(driver, "plain_truncated", plain_port)["result"]
    assert over_the_hook.startswith("error tag=") and over_the_hook == over_a_socket


def test_a_request_over_http_does_not_touch_the_hook(driver: Path, plain_port: int) -> None:
    got = run(driver, "http_untouched", plain_port)
    assert got["result"] == "ok status=200 body=ok"
    assert got["calls"].startswith("calls connect=0 read=0 write=0 close=0 ")


def test_without_a_hook_https_is_the_internal_error_of_today(driver: Path, idle_port: int) -> None:
    got = run(driver, "unset", idle_port)
    assert got["result"].startswith(f"error tag={tag('internal-error')} message=https needs a TLS hook")
    assert "http_send" in got["result"]
    assert got["calls"].startswith("calls connect=0 ")
