"""The reference TLS hook over Mbed TLS (`tests/tls_mbedtls/`), against a real TLS server, and the Python twin on the same server (docs/specs/S0001.md, step 4).

For each request the C twin (the library with the reference hook) and the Python twin (the library with its own TLS, `ssl`) are given the same server, and must give the same status and
body, or the same case of the `error-code`. The server is made here, with Python's `ssl`, and the certificates are made here with the `openssl` command: a throwaway CA, a server
certificate for `localhost` and `127.0.0.1`, one for a name that is not the host, and one from another CA. The server of TLS 1.1 is `openssl s_server`, because Python warns (and the warning is not silenced) when a context is given a version that is deprecated. No test uses the internet except the fetch of Mbed TLS by CMake.

It is **opt in**: it fetches Mbed TLS and builds it, so a plain `pytest` skips it. Set `FUSEDWASI_TEST_TLS=1` (the CI job of Ubuntu does); then a missing `cmake` or `openssl`, or a build
that fails, is a failure and not a skip. `FUSEDWASI_TEST_MBEDTLS_SRC` names an unpacked `mbedtls-3.6.7` to use instead of the download (offline).
"""

import os
import shlex
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Iterator
from pathlib import Path

import pytest

from fusedwasi.p2 import wasi_p2_http as H

HERE = Path(__file__).parent
C_P2 = HERE.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"

pytestmark = pytest.mark.skipif(
    not os.environ.get("FUSEDWASI_TEST_TLS"),
    reason="set FUSEDWASI_TEST_TLS=1 to build the reference TLS hook with Mbed TLS (it needs cmake, openssl and the network)",
)


def tag(name: str) -> int:
    return H._CASES.index(next(c for c in H._CASES if c[0] == name))


def run(cmd: list[str], *, cwd: Path | None = None, timeout: float | None = None) -> "subprocess.CompletedProcess[str]":
    done = subprocess.run(cmd, check=False, capture_output=True, text=True, cwd=cwd, timeout=timeout)
    assert done.returncode == 0, f"{cmd[0]} failed:\n{done.stdout}\n{done.stderr}"
    return done


def find_openssl() -> str:
    """An OpenSSL 3 `openssl` command. The one on the PATH when it is OpenSSL 3; otherwise the usual places (Homebrew on macOS, Git for Windows, where the PATH has none or has LibreSSL).
    `FUSEDWASI_TEST_OPENSSL` names one by hand."""
    candidates = [os.environ.get("FUSEDWASI_TEST_OPENSSL"), shutil.which("openssl")]
    candidates += [
        "/opt/homebrew/opt/openssl@3/bin/openssl",
        "/usr/local/opt/openssl@3/bin/openssl",
        r"C:\Program Files\Git\usr\bin\openssl.exe",
        r"C:\Program Files\Git\mingw64\bin\openssl.exe",
        r"C:\Program Files\OpenSSL-Win64\bin\openssl.exe",
    ]
    seen: list[str] = []
    for candidate in candidates:
        if not candidate or not Path(candidate).is_file():
            continue
        version = subprocess.run([candidate, "version"], check=False, capture_output=True, text=True).stdout.strip()
        seen.append(f"{candidate}: {version}")
        if version.startswith("OpenSSL 3"):
            return candidate
    raise AssertionError(
        "FUSEDWASI_TEST_TLS is set and there is no OpenSSL 3 `openssl` command; seen: " + "; ".join(seen)
    )


OPENSSL_CONFIG = "[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=fusedwasi\n"  # a build of openssl for Windows has no default configuration file


@pytest.fixture(scope="module")
def tls_driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    assert shutil.which("cmake"), "FUSEDWASI_TEST_TLS is set and there is no cmake"
    build = tmp_path_factory.mktemp("tls_build")
    configure = [
        "cmake",
        "-S",
        str(HERE / "tls_mbedtls"),
        "-B",
        str(build),
        f"-DFUSEDWASI_INCLUDE_DIR={C_P2}",
        "-DCMAKE_BUILD_TYPE=MinSizeRel",
    ]
    configure += shlex.split(
        os.environ.get("FUSEDWASI_TEST_CMAKE_ARGS", ""), posix=os.name != "nt"
    )  # a generator, an architecture, a C flag
    if os.environ.get("FUSEDWASI_TEST_MBEDTLS_SRC"):
        configure.append(f"-DFETCHCONTENT_SOURCE_DIR_MBEDTLS={os.environ['FUSEDWASI_TEST_MBEDTLS_SRC']}")
    run(configure, timeout=600)
    run(["cmake", "--build", str(build), "--config", "MinSizeRel", "--parallel", "--target", "tls_driver"], timeout=900)
    binary = next(
        p for p in build.rglob("tls_driver*") if p.is_file() and os.access(p, os.X_OK) and p.suffix in ("", ".exe")
    )
    return binary


class Pki:
    """A throwaway CA and three server certificates, made with the `openssl` command."""

    def __init__(self, where: Path) -> None:
        self.openssl = find_openssl()
        self.where = where
        self.config = where / "openssl.cnf"
        self.config.write_text(OPENSSL_CONFIG)
        self.ca = self._ca("ca")
        self.rogue = self._ca("rogue")
        self.good = self._leaf("good", self.ca, "DNS:localhost,IP:127.0.0.1")  # the host, by name and by address
        self.other_name = self._leaf("other", self.ca, "DNS:other.test")  # a name that is not the host
        self.from_rogue = self._leaf(
            "rogue_leaf", self.rogue, "DNS:localhost,IP:127.0.0.1"
        )  # the right names, a CA that is not trusted

    def _openssl(self, *args: str) -> None:
        run([self.openssl, *args], cwd=self.where)

    def _ca(self, name: str) -> tuple[Path, Path]:
        key, cert = self.where / f"{name}.key", self.where / f"{name}.pem"
        self._openssl(
            "req", "-config", str(self.config), "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes", "-keyout", str(key), "-out", str(cert),
            "-subj", f"/CN=fusedwasi-test-{name}", "-days", "2",
            "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        )  # fmt: skip
        return cert, key

    def _leaf(self, name: str, ca: tuple[Path, Path], names: str) -> tuple[Path, Path]:
        key, request, cert, extensions = (
            self.where / f"{name}.key",
            self.where / f"{name}.csr",
            self.where / f"{name}.pem",
            self.where / f"{name}.ext",
        )
        extensions.write_text(
            f"subjectAltName={names}\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n"
        )
        self._openssl(
            "req",
            "-config",
            str(self.config),
            "-newkey",
            "ec",
            "-pkeyopt",
            "ec_paramgen_curve:prime256v1",
            "-nodes",
            "-keyout",
            str(key),
            "-out",
            str(request),
            "-subj",
            "/CN=server",
        )
        self._openssl(
            "x509",
            "-req",
            "-in",
            str(request),
            "-CA",
            str(ca[0]),
            "-CAkey",
            str(ca[1]),
            "-CAcreateserial",
            "-out",
            str(cert),
            "-days",
            "2",
            "-extfile",
            str(extensions),
        )
        return cert, key


@pytest.fixture(scope="module")
def pki(tmp_path_factory: pytest.TempPathFactory) -> Pki:
    return Pki(tmp_path_factory.mktemp("pki"))


BIG = bytes(range(251)) * 400  # 100,400 bytes: several TLS records of 16 KiB


def answer(path: str) -> tuple[bytes, bytes]:
    """The head and the body for a path of the test server."""
    if path == "/x":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n", b"ok"
    if path == "/big":
        return b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(BIG), BIG
    if path == "/close":  # no length: the body ends with the closing of the connection
        return b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", b"until the end"
    raise KeyError(path)


@pytest.fixture
def tls_server(pki: Pki) -> Iterator[Callable[..., int]]:
    """`start(cert_and_key) -> port`: a TLS server on 127.0.0.1 that serves /x, /big and /close, and never answers /hang."""
    listeners: list[socket.socket] = []

    def start(certificate: tuple[Path, Path]) -> int:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(certificate[0]), str(certificate[1]))
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        listeners.append(listener)

        def serve() -> None:
            while True:
                try:
                    conn, _ = listener.accept()
                except OSError:
                    return
                threading.Thread(target=handle, args=(conn,), daemon=True).start()

        def handle(conn: socket.socket) -> None:
            try:
                with context.wrap_socket(conn, server_side=True) as tls:
                    data = b""
                    while b"\r\n\r\n" not in data:
                        chunk = tls.recv(4096)
                        if not chunk:
                            return
                        data += chunk
                    path = data.split(b" ")[1].decode()
                    if path == "/hang":
                        time.sleep(5)
                        return
                    head, body = answer(path)
                    tls.sendall(head + body)
            except (ssl.SSLError, OSError):
                pass

        threading.Thread(target=serve, daemon=True).start()
        return int(listener.getsockname()[1])

    yield start
    for listener in listeners:
        listener.close()


@pytest.fixture
def old_tls_port(pki: Pki) -> Iterator[int]:
    """A server that speaks TLS 1.1 and nothing newer: `openssl s_server`, and not Python's `ssl`. Python warns when a context is given TLS 1.0 or 1.1 (they are deprecated there), and the
    server of another implementation is the better witness anyway. Where this `openssl` cannot serve TLS 1.1 the test is skipped (and the job of the CI that must run it fails on the skip)."""
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = int(probe.getsockname()[1])
    probe.close()
    server = subprocess.Popen(
        [
            pki.openssl,
            "s_server",
            "-accept",
            str(port),
            "-cert",
            str(pki.good[0]),
            "-key",
            str(pki.good[1]),
            "-tls1_1",
            "-cipher",
            "ALL:@SECLEVEL=0",
            "-quiet",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        for _ in range(100):  # up to 5 s for the listener to be there
            if server.poll() is not None:
                pytest.skip(
                    f"this openssl cannot serve TLS 1.1: {(server.stderr.read() if server.stderr else '').strip()[:200]}"
                )
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                break
            except OSError:
                time.sleep(0.05)
        else:
            pytest.skip("openssl s_server did not start")
        yield port
    finally:
        server.terminate()
        server.wait(timeout=10)
        if server.stdin:
            server.stdin.close()
        if server.stderr:
            server.stderr.close()


@pytest.fixture
def silent_port() -> Iterator[int]:
    """A listener that takes the connection and says nothing at all: a handshake that never gets an answer."""
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    try:
        yield int(listener.getsockname()[1])
    finally:
        listener.close()


@pytest.fixture
def plain_on_tls_port() -> Iterator[int]:
    """A server that answers in plain HTTP, whatever it is sent: what a client of TLS meets on a port that is not TLS."""
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
                try:
                    conn.recv(4096)
                    conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
                except OSError:
                    pass

    threading.Thread(target=serve, daemon=True).start()
    try:
        yield int(listener.getsockname()[1])
    finally:
        listener.close()


Result = tuple[str, int, bytes] | tuple[str, int]


def over_c(driver: Path, authority: str, path: str, ca: Path | None, timeouts: dict[str, int]) -> Result:
    cmd = [str(driver), "--authority", authority, "--path", path]
    if ca:
        cmd += ["--ca", str(ca)]
    for name, ms in timeouts.items():
        cmd += [f"--{name}-ms", str(ms)]
    line = run(cmd, timeout=60).stdout.strip()
    if line.startswith("ok "):
        status, body = line.removeprefix("ok ").split(" ")
        return ("ok", int(status.removeprefix("status=")), bytes.fromhex(body.removeprefix("body=")))
    return ("error", int(line.removeprefix("error tag=")))


def over_python(
    monkeypatch: pytest.MonkeyPatch, authority: str, path: str, ca: Path, timeouts: dict[str, int]
) -> Result:
    original = ssl.create_default_context

    def only_the_test_ca(*args: object, **kwargs: object) -> ssl.SSLContext:
        return original(cafile=str(ca))

    # the test CA is the only anchor, as it is for the C hook; the replacement is the test's and is not an API of the library (S0001, 3.3)
    monkeypatch.setattr(ssl, "create_default_context", only_the_test_ca)
    request = H.HttpRequest(
        "GET",
        "https",
        authority,
        path,
        [],
        connect_ns=timeouts["connect"] * 1_000_000 if "connect" in timeouts else None,
        first_byte_ns=timeouts["first"] * 1_000_000 if "first" in timeouts else None,
        between_bytes_ns=timeouts["between"] * 1_000_000 if "between" in timeouts else None,
    )
    try:
        response = H.default_send(request)
    except H.ErrorCode as failure:
        return ("error", tag(failure.name))
    return ("ok", response.status, response.body)


def both(
    driver: Path, monkeypatch: pytest.MonkeyPatch, authority: str, path: str, ca: Path, **timeouts: int
) -> tuple[Result, Result]:
    return over_c(driver, authority, path, ca, timeouts), over_python(monkeypatch, authority, path, ca, timeouts)


@pytest.mark.parametrize("host", ["localhost", "127.0.0.1"])
@pytest.mark.parametrize(
    ("path", "body"),
    [
        pytest.param("/x", b"ok", id="short"),
        pytest.param("/big", BIG, id="big"),
        pytest.param("/close", b"until the end", id="close"),
    ],
)  # an explicit id: the bytes of BIG would be the name of the test, and the name goes into the environment of the child
def test_a_request_over_tls_gives_the_same_answer(
    tls_driver: Path,
    pki: Pki,
    tls_server: Callable[..., int],
    monkeypatch: pytest.MonkeyPatch,
    host: str,
    path: str,
    body: bytes,
) -> None:
    port = tls_server(pki.good)
    c, py = both(tls_driver, monkeypatch, f"{host}:{port}", path, pki.ca[0])
    assert c == ("ok", 200, body)
    assert py == c


def test_a_certificate_for_another_name_is_refused_by_both(
    tls_driver: Path, pki: Pki, tls_server: Callable[..., int], monkeypatch: pytest.MonkeyPatch
) -> None:
    port = tls_server(pki.other_name)
    c, py = both(tls_driver, monkeypatch, f"localhost:{port}", "/x", pki.ca[0])
    assert c == py == ("error", tag("TLS-certificate-error"))


def test_a_certificate_of_a_ca_that_is_not_trusted_is_refused_by_both(
    tls_driver: Path, pki: Pki, tls_server: Callable[..., int], monkeypatch: pytest.MonkeyPatch
) -> None:
    port = tls_server(pki.from_rogue)
    c, py = both(tls_driver, monkeypatch, f"localhost:{port}", "/x", pki.ca[0])
    assert c == py == ("error", tag("TLS-certificate-error"))


def test_a_server_that_speaks_plain_http_is_a_protocol_error_in_both(
    tls_driver: Path, pki: Pki, plain_on_tls_port: int, monkeypatch: pytest.MonkeyPatch
) -> None:
    c, py = both(tls_driver, monkeypatch, f"localhost:{plain_on_tls_port}", "/x", pki.ca[0])
    assert c == py == ("error", tag("TLS-protocol-error"))


def test_a_server_of_an_old_version_of_tls_is_a_protocol_error_in_both(
    tls_driver: Path, pki: Pki, old_tls_port: int, monkeypatch: pytest.MonkeyPatch
) -> None:
    c, py = both(tls_driver, monkeypatch, f"localhost:{old_tls_port}", "/x", pki.ca[0])
    assert c == py == ("error", tag("TLS-protocol-error"))


def test_a_handshake_that_gets_no_answer_is_a_connection_timeout_in_both(
    tls_driver: Path, pki: Pki, silent_port: int, monkeypatch: pytest.MonkeyPatch
) -> None:
    c, py = both(tls_driver, monkeypatch, f"localhost:{silent_port}", "/x", pki.ca[0], connect=400)
    assert c == py == ("error", tag("connection-timeout"))


def test_an_answer_that_does_not_come_is_a_read_timeout_in_both(
    tls_driver: Path, pki: Pki, tls_server: Callable[..., int], monkeypatch: pytest.MonkeyPatch
) -> None:
    port = tls_server(pki.good)
    c, py = both(tls_driver, monkeypatch, f"localhost:{port}", "/hang", pki.ca[0], first=400)
    assert c == py == ("error", tag("connection-read-timeout"))


def test_the_reference_hook_without_trust_anchors_refuses(
    tls_driver: Path, pki: Pki, tls_server: Callable[..., int]
) -> None:
    """The hook does not fall back to "no check": with no anchors the request fails as a certificate error (the Python twin has the system store: the deliberate difference)."""
    port = tls_server(pki.good)
    assert over_c(tls_driver, f"localhost:{port}", "/x", None, {}) == ("error", tag("TLS-certificate-error"))


def test_the_driver_is_built_with_the_trimmed_configuration(tls_driver: Path) -> None:
    """The size of the TLS in the program of the embedder is the reason for the trimmed configuration (S0001): the driver stays well below what the default one gives (674 KB stripped, S0002)."""
    assert tls_driver.stat().st_size < 1_200_000 or sys.platform == "win32"
