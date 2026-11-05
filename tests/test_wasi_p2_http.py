"""wasi:http of the host of WASI 0.2 (`fusedwasi.p2.wasi_p2_http`, `include/fusedwasi/p2/wasi_p2_http.h`): the same scenarios in Python and in C, against a small HTTP server on the loopback,
and against a transport that the embedder gives (`http_send`).

The port is the one the system gives, so it is never printed: the server masks it in what it echoes. What is compared is the whole output: statuses, fields, bodies, trailers and
the bytes of every `error-code` that the guest receives (layout and strings), so the layouts of the Canonical ABI are compared too.
"""

import socket
import struct
import subprocess
import sys
import threading
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from cbuild import CC, compile_c
from guests import guest_of

from fusedwasi.p2 import wasi_p2_http as H
from fusedwasi.p2 import wasi_p2_io as io
from fusedwasi.p2.wasi_p2_core import Trap, _load_u32, _store_u32
from fusedwasi.p2.wasi_p2_io import StreamClosed, StreamFailed

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
DRIVER = Path(__file__).parent / "data" / "wasi_p2_http_driver.c"

SCENARIOS = [
    "main",
    pytest.param(
        "late_errors",
        marks=pytest.mark.xfail(
            strict=True,
            reason="S0013: the C twin reads the answer whole, so a broken body fails at `get`; the Python twin streams it",
        ),
    ),
    "denied",
    "check",
    "hook",
    "fields",
    "setters",
    "server",
    "flat_errors",
    "limits",
    "finish_alive",
    "bad_handle",
]
R = 100
STD_METHODS = ("GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH")

# what the transport of the embedder fails with for /fail/N (the same table is in the driver)
FAILS: list[H.ErrorCode] = [
    H.ErrorCode("DNS-timeout"),
    H.ErrorCode("DNS-error", "NXDOMAIN", None),
    H.ErrorCode("DNS-error", "rc", 7),
    H.ErrorCode("TLS-alert-received", 42, "bad"),
    H.ErrorCode("TLS-alert-received", None, None),
    H.ErrorCode("HTTP-request-body-size", (1 << 40) + 5),
    H.ErrorCode("HTTP-request-body-size", None),
    H.ErrorCode("HTTP-request-header-size", "name", 99),
    H.ErrorCode("HTTP-request-header-size", present=False),
    H.ErrorCode("HTTP-response-header-size", "h", None),
    H.ErrorCode("HTTP-response-header-section-size", 65536),
    H.ErrorCode("HTTP-response-transfer-coding", "gzip"),
    H.ErrorCode("internal-error", "oops"),
    H.ErrorCode("internal-error", None),
    H.ErrorCode("connection-refused"),
    H.ErrorCode("loop-detected"),
]


def hook(req: H.HttpRequest) -> H.HttpResponse:
    if req.path.startswith("/fail/"):
        raise FAILS[int(req.path[6:])]
    lines = [f"{req.method} {req.scheme} {req.authority} {req.path}"]
    lines += [f"{n}: {v.decode('latin-1')}" for n, v in req.headers]
    lines.append("body=" + req.body.decode("latin-1"))
    if req.trailers is not None:
        lines += [f"trailer {n}: {v.decode('latin-1')}" for n, v in req.trailers]
    return H.HttpResponse(
        201,
        [("X-Upper", b"One"), ("x-dup", b"a"), ("X-DUP", b"b")],
        "\n".join(lines).encode(),
        [("X-Trailer", b"t")],
    )


def describe(ec: H.ErrorCode | None) -> str:
    """An error-code as text, in a form the driver prints too: the tag, whether the payload is there, the leaves."""
    if ec is None:
        return "none"
    return f"{ec.tag}/{int(ec.present)}/" + "/".join("-" if v is None else str(v) for v in ec.values)


def quoted(text: str | None) -> str:
    """What the driver prints for a text that may be missing (not `repr`: it would escape what C prints as it is)."""
    return "None" if text is None else f"'{text}'"


def check_authority(scheme: str, authority: str) -> bool:
    return authority == "allowed.test:80"


def python_output(scenario: str, port: int, dead: int) -> str:
    memory = bytearray(1 << 16)
    next_free = [40000]
    next_input = [4000]

    def bump(comp: int, old_ptr: int, old_size: int, align: int, new_size: int) -> int:
        ptr = (next_free[0] + align - 1) // align * align
        next_free[0] = ptr + new_size
        return ptr

    host = guest_of(
        [memory],
        realloc=bump,
        http=scenario not in ("denied", "check"),
        http_check=check_authority if scenario == "check" else None,
        http_send=hook if scenario in ("hook", "check", "fields", "setters", "limits") else None,
    )
    out: list[str] = []

    def pr(text: str) -> None:
        out.append(text)

    def u32(off: int) -> int:
        return _load_u32(host, 0, off)

    def clear() -> None:
        memory[R : R + 64] = bytes(64)

    def put(data: bytes) -> int:
        ptr = next_input[0]
        memory[ptr : ptr + len(data)] = data
        next_input[0] = (ptr + len(data) + 8) // 8 * 8
        return ptr

    def error_at(label: str, off: int) -> None:
        pr(f"{label} error:{bytes(memory[off : off + 32]).hex()}\n")

    def pairs(items: list[tuple[str, str]]) -> int:
        spans = [(put(n.encode()), len(n), put(v.encode()), len(v)) for n, v in items]
        arr = put(bytes(16 * len(items)))
        for i, span in enumerate(spans):
            for k, x in enumerate(span):
                _store_u32(host, 0, arr + 16 * i + 4 * k, x)
        return arr

    def blobs(items: list[str]) -> int:
        """A `list<list<u8>>`: the pointer to the array of (pointer, length)."""
        spans = [(put(x.encode()), len(x)) for x in items]
        arr = put(bytes(8 * len(items)))
        for i, (ptr, n) in enumerate(spans):
            _store_u32(host, 0, arr + 8 * i, ptr)
            _store_u32(host, 0, arr + 8 * i + 4, n)
        return arr

    def fields(items: list[tuple[str, str]]) -> int:
        clear()
        H.wasi_http_types_fields_from_list(host, 0, pairs(items), len(items), R)
        assert memory[R] == 0
        return u32(R + 4)

    def show_entries(label: str, handle: int) -> None:
        clear()
        H.wasi_http_types_fields_entries(host, 0, handle, R)
        ptr, n = u32(R), u32(R + 4)
        for i in range(n):
            e = ptr + 16 * i
            name = bytes(memory[u32(e) : u32(e) + u32(e + 4)]).decode("latin-1")
            value = bytes(memory[u32(e + 8) : u32(e + 8) + u32(e + 12)]).decode("latin-1")
            pr(f"  {label} {name}: {value}\n")

    def method_args(method: str) -> tuple[int, int, int]:
        if method in STD_METHODS:
            return STD_METHODS.index(method), 0, 0
        return 9, put(method.encode()), len(method)

    def scheme_args(scheme: str | None) -> tuple[int, int, int, int]:
        if scheme is None:
            return 0, 0, 0, 0
        if scheme in ("http", "https"):
            return 1, ("http", "https").index(scheme), 0, 0
        return 1, 2, put(scheme.encode()), len(scheme)

    def opt_text(text: str | None) -> tuple[int, int, int]:
        return (0, 0, 0) if text is None else (1, put(text.encode()), len(text))

    def make_request(
        method: str, scheme: str | None, authority: str | None, path: str | None, headers: list[tuple[str, str]]
    ) -> int:
        req = H.wasi_http_types_outgoing_request_new(host, fields(headers))
        pr(f"  set-method {H.wasi_http_types_outgoing_request_set_method(host, 0, req, *method_args(method))}\n")
        pr(f"  set-scheme {H.wasi_http_types_outgoing_request_set_scheme(host, 0, req, *scheme_args(scheme))}\n")
        pr(f"  set-authority {H.wasi_http_types_outgoing_request_set_authority(host, 0, req, *opt_text(authority))}\n")
        pr(f"  set-path {H.wasi_http_types_outgoing_request_set_path_with_query(host, 0, req, *opt_text(path))}\n")
        return req

    def write_body(body: bytes | None, trailers: list[tuple[str, str]] | None, req: int, label: str) -> None:
        clear()
        H.wasi_http_types_outgoing_request_body(host, 0, req, R)
        body_h = u32(R + 4)
        if body is not None:
            clear()
            H.wasi_http_types_outgoing_body_write(host, 0, body_h, R)
            stream = u32(R + 4)
            clear()
            io.wasi_io_streams_output_stream_blocking_write_and_flush(host, 0, stream, put(body), len(body), R)
            io.wasi_io_streams_output_stream_drop(host, stream)
        t = fields(trailers) if trailers is not None else 0
        clear()
        H.wasi_http_types_outgoing_body_finish(host, 0, body_h, int(trailers is not None), t, R)
        if memory[R] == 1:
            error_at(f"{label} finish", R + 8)

    def read_all(stream: int) -> bytes:
        data = b""
        while True:
            clear()
            io.wasi_io_streams_input_stream_read(host, 0, stream, 4096, R)
            if memory[R] == 1:
                if memory[R + 4] == 0:
                    pr("  body stream failed\n")
                    failure = u32(R + 8)
                    clear()
                    H.wasi_http_types_http_error_code(host, 0, failure, R)
                    if memory[R] == 1:
                        error_at("  body", R + 8)
                return data
            data += bytes(memory[u32(R + 4) : u32(R + 4) + u32(R + 8)])

    def receive(label: str, fut: int) -> None:
        pol = H.wasi_http_types_future_incoming_response_subscribe(host, fut)
        io.wasi_io_poll_pollable_block(host, pol)
        io.wasi_io_poll_pollable_drop(host, pol)
        clear()
        H.wasi_http_types_future_incoming_response_get(host, 0, fut, R)
        pr(f"  get tags {memory[R]} {memory[R + 8]} {memory[R + 16]}\n")
        if memory[R + 16] == 1:
            error_at(label, R + 24)
            return
        resp = u32(R + 24)
        pr(f"  status {H.wasi_http_types_incoming_response_status(host, resp)}\n")
        headers = H.wasi_http_types_incoming_response_headers(host, resp)
        show_entries("header", headers)
        pr(f"  append to the headers of an answer {_append(headers)}\n")
        H.wasi_http_types_fields_drop(host, headers)
        clear()
        H.wasi_http_types_incoming_response_consume(host, 0, resp, R)
        body = u32(R + 4)
        clear()
        H.wasi_http_types_incoming_response_consume(host, 0, resp, R)
        pr(f"  consume again {memory[R]}\n")
        clear()
        H.wasi_http_types_incoming_body_stream(host, 0, body, R)
        stream = u32(R + 4)
        data = read_all(stream)
        pr(f"  body {data.hex()}\n")
        clear()
        H.wasi_http_types_incoming_body_stream(host, 0, body, R)
        pr(f"  stream again {memory[R]}\n")
        io.wasi_io_streams_input_stream_drop(host, stream)
        trailers = H.wasi_http_types_incoming_body_finish(host, body)
        pol = H.wasi_http_types_future_trailers_subscribe(host, trailers)
        io.wasi_io_poll_pollable_block(host, pol)
        io.wasi_io_poll_pollable_drop(host, pol)
        clear()
        H.wasi_http_types_future_trailers_get(host, 0, trailers, R)
        pr(f"  trailers tags {memory[R]} {memory[R + 8]} {memory[R + 16]} {memory[R + 24]}\n")
        if memory[R + 24] == 1:
            show_entries("trailer", u32(R + 28))
        clear()
        H.wasi_http_types_future_trailers_get(host, 0, trailers, R)
        pr(f"  trailers again {memory[R]} {memory[R + 8]}\n")
        H.wasi_http_types_future_trailers_drop(host, trailers)
        H.wasi_http_types_incoming_response_drop(host, resp)

    def _append(handle: int) -> int:
        clear()
        H.wasi_http_types_fields_append(host, 0, handle, put(b"x"), 1, put(b"y"), 1, R)
        return memory[R] * 100 + (memory[R + 1] if memory[R] else 0)

    def fetch(
        label: str,
        authority: str | None,
        path: str | None = "/",
        method: str = "GET",
        scheme: str | None = "http",
        headers: list[tuple[str, str]] | None = None,
        body: bytes | None = None,
        trailers: list[tuple[str, str]] | None = None,
        first_ns: int | None = None,
        connect_ns: int | None = None,
    ) -> None:
        pr(f"== {label}\n")
        req = make_request(method, scheme, authority, path, headers or [])
        if body is not None or trailers is not None:
            write_body(body, trailers, req, label)
        opts = 0
        if first_ns is not None or connect_ns is not None:
            opts = H.wasi_http_types_request_options_new(host)
            if first_ns is not None:
                H.wasi_http_types_request_options_set_first_byte_timeout(host, opts, 1, first_ns)
            if connect_ns is not None:
                H.wasi_http_types_request_options_set_connect_timeout(host, opts, 1, connect_ns)
        clear()
        H.wasi_http_outgoing_handler_handle(host, 0, req, 1 if opts else 0, opts, R)
        if memory[R] == 1:
            error_at(label, R + 8)
            return
        receive(label, u32(R + 8))

    me = f"127.0.0.1:{port}"
    try:
        if scenario == "main":
            fetch("hello", me, "/hello", headers=[("X-Test", "1")])
            fetch("echo", me, "/echo?a=b", "POST", headers=[("Content-Type", "text/x")], body=b"ping")
            fetch("echo with trailers", me, "/echo", "POST", body=b"data", trailers=[("X-End", "yes")])
            fetch("head", me, "/hello", "HEAD")
            fetch("chunked", me, "/chunked")
            fetch("status 404", me, "/status/404")
            fetch("informational first", me, "/info")
            fetch("until close", me, "/closeend")
            fetch("silent server", me, "/slow", first_ns=100_000_000)
            fetch("bad status", me, "/badstatus")
            fetch("transfer coding", me, "/te")
            fetch("long head", me, "/bighead")
            fetch("nothing listens", f"127.0.0.1:{dead}", "/")
            fetch("other method", me, "/echo", "PROPFIND", body=b"x")
        elif scenario == "late_errors":
            # an answer that breaks after its head: the C twin reads the answer whole, so it fails at `get`; the Python twin gives the head and fails in the body (S0013)
            fetch("truncated", me, "/truncated")
            fetch("bad chunk", me, "/badchunk")
        elif scenario in ("early_answer", "early_answer_declared"):
            # the answer is asked for while the body is still being written, and the server answers at once; then the guest finishes its body (the Python twin only: S0013)
            pr(f"== {scenario}\n")
            declared = scenario.endswith("declared")
            req = make_request("POST", "http", me, "/early", [("content-length", "4")] if declared else [])
            clear()
            H.wasi_http_types_outgoing_request_body(host, 0, req, R)
            body_h = u32(R + 4)
            clear()
            H.wasi_http_types_outgoing_body_write(host, 0, body_h, R)
            stream = u32(R + 4)
            clear()
            io.wasi_io_streams_output_stream_blocking_write_and_flush(host, 0, stream, put(b"ab"), 2, R)
            clear()
            H.wasi_http_outgoing_handler_handle(host, 0, req, 0, 0, R)
            future = u32(R + 8)
            pol = H.wasi_http_types_future_incoming_response_subscribe(host, future)
            io.wasi_io_poll_pollable_block(host, pol)  # the body is not finished: this waited for ever before
            clear()
            H.wasi_http_types_future_incoming_response_get(host, 0, future, R)
            pr(f"  get tags {memory[R]} {memory[R + 8]} {memory[R + 16]}\n")
            resp = u32(R + 24)
            pr(f"  status {H.wasi_http_types_incoming_response_status(host, resp)}\n")
            clear()
            io.wasi_io_streams_output_stream_blocking_write_and_flush(host, 0, stream, put(b"cd"), 2, R)
            pr(f"  wrote the rest {memory[R]}\n")
            io.wasi_io_streams_output_stream_drop(host, stream)
            clear()
            H.wasi_http_types_outgoing_body_finish(host, 0, body_h, 0, 0, R)
            pr(f"  finish {memory[R]}\n")
            clear()
            H.wasi_http_types_incoming_response_consume(host, 0, resp, R)
            in_body = u32(R + 4)
            clear()
            H.wasi_http_types_incoming_body_stream(host, 0, in_body, R)
            pr(f"  body {read_all(u32(R + 4)).hex()}\n")
        elif scenario in ("denied", "check"):
            fetch("denied", me, "/hello")
            fetch("allowed", "allowed.test:80", "/a")
        elif scenario == "hook":
            fetch("echo", "example.test", "/a?b=1", "POST", headers=[("X-A", "1"), ("x-a", "2")], body=b"hi")
            fetch("with trailers", "example.test", "/t", "PUT", body=b"t", trailers=[("T1", "v")])
            fetch("no authority", None, "/a")
            fetch("bad scheme", "example.test", "/a", scheme="ftp")
            fetch("bad method", "example.test", "/a", "BAD METHOD")
            fetch("uri", "bad host!", "/a")
            for i in range(len(FAILS)):
                fetch(f"fail {i}", "example.test", f"/fail/{i}")
        elif scenario == "fields":
            f = fields([("A", "1"), ("b", "2"), ("a", "3")])
            show_entries("entries", f)

            def get(handle: int, name: str) -> None:
                clear()
                H.wasi_http_types_fields_get(host, 0, handle, put(name.encode()), len(name), R)
                values = [
                    bytes(memory[u32(u32(R) + 8 * i) : u32(u32(R) + 8 * i) + u32(u32(R) + 8 * i + 4)]).decode()
                    for i in range(u32(R + 4))
                ]
                pr(f"get {name}: {values}\n")

            get(f, "a")
            get(f, "B")
            get(f, "none")
            pr(
                f"has A {H.wasi_http_types_fields_has(host, 0, f, put(b'A'), 1)} has c {H.wasi_http_types_fields_has(host, 0, f, put(b'c'), 1)}\n"
            )
            clear()
            H.wasi_http_types_fields_append(host, 0, f, put(b"c"), 1, put(b"4"), 1, R)
            pr(f"append {bytes(memory[R : R + 2]).hex()}\n")
            clear()
            H.wasi_http_types_fields_set(host, 0, f, put(b"A"), 1, blobs(["x", "y"]), 2, R)
            pr(f"set {bytes(memory[R : R + 2]).hex()}\n")
            show_entries("after set", f)
            clear()
            H.wasi_http_types_fields_delete(host, 0, f, put(b"b"), 1, R)
            pr(f"delete {bytes(memory[R : R + 2]).hex()}\n")
            clear()
            H.wasi_http_types_fields_set(host, 0, f, put(b"c"), 1, 0, 0, R)
            pr(f"set to nothing {bytes(memory[R : R + 2]).hex()}\n")
            show_entries("after delete", f)
            for label, name, value in (
                ("bad name", "a b", "x"),
                ("empty name", "", "x"),
                ("newline in value", "x", "a\nb"),
                ("nul in value", "x", "a\0b"),
                ("forbidden", "Transfer-Encoding", "chunked"),
                ("forbidden host", "HOST", "x"),
                ("tab is fine", "x", "a\tb"),
            ):
                clear()
                H.wasi_http_types_fields_append(
                    host, 0, f, put(name.encode()), len(name), put(value.encode()), len(value), R
                )
                pr(f"{label} {bytes(memory[R : R + 2]).hex()}\n")
            clear()
            H.wasi_http_types_fields_from_list(host, 0, pairs([("ok", "1"), ("connection", "x")]), 2, R)
            pr(f"from-list forbidden {bytes(memory[R : R + 8]).hex()}\n")
            clear()
            H.wasi_http_types_fields_from_list(host, 0, pairs([("o k", "1")]), 1, R)
            pr(f"from-list syntax {bytes(memory[R : R + 8]).hex()}\n")
            c = H.wasi_http_types_fields_clone(host, f)
            clear()
            H.wasi_http_types_fields_delete(host, 0, c, put(b"A"), 1, R)
            show_entries("clone", c)
            show_entries("original", f)
            req = H.wasi_http_types_outgoing_request_new(host, f)
            headers = H.wasi_http_types_outgoing_request_headers(host, req)
            pr(f"append to the headers of a request {_append(headers)}\n")
            clear()
            H.wasi_http_types_fields_delete(host, 0, headers, put(b"A"), 1, R)
            pr(f"delete from the headers of a request {bytes(memory[R : R + 2]).hex()}\n")
            clear()
            H.wasi_http_types_fields_set(host, 0, headers, put(b"A"), 1, 0, 0, R)
            pr(f"set on them {bytes(memory[R : R + 2]).hex()}\n")
            show_entries("request headers", headers)
        elif scenario == "setters":
            req = H.wasi_http_types_outgoing_request_new(host, fields([]))

            def text(label: str, getter: Any) -> None:
                clear()
                getter(host, 0, req, R)
                pr(
                    f"{label}: {bytes(memory[R : R + 16]).hex()} {bytes(memory[u32(R + 4) : u32(R + 4) + u32(R + 8)]).hex() if memory[R] else ''}\n"
                )

            text("method", H.wasi_http_types_outgoing_request_method)
            for m in ("POST", "PATCH", "get", "bad method", "", "PROPFIND"):
                pr(
                    f"set-method {quoted(m)} {H.wasi_http_types_outgoing_request_set_method(host, 0, req, *method_args(m))}\n"
                )
                text("method", H.wasi_http_types_outgoing_request_method)
            for sch in (None, "http", "https", "x-y.z+1", "1x", "a b", ""):
                pr(
                    f"set-scheme {quoted(sch)} {H.wasi_http_types_outgoing_request_set_scheme(host, 0, req, *scheme_args(sch))}\n"
                )
                clear()
                H.wasi_http_types_outgoing_request_scheme(host, 0, req, R)
                pr(f"scheme: {bytes(memory[R : R + 16]).hex()}\n")
            for path in ("/a?b=c", "*", "a", "/a b", "/\x7f", "", None):
                pr(
                    f"set-path {quoted(path)} {H.wasi_http_types_outgoing_request_set_path_with_query(host, 0, req, *opt_text(path))}\n"
                )
                text("path", H.wasi_http_types_outgoing_request_path_with_query)
            for auth in (
                "a.test",
                "a.test:8080",
                "[::1]:80",
                "[::1]",
                "a:b:c",
                "a:",
                "[::1]:",
                "a:0",
                "a:65536",
                "a:99999999999",
                "a b",
                "u@h",
                ":80",
                "",
                None,
            ):
                pr(
                    f"set-authority {quoted(auth)} {H.wasi_http_types_outgoing_request_set_authority(host, 0, req, *opt_text(auth))}\n"
                )
                text("authority", H.wasi_http_types_outgoing_request_authority)
            clear()
            H.wasi_http_types_outgoing_request_body(host, 0, req, R)
            first = memory[R]
            clear()
            H.wasi_http_types_outgoing_request_body(host, 0, req, R)
            pr(f"body once {first} then {memory[R]}\n")
            opts = H.wasi_http_types_request_options_new(host)
            for which in ("connect", "first_byte", "between_bytes"):
                getter = getattr(H, f"wasi_http_types_request_options_{which}_timeout")
                setter = getattr(H, f"wasi_http_types_request_options_set_{which}_timeout")
                clear()
                getter(host, 0, opts, R)
                pr(f"{which} {bytes(memory[R : R + 16]).hex()}\n")
                pr(f"set {which} {setter(host, opts, 1, 123456789012)}\n")
                clear()
                getter(host, 0, opts, R)
                pr(f"{which} {bytes(memory[R : R + 16]).hex()}\n")
                setter(host, opts, 0, 5)
                clear()
                getter(host, 0, opts, R)
                pr(f"{which} {bytes(memory[R : R + 16]).hex()}\n")
            resp_h = H.wasi_http_types_outgoing_response_new(host, fields([]))
            pr(f"status {H.wasi_http_types_outgoing_response_status_code(host, resp_h)}\n")
            for code in (99, 100, 599, 600, 200):
                pr(f"set-status {code} {H.wasi_http_types_outgoing_response_set_status_code(host, resp_h, code)}\n")
            pr(f"status {H.wasi_http_types_outgoing_response_status_code(host, resp_h)}\n")
        elif scenario == "limits":
            fetch("short body", "example.test", "/x", "POST", headers=[("content-length", "5")], body=b"ab")
            fetch("right length", "example.test", "/x", "POST", headers=[("content-length", "2")], body=b"ab")
            fetch("not a number", "example.test", "/x", "POST", headers=[("content-length", "x")], body=b"ab")
        elif scenario == "server":
            req = H.incoming_request_new(
                host, "PUT", "/x?y=1", "https", "svc.test", [("A", b"1"), ("a", b"2")], b"payload", [("t", b"v")]
            )
            clear()
            H.wasi_http_types_incoming_request_method(host, 0, req, R)
            pr(f"method {bytes(memory[R : R + 4]).hex()}\n")
            for label, fn in (
                ("path", H.wasi_http_types_incoming_request_path_with_query),
                ("scheme", H.wasi_http_types_incoming_request_scheme),
                ("authority", H.wasi_http_types_incoming_request_authority),
            ):
                clear()
                fn(host, 0, req, R)
                pr(f"{label} {bytes(memory[R : R + 16]).hex()}\n")
            hh = H.wasi_http_types_incoming_request_headers(host, req)
            show_entries("header", hh)
            pr(f"append to them {_append(hh)}\n")
            clear()
            H.wasi_http_types_incoming_request_consume(host, 0, req, R)
            body = u32(R + 4)
            clear()
            H.wasi_http_types_incoming_request_consume(host, 0, req, R)
            pr(f"consume again {memory[R]}\n")
            clear()
            H.wasi_http_types_incoming_body_stream(host, 0, body, R)
            stream = u32(R + 4)
            pr(f"body {read_all(stream).hex()}\n")
            io.wasi_io_streams_input_stream_drop(host, stream)
            trailers = H.wasi_http_types_incoming_body_finish(host, body)
            clear()
            H.wasi_http_types_future_trailers_get(host, 0, trailers, R)
            show_entries("trailer", u32(R + 28))
            param, out_param = H.response_outparam_new(host)
            resp_h = H.wasi_http_types_outgoing_response_new(host, fields([("Content-Type", "text/plain")]))
            pr(f"set-status {H.wasi_http_types_outgoing_response_set_status_code(host, resp_h, 418)}\n")
            clear()
            H.wasi_http_types_outgoing_response_body(host, 0, resp_h, R)
            body_h = u32(R + 4)
            clear()
            H.wasi_http_types_outgoing_response_body(host, 0, resp_h, R)
            pr(f"body again {memory[R]}\n")
            clear()
            H.wasi_http_types_outgoing_body_write(host, 0, body_h, R)
            stream = u32(R + 4)
            clear()
            H.wasi_http_types_outgoing_body_write(host, 0, body_h, R)
            pr(f"write again {memory[R]}\n")
            clear()
            io.wasi_io_streams_output_stream_blocking_write_and_flush(host, 0, stream, put(b"tea"), 3, R)
            io.wasi_io_streams_output_stream_drop(host, stream)
            H.wasi_http_types_response_outparam_set(host, 0, param, 0, resp_h, 0, 0, 0, 0, 0, 0)
            pr(
                f"before finish: done {int(out_param.done)} finished {int(out_param.response.finished) if out_param.response else -1}\n"
            )
            clear()
            H.wasi_http_types_outgoing_body_finish(host, 0, body_h, 0, 0, R)
            r = out_param.response
            assert r is not None
            pr(
                f"embedder sees status {r.status} body {r.body_bytes().hex()} finished {int(r.finished)} trailers {'None' if r.trailers is None else 'fields'}\n"
            )
            for k, v in r.headers.entries:
                pr(f"  {k}: {v.decode()}\n")
            param2, out2 = H.response_outparam_new(host)
            H.wasi_http_types_response_outparam_drop(host, param2)
            pr(f"dropped without a response: {describe(out2.error)}\n")
        elif scenario == "flat_errors":
            cases: list[tuple[int, ...]] = [
                (6,),
                (1, 1, put(b"ab"), 2, 1, 7),
                (1, 0, 0, 0, 0, 0),
                (14, 1, 42, 1, put(b"xyz"), 3),
                (17, 1, (1 << 40) + 5),
                (17, 0, 0),
                (22, 1, 1, put(b"nm"), 2, 1, 99),
                (22, 0),
                (22, 1, 0, 0, 0, 1, 5),
                (38, 1, put(b"oops"), 4),
                (38, 0, 0, 0),
                (31, 1, put(b"gzip"), 4),
            ]
            for c in cases:
                param, o = H.response_outparam_new(host)
                padded = (*c, *([0] * (7 - len(c))))
                H.wasi_http_types_response_outparam_set(host, 0, param, 1, *padded)
                pr(f"{c[0]}: {describe(o.error)}\n")
                clear()
                if o.error is not None:
                    H._store_error_code(host, 0, R, o.error)
                    pr(f"  stored {bytes(memory[R : R + 32]).hex()}\n")
        elif scenario == "finish_alive":
            req = H.wasi_http_types_outgoing_request_new(host, fields([]))
            clear()
            H.wasi_http_types_outgoing_request_body(host, 0, req, R)
            body_h = u32(R + 4)
            clear()
            H.wasi_http_types_outgoing_body_write(host, 0, body_h, R)
            clear()
            H.wasi_http_types_outgoing_body_finish(host, 0, body_h, 0, 0, R)
        elif scenario == "bad_handle":
            H.wasi_http_types_fields_entries(host, 0, 4242, R)
    except Trap:
        out.append("TRAP\n")
    return "".join(out)


def c_output(scenario: str, port: int, dead: int, binary: Path) -> str:
    out = subprocess.run(
        [str(binary), scenario, str(port), str(dead)], capture_output=True, text=True, check=True, timeout=120
    ).stdout
    return "".join(("TRAP\n" if line.startswith("TRAP") else line + "\n") for line in out.splitlines())


# a small server: one request for each connection, what is asked is echoed or one of the answers that go wrong


def read_request(conn: socket.socket) -> tuple[str, str, list[tuple[str, str]], bytes, list[tuple[str, str]]]:
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = conn.recv(65536)
        if not chunk:
            break
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    method, target, _ = lines[0].split(" ", 2)
    headers = [(k.lower(), v.strip()) for k, _, v in (x.partition(":") for x in lines[1:])]
    trailers: list[tuple[str, str]] = []
    names = dict(headers)
    if names.get("transfer-encoding") == "chunked":
        body = b""
        while True:
            while b"\r\n" not in rest:
                rest += conn.recv(65536)
            size_line, _, rest = rest.partition(b"\r\n")
            size = int(size_line, 16)
            while len(rest) < size + 2:
                rest += conn.recv(65536)
            if size == 0:
                break
            body += rest[:size]
            rest = rest[size + 2 :]
        tail = rest
        while b"\r\n\r\n" not in b"\r\n" + tail and tail != b"\r\n":
            tail += conn.recv(65536)
        for line in tail.decode("latin-1").split("\r\n"):
            if line:
                k, _, v = line.partition(":")
                trailers.append((k.lower(), v.strip()))
    else:
        n = int(names.get("content-length", "0"))
        while len(rest) < n:
            rest += conn.recv(65536)
        body = rest[:n]
    return method, target, headers, body, trailers


def respond(conn: socket.socket, port: int, framing: bool = True) -> None:
    try:
        method, target, headers, body, trailers = read_request(conn)
        path = target.split("?")[0]
        host_seen = dict(headers).get("host", "").replace(str(port), "PORT")
        if path == "/hello":
            payload = b"hello world"
            head = b"HTTP/1.1 200 OK\r\ncontent-type: text/plain\r\nX-Dup: a\r\nx-dup: b\r\ncontent-length: 11\r\n\r\n"
            conn.sendall(head + (b"" if method == "HEAD" else payload))
        elif path == "/echo":
            seen = f"{method} {target} host={host_seen} len={len(body)}"
            if (
                framing
            ):  # how the client sent the body (the conformance server leaves it out: Wasmtime always sends chunks)
                seen += f" trailers={trailers} te={dict(headers).get('transfer-encoding')}"
            head = f"HTTP/1.1 200 OK\r\ncontent-length: {len(body)}\r\nx-seen: {seen}\r\n\r\n".encode("latin-1")
            conn.sendall(head + body)
        elif path == "/chunked":
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n3;ext=1\r\ncde\r\n0\r\nX-Trailer: t1\r\nx-two: 2\r\n\r\n"
            )
        elif path.startswith("/status/"):
            conn.sendall(f"HTTP/1.1 {path[8:]} Not Found\r\ncontent-length: 0\r\n\r\n".encode())
        elif path == "/info":
            conn.sendall(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nok")
        elif path == "/closeend":
            conn.sendall(b"HTTP/1.1 200 OK\r\n\r\nuntil close")
        elif path == "/truncated":
            conn.sendall(b"HTTP/1.1 200 OK\r\ncontent-length: 100\r\n\r\n0123456789")
        elif path == "/slow":
            time.sleep(1.5)
        elif path == "/badstatus":
            conn.sendall(b"garbage\r\n\r\n")
        elif path == "/te":
            conn.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nxx")
        elif path == "/badchunk":
            conn.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n")
        elif path == "/drip":  # the head and two pieces at once, a pause, two more and a trailer
            conn.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTrailer: x-t\r\n\r\n2\r\nab\r\n")
            time.sleep(1.5)
            conn.sendall(b"2\r\ncd\r\n0\r\nx-t: 1\r\n\r\n")
        elif path == "/stall":  # a piece, and then nothing for longer than the test waits
            conn.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n")
            time.sleep(3)
        elif path == "/reset":  # ten bytes of a hundred, and then the connection is reset
            conn.sendall(b"HTTP/1.1 200 OK\r\ncontent-length: 100\r\n\r\n0123456789")
            time.sleep(0.2)
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        elif path == "/endless":  # never ends: a piece every 20 ms until the client goes
            conn.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            while True:
                conn.sendall(b"2\r\nxx\r\n")
                time.sleep(0.02)
        elif path == "/bighead":
            conn.sendall(b"HTTP/1.1 200 OK\r\n" + b"X-Pad: " + b"a" * 70000 + b"\r\n\r\n")
        else:
            conn.sendall(b"HTTP/1.1 404 Not Found\r\ncontent-length: 0\r\n\r\n")
    except OSError:
        pass
    finally:
        conn.close()


@pytest.fixture(scope="module")
def server() -> Iterator[int]:
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(16)
    port = listener.getsockname()[1]

    def accept_loop() -> None:
        while True:
            try:
                conn, _ = listener.accept()
            except OSError:
                return
            threading.Thread(target=respond, args=(conn, port), daemon=True).start()

    threading.Thread(target=accept_loop, daemon=True).start()
    yield port
    listener.close()


@pytest.fixture(scope="module")
def dead_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("p2http") / ("driver.exe" if sys.platform == "win32" else "driver")
    compile_c(DRIVER, binary, [C_P2])
    return binary


@pytest.mark.parametrize("scenario", SCENARIOS)
def test_c_and_python_agree(driver: Path, server: int, dead_port: int, scenario: str) -> None:
    assert python_output(scenario, server, dead_port) == c_output(scenario, server, dead_port, driver)


def lines_of(scenario: str, port: int, dead: int) -> list[str]:
    return python_output(scenario, port, dead).splitlines()


def test_answers(server: int, dead_port: int) -> None:
    lines = lines_of("main", server, dead_port)
    text = "\n".join(lines)
    assert "== hello\n" in text + "\n" or "== hello" in lines
    hello = text.split("== echo\n")[0]
    assert "  status 200" in hello and f"  body {b'hello world'.hex()}" in hello
    assert "  header x-dup: a" in hello and "  header x-dup: b" in hello  # two fields of one name
    assert "  append to the headers of an answer 102" in hello  # immutable
    assert "  consume again 1" in hello and "  stream again 1" in hello
    chunked = text.split("== chunked\n")[1].split("== ")[0]
    assert f"  body {b'abcde'.hex()}" in chunked
    assert "  trailer x-trailer: t1" in chunked and "  trailer x-two: 2" in chunked
    assert "  trailers again 1 1" in chunked  # taken
    echo = text.split("== echo\n")[1].split("== ")[0]
    assert f"  body {b'ping'.hex()}" in echo
    assert "x-seen: POST /echo?a=b host=127.0.0.1:PORT len=4 trailers=[] te=None" in echo
    with_trailers = text.split("== echo with trailers\n")[1].split("== ")[0]
    assert "te=chunked" in with_trailers and "('x-end', 'yes')" in with_trailers
    head = text.split("== head\n")[1].split("== ")[0]
    assert "  header content-length: 11" in head and f"  body {''}" in head
    assert "  status 404" in text and "  status 200" in text.split("== informational first\n")[1]
    assert f"  body {b'until close'.hex()}" in text
    assert "  body 6f6b" in text  # "ok" after the 100


def tag_of(output: str, label: str) -> int:
    line = next(x for x in output.splitlines() if x.startswith(f"{label} error:"))
    return int(line.split("error:")[1][:2], 16)


def test_error_codes(server: int, dead_port: int) -> None:
    out = python_output("main", server, dead_port)
    names = [c[0] for c in H._CASES]
    assert names[tag_of(out, "silent server")] == "connection-read-timeout"
    assert names[tag_of(out, "bad status")] == "HTTP-protocol-error"
    assert names[tag_of(out, "transfer coding")] == "HTTP-response-transfer-coding"
    assert names[tag_of(out, "long head")] == "HTTP-response-header-section-size"
    assert names[tag_of(out, "nothing listens")] == "connection-refused"
    assert "x-seen: PROPFIND /echo" in out  # any token is a method
    coding = next(x for x in out.splitlines() if x.startswith("transfer coding error:")).split("error:")[1]
    assert len(coding) == 64 and coding[16:24] == "01000000"  # the payload is a some(string) at offset 8


def test_denied_by_default(server: int, dead_port: int) -> None:
    out = python_output("denied", server, dead_port)
    assert names_tag(out, "denied") == "HTTP-request-denied"
    assert names_tag(python_output("check", server, dead_port), "denied") == "HTTP-request-denied"
    assert "  status 201" in python_output("check", server, dead_port)  # the check let allowed.test through


def names_tag(out: str, label: str) -> str:
    return H._CASES[tag_of(out, label)][0]


def test_the_transport_of_the_embedder(server: int, dead_port: int) -> None:
    out = python_output("hook", server, dead_port)
    assert names_tag(out, "no authority") == "HTTP-request-URI-invalid"
    assert names_tag(out, "bad scheme") == "HTTP-request-URI-invalid"
    section = out.split("== bad method\n")[1].split("== ")[0]
    assert section.startswith("  set-method 1\n")  # refused at once, so the request goes as the GET it was
    section = out.split("== uri\n")[1].split("== ")[0]
    assert section.startswith("  set-method 0\n  set-scheme 0\n  set-authority 1\n")
    assert names_tag(out, "uri") == "HTTP-request-URI-invalid"  # and with no authority it cannot be sent
    for i, ec in enumerate(FAILS):
        assert names_tag(out, f"fail {i}") == ec.name
    assert "  header x-upper: One" in out and "  header x-dup: a" in out and "  header x-dup: b" in out  # lower case
    assert "  trailer x-trailer: t" in out
    body = bytes.fromhex(next(x for x in out.splitlines() if x.startswith("  body ")).split()[1]).decode()
    assert (
        body.startswith("POST http example.test /a?b=1")
        and "x-a: 1" in body
        and "x-a: 2" in body
        and body.endswith("body=hi")
    )


def test_limits_of_content_length(server: int, dead_port: int) -> None:
    out = python_output("limits", server, dead_port)
    assert names_tag(out, "short body finish") == "HTTP-request-body-size"
    assert "  status 201" in out  # the right length goes through
    assert out.count("status 201") == 2  # "right length" and "not a number" (nothing to compare with)


def test_the_server_side(server: int, dead_port: int) -> None:
    out = python_output("server", server, dead_port)
    assert "embedder sees status 418 body 746561 finished 1 trailers None" in out
    assert "  content-type: text/plain" in out
    assert "before finish: done 1 finished 0" in out  # the guest may go on writing after it has set the answer
    assert "dropped without a response: 38/1/response-outparam dropped without a response" in out
    assert "append to them 102" in out and "consume again 1" in out and "body again 1" in out and "write again 1" in out


def test_error_codes_come_back_as_flat_arguments(server: int, dead_port: int) -> None:
    out = python_output("flat_errors", server, dead_port)
    assert "1: 1/1/ab/7" in out
    assert "17: 17/1/1099511627781" in out
    assert "22: 22/1/nm/99" in out
    assert "22: 22/0/-/-" in out
    assert "38: 38/1/oops" in out
    assert "14: 14/1/42/xyz" in out
    # what is stored for the guest is the 32-byte form
    assert f"  stored {'06' + '00' * 31}" in out


def test_traps(server: int, dead_port: int) -> None:
    assert python_output("finish_alive", server, dead_port).splitlines()[-1] == "TRAP"
    assert python_output("bad_handle", server, dead_port).splitlines()[-1] == "TRAP"


def test_the_table_of_error_codes_is_the_same_in_c(driver: Path) -> None:
    kinds = {"": 0, "n8": 2, "n16": 3, "n32": 4, "n64": 5, "str": 6}  # the enum of the header: NONE is 0 and N8 is 1
    kinds = {"": 0, "n8": 1, "n16": 2, "n32": 3, "n64": 4, "str": 5}
    c_table = [
        tuple(map(int, line.split()))
        for line in subprocess.run(
            [str(driver), "cases"], capture_output=True, text=True, check=True
        ).stdout.splitlines()
    ]
    for i, (_, outer, leaves) in enumerate(H._CASES):
        padded = [*leaves, ""][:2] if len(leaves) < 2 else list(leaves)
        padded = list(leaves) + [""] * (2 - len(leaves))
        assert c_table[i] == (i, int(outer), len(leaves), kinds[padded[0]], kinds[padded[1]]), H._CASES[i]


def test_the_table_of_error_codes_is_the_wit() -> None:
    assert len(H._CASES) == 39
    assert [c[0] for c in H._CASES][:3] == ["DNS-timeout", "DNS-error", "destination-not-found"]
    assert H._CASES[22] == ("HTTP-request-header-size", True, ("str", "n32"))
    assert H._CASES[-1] == ("internal-error", False, ("str",))


# the answer is read as it comes (S0013): the head before the body, the body in pieces, the failures after the bytes that came before them


def open_answer(
    port: int, path: str, between_bytes_ns: int | None = None
) -> tuple[int, list[tuple[str, bytes]], H._BodySource]:  # pyright: ignore[reportPrivateUsage]
    request = H.HttpRequest("GET", "http", f"127.0.0.1:{port}", path, [], between_bytes_ns=between_bytes_ns)
    return H._default_open(request)  # pyright: ignore[reportPrivateUsage]


def wait_ready(source: H._BodySource, seconds: float = 3.0) -> None:  # pyright: ignore[reportPrivateUsage]
    deadline = time.monotonic() + seconds
    while not source.ready():
        assert time.monotonic() < deadline, "the body did not come"
        time.sleep(0.005)


def test_the_head_comes_before_the_body_and_a_read_never_waits(server: int) -> None:
    started = time.monotonic()
    status, headers, source = open_answer(server, "/drip")
    assert status == 200 and ("trailer", b"x-t") in headers
    assert time.monotonic() - started < 1.0  # the rest of the body is a pause of 1.5 s away
    wait_ready(source)
    assert source.read(100) == b"ab"
    assert source.read(100) == b""  # nothing now: not closed, and no wait
    assert not source.ready()
    wait_ready(source)
    assert source.read(100) == b"cd"
    with pytest.raises(StreamClosed):
        source.read(100)
    assert source.settled() and source.error is None
    trailers = source.trailers()
    assert trailers is not None and [(n, v) for n, v in trailers.entries] == [("x-t", b"1")]
    assert source.answer.sock.fileno() == -1  # the socket is closed when the body ended


def test_a_stall_between_bytes_fails_after_the_bytes_that_came(server: int) -> None:
    _, _, source = open_answer(server, "/stall", between_bytes_ns=300_000_000)
    wait_ready(source)
    assert source.read(100) == b"ab"
    wait_ready(source)  # the failure is a thing to be read, too
    with pytest.raises(StreamFailed) as failure:
        source.read(100)
    assert (
        isinstance(failure.value.http_code, H.ErrorCode) and failure.value.http_code.name == "connection-read-timeout"
    )
    assert source.answer.sock.fileno() == -1


def test_a_reset_in_the_middle_gives_the_bytes_and_then_the_error(server: int) -> None:
    _, _, source = open_answer(server, "/reset")
    got = b""
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        try:
            got += source.read(100)
        except StreamFailed as failure:
            assert got == b"0123456789"
            assert isinstance(failure.http_code, H.ErrorCode) and failure.http_code.name in (
                "connection-terminated",
                "HTTP-response-incomplete",
            )
            return
        time.sleep(0.005)
    raise AssertionError("no failure")


def test_the_total_is_a_limit_of_the_body_not_of_the_head(server: int, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(H, "MAX_BODY", 4)  # /chunked is "ab" and "cde": 5 bytes
    status, _, source = open_answer(server, "/chunked")
    assert status == 200
    got = b""
    with pytest.raises(StreamFailed) as failure:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            got += source.read(100)
            time.sleep(0.005)
    code = failure.value.http_code
    assert got == b"ab" and isinstance(code, H.ErrorCode) and code.name == "HTTP-response-body-size"


def test_a_body_that_never_ends_is_let_go(server: int) -> None:
    _, _, source = open_answer(server, "/endless")
    for _ in range(3):
        wait_ready(source)
        assert source.read(100).startswith(b"xx")
    source.release()
    assert source.answer.sock.fileno() == -1  # the socket is closed, and the server stops sending when it finds out


def test_the_whole_body_of_default_send_is_the_same_as_before(server: int) -> None:
    response = H.default_send(H.HttpRequest("GET", "http", f"127.0.0.1:{server}", "/chunked", []))
    assert response.status == 200 and response.body == b"abcde"
    assert response.trailers == [("x-trailer", b"t1"), ("x-two", b"2")]
    assert all(name != "transfer-encoding" for name, _ in response.headers)


# the request goes out while the body is written (S0013, section 8, item 1): the answer may come first


def serve_early(declared: bool) -> tuple[int, dict[str, bytes], threading.Thread]:
    """One connection: reads the head and the first bytes of the body, answers at once, then reads what is left of the request."""
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    seen: dict[str, bytes] = {}

    def run() -> None:
        conn, _ = listener.accept()
        conn.settimeout(10)
        buf = b""
        while b"\r\n\r\n" not in buf or b"ab" not in buf.split(b"\r\n\r\n", 1)[1]:
            buf += conn.recv(65536)
        conn.sendall(b"HTTP/1.1 200 OK\r\ncontent-length: 2\r\nconnection: close\r\n\r\nok")
        end = b"abcd" if declared else b"0\r\n\r\n"
        try:
            while not buf.endswith(end):
                chunk = conn.recv(65536)
                if not chunk:
                    break
                buf += chunk
        except OSError:
            pass
        seen["raw"] = buf
        conn.close()
        listener.close()

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    return listener.getsockname()[1], seen, thread


@pytest.mark.parametrize("scenario", ["early_answer", "early_answer_declared"])
def test_the_answer_may_come_before_the_body_is_finished(scenario: str) -> None:
    port, seen, server_thread = serve_early(scenario.endswith("declared"))
    result: list[str] = []
    guest = threading.Thread(target=lambda: result.append(python_output(scenario, port, 1)), daemon=True)
    guest.start()
    guest.join(20)
    assert not guest.is_alive(), (
        "the guest waited for ever for the answer: the request did not go out before its body ended"
    )
    server_thread.join(10)
    out = result[0]
    assert "  status 200" in out and "  wrote the rest 0" in out and "  finish 0" in out
    assert f"  body {b'ok'.hex()}" in out
    head, _, body = seen["raw"].partition(b"\r\n\r\n")
    lowered = head.lower()
    if scenario.endswith("declared"):
        assert b"content-length: 4" in lowered and b"transfer-encoding" not in lowered
        assert body == b"abcd"  # the length the guest declared, and the bytes as they are
    else:
        assert b"transfer-encoding: chunked" in lowered and b"content-length" not in lowered
        assert body == b"2\r\nab\r\n2\r\ncd\r\n0\r\n\r\n"  # a chunk for each write, then the end
