"""wasi:http (63 imports): `types` (62) and `outgoing-handler` (1) (the twin of `include/fusedwasi/p2/wasi_p2_http.h`). `incoming-handler` is an export of a component, not an import.

**An outgoing request is refused (`HTTP-request-denied`) unless the caller says so**: `Host(http=True)` allows all, `Host(http_check=f)` decides for each one (`f("https", "example.com:443")`),
`Host(http_send=g)` replaces the transport (`g(HttpRequest) -> HttpResponse`, a failure is a raised `ErrorCode`). The transport of the host is a small HTTP/1.1 client over a socket
(`Connection: close`): the protocol is done by `h11` (a dependency of the Python twin, pure Python), the socket, `ssl`, the timeouts, the limits and the `ErrorCode` are ours. The C twin has the same
shape written by hand; only Python has TLS built in (`ssl`); in C `https` is `internal-error` unless `http_send` is set. What a guest sees is shaped by these choices:

* the answer is read whole before the guest sees it (a body is a stream over bytes in memory, at most `MAX_BODY`; the end is `closed`, and `http-error-code` is `none`: a stream of a body never fails);
* a request is sent when the guest first asks for the answer (`get`, `ready` or `block` of the future), after the body is finished; a future dropped before that sends nothing;
* the body of a request is collected and sent with `Content-Length` (with `Transfer-Encoding: chunked` if trailers are given); if the guest set `content-length`, a body of another size is `HTTP-request-body-size`;
* names of fields are kept in lower case; `connection`, `keep-alive`, `proxy-connection`, `transfer-encoding`, `upgrade`, `host`, `http2-settings` and `te` are `forbidden`; the headers of a request, a response or an incoming
  message are `immutable`; the order of checks is immutable, invalid-syntax, forbidden;
* the timeouts of `request-options` default to 600 s (as in Wasmtime): connect → `connection-timeout`, first byte and between bytes → `connection-read-timeout`;
* an unset `scheme` is `https`; an unset `path-with-query` is `/`; an unset `authority` is `HTTP-request-URI-invalid`.

The server side (a component that exports `incoming-handler`) is made of the embedder: `incoming_request_new` gives the guest a request and `response_outparam_new` the place where it puts its answer.

Layouts (Canonical ABI): `method` 12 bytes (tag at 0, string at 4), `scheme` the same, `option<scheme>` 16 bytes (tag 0, scheme at 4), `option<string>` 12 bytes. `error-code` is 32 bytes aligned to 8:
the tag at 0 and the payload at 8 (see `CASES`: each payload is one or two options of a number or a string, and `HTTP-request-header-size` has them in an option of a record). `result<T, error-code>` is 8 + 32
(the tag at 0, T or the error at 8); `result<_, header-error>`: tag 0, error 1; `result<own T>`: tag 0, handle 4. The answer of `future-incoming-response.get` (`option<result<result<T, error-code>>>`)
is 56 bytes: option tag 0, tag of the outer result 8, tag of the inner 16, handle 24 or error-code 24.
"""

from __future__ import annotations

import contextlib
import errno
import re
import socket
import ssl
import time
from collections.abc import Iterator, Sequence
from dataclasses import dataclass

import h11

from .wasi_p2_core import (
    KIND_ERROR,
    KIND_FIELDS,
    KIND_FUTURE_INCOMING_RESPONSE,
    KIND_FUTURE_TRAILERS,
    KIND_INCOMING_BODY,
    KIND_INCOMING_REQUEST,
    KIND_INCOMING_RESPONSE,
    KIND_OUTGOING_BODY,
    KIND_OUTGOING_REQUEST,
    KIND_OUTGOING_RESPONSE,
    KIND_OUTPUT_STREAM,
    KIND_POLLABLE,
    KIND_REQUEST_OPTIONS,
    KIND_RESPONSE_OUTPARAM,
    Guest,
    Pollable,
    State,
    _alloc,
    _load_bytes,
    _load_u32,
    _store_bytes,
    _store_u8,
    _store_u32,
    _store_u64,
    _table_alive,
    _table_drop,
    _table_get,
    _table_new,
    _trap,
)
from .wasi_p2_io import (
    BufferOutput,
    InputStream,
    MemoryInput,
    StreamClosed,
    StreamFailed,
    _input_stream_new,
    _output_stream_new,
)

MAX_BODY = 1 << 28  # the most a body of a request or of an answer can have
MAX_HEAD = 65536  # the most the head of an answer can have
_DEFAULT_TIMEOUT_NS = 600 * 1_000_000_000

# the error-code: (name, payload is in an option, kinds of the leaves); a leaf is an option of "n8", "n16", "n32", "n64" or a "str"
_CASES: tuple[tuple[str, bool, tuple[str, ...]], ...] = (
    ("DNS-timeout", False, ()),
    ("DNS-error", False, ("str", "n16")),
    ("destination-not-found", False, ()),
    ("destination-unavailable", False, ()),
    ("destination-IP-prohibited", False, ()),
    ("destination-IP-unroutable", False, ()),
    ("connection-refused", False, ()),
    ("connection-terminated", False, ()),
    ("connection-timeout", False, ()),
    ("connection-read-timeout", False, ()),
    ("connection-write-timeout", False, ()),
    ("connection-limit-reached", False, ()),
    ("TLS-protocol-error", False, ()),
    ("TLS-certificate-error", False, ()),
    ("TLS-alert-received", False, ("n8", "str")),
    ("HTTP-request-denied", False, ()),
    ("HTTP-request-length-required", False, ()),
    ("HTTP-request-body-size", False, ("n64",)),
    ("HTTP-request-method-invalid", False, ()),
    ("HTTP-request-URI-invalid", False, ()),
    ("HTTP-request-URI-too-long", False, ()),
    ("HTTP-request-header-section-size", False, ("n32",)),
    ("HTTP-request-header-size", True, ("str", "n32")),
    ("HTTP-request-trailer-section-size", False, ("n32",)),
    ("HTTP-request-trailer-size", False, ("str", "n32")),
    ("HTTP-response-incomplete", False, ()),
    ("HTTP-response-header-section-size", False, ("n32",)),
    ("HTTP-response-header-size", False, ("str", "n32")),
    ("HTTP-response-body-size", False, ("n64",)),
    ("HTTP-response-trailer-section-size", False, ("n32",)),
    ("HTTP-response-trailer-size", False, ("str", "n32")),
    ("HTTP-response-transfer-coding", False, ("str",)),
    ("HTTP-response-content-coding", False, ("str",)),
    ("HTTP-response-timeout", False, ()),
    ("HTTP-upgrade-failed", False, ()),
    ("HTTP-protocol-error", False, ()),
    ("loop-detected", False, ()),
    ("configuration-error", False, ()),
    ("internal-error", False, ("str",)),
)
_CASE_INDEX = {c[0]: i for i, c in enumerate(_CASES)}
_LEAF = {
    "n8": (2, 1),
    "n16": (4, 2),
    "n32": (8, 4),
    "n64": (16, 8),
    "str": (12, 4),
}  # size and alignment of an option of it

_STD_METHODS = ("GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH")
_FORBIDDEN = frozenset(
    {"connection", "keep-alive", "proxy-connection", "transfer-encoding", "upgrade", "host", "http2-settings", "te"}
)
_TOKEN = re.compile(rb"[!#$%&'*+\-.^_`|~0-9A-Za-z]+")
_SCHEME = re.compile(r"[A-Za-z][A-Za-z0-9+.\-]*")
_INVALID_SYNTAX, _FORBIDDEN_HEADER, _IMMUTABLE = 0, 1, 2  # header-error


class ErrorCode(Exception):
    """A `wasi:http/types.error-code`: `ErrorCode("connection-refused")`, `ErrorCode("internal-error", "message")`, `ErrorCode("DNS-error", "NXDOMAIN", None)`.

    `values` has one item for each leaf of the payload: None (the option is none), an int or a str. `present` is for `HTTP-request-header-size`, whose payload is in an option.
    """

    def __init__(self, name: str, *values: int | str | None, present: bool = True) -> None:
        if name not in _CASE_INDEX:
            raise ValueError(f"no error-code {name}")
        kinds = _CASES[_CASE_INDEX[name]][2]
        if len(values) > len(kinds):
            raise ValueError(f"{name} has {len(kinds)} values")
        self.name = name
        self.values = tuple(values) + (None,) * (len(kinds) - len(values))
        self.present = present
        super().__init__(name, *self.values)

    @property
    def tag(self) -> int:
        return _CASE_INDEX[self.name]

    def __eq__(self, other: object) -> bool:
        return isinstance(other, ErrorCode) and (self.name, self.values, self.present) == (
            other.name,
            other.values,
            other.present,
        )

    def __hash__(self) -> int:
        return hash((self.name, self.values, self.present))

    def __repr__(self) -> str:
        return f"ErrorCode({self.name!r}{''.join(', ' + repr(v) for v in self.values)})"


def _offsets(outer: bool, kinds: tuple[str, ...]) -> list[int]:
    """Where the leaves are, from the start of the payload."""
    pos = 0
    if outer:
        align = max(_LEAF[k][1] for k in kinds)
        pos = (1 + align - 1) // align * align
    offs: list[int] = []
    for k in kinds:
        size, align = _LEAF[k]
        pos = (pos + align - 1) // align * align
        offs.append(pos)
        pos += size
    return offs


def _store_string(guest: Guest, comp: int, at: int, text: str | bytes) -> None:
    data = text.encode() if isinstance(text, str) else text
    ptr = _alloc(guest, comp, 1, len(data))
    _store_bytes(guest, comp, ptr, data)
    _store_u32(guest, comp, at, ptr)
    _store_u32(guest, comp, at + 4, len(data))


def _store_opt_string(guest: Guest, comp: int, at: int, text: str | None) -> None:
    _store_u8(guest, comp, at, 0 if text is None else 1)
    if text is not None:
        _store_string(guest, comp, at + 4, text)


def _store_error_code(guest: Guest, comp: int, ptr: int, ec: ErrorCode) -> None:
    """Writes an `error-code` (32 bytes) at `ptr`."""
    _, outer, kinds = _CASES[_CASE_INDEX[ec.name]]
    _store_u8(guest, comp, ptr, _CASE_INDEX[ec.name])
    base = ptr + 8
    if outer:
        _store_u8(guest, comp, base, 1 if ec.present else 0)
        if not ec.present:
            return
    for kind, off, value in zip(kinds, _offsets(outer, kinds), ec.values, strict=True):
        at = base + off
        if kind == "str":
            _store_opt_string(guest, comp, at, None if value is None else str(value))
            continue
        _store_u8(guest, comp, at, 0 if value is None else 1)
        if value is not None:
            width = {"n8": 1, "n16": 2, "n32": 4, "n64": 8}[kind]
            data = int(value).to_bytes(width, "little")
            _store_bytes(
                guest, comp, at + _LEAF[kind][1], data
            )  # the value follows the tag, at the alignment of the number


def _lift_error_code_flat(guest: Guest, comp: int, flat: Sequence[int]) -> ErrorCode:
    """An `error-code` that came as flat arguments: the tag, then the numbers of the payload (a string is a pointer and a length)."""
    tag = flat[0] & 0xFF if flat[0] < 256 else -1
    if not 0 <= tag < len(_CASES):
        _trap(guest, f"bad error-code {flat[0]}")
    name, outer, kinds = _CASES[tag]
    pos = 1
    present = True
    if outer:
        present = bool(flat[pos] & 0xFF)
        pos += 1
    values: list[int | str | None] = []
    for kind in kinds:
        if not present:
            values.append(None)
            continue
        has = flat[pos] & 0xFF
        if kind == "str":
            ptr, n = flat[pos + 1] & 0xFFFFFFFF, flat[pos + 2] & 0xFFFFFFFF
            values.append(_load_bytes(guest, comp, ptr, n).decode("utf-8", "replace") if has else None)
            pos += 3
        else:
            width = {"n8": 8, "n16": 16, "n32": 32, "n64": 64}[kind]
            values.append(flat[pos + 1] & ((1 << width) - 1) if has else None)
            pos += 2
    return ErrorCode(name, *values, present=present)


# fields


class Fields:
    """The `fields` resource: names in lower case, values as bytes; `immutable` once something owns them."""

    def __init__(self, entries: Sequence[tuple[str, bytes]] = (), immutable: bool = False) -> None:
        self.entries: list[tuple[str, bytes]] = list(entries)
        self.immutable = immutable

    def get(self, name: str) -> list[bytes]:
        name = name.lower()
        return [v for n, v in self.entries if n == name]


def _check_entry(name: bytes, value: bytes) -> int | None:
    """None if (name, value) may be a field, else the header-error."""
    if not _TOKEN.fullmatch(name) or any(c in (0, 10, 13) or (c < 32 and c != 9) or c == 127 for c in value):
        return _INVALID_SYNTAX
    if name.decode().lower() in _FORBIDDEN:
        return _FORBIDDEN_HEADER
    return None


def fields_new(guest: Guest, entries: Sequence[tuple[str, bytes]] = (), immutable: bool = False) -> int:
    return _table_new(guest, KIND_FIELDS, Fields([(n.lower(), v) for n, v in entries], immutable))


def _fields(guest: Guest, handle: int) -> Fields:
    return _table_get(guest, handle, KIND_FIELDS)  # type: ignore[return-value]


def _take_fields(guest: Guest, handle: int) -> Fields:
    """An own `fields` given to a request, a response or a body: the handle is spent, the fields are not to be changed any more."""
    f = _fields(guest, handle)
    _table_drop(guest, handle, KIND_FIELDS)
    f.immutable = True
    return f


def _load_list_pairs(guest: Guest, comp: int, ptr: int, n: int) -> list[tuple[bytes, bytes]]:
    out: list[tuple[bytes, bytes]] = []
    for i in range(n):
        at = ptr + 16 * i
        name = _load_bytes(guest, comp, _load_u32(guest, comp, at), _load_u32(guest, comp, at + 4))
        value = _load_bytes(guest, comp, _load_u32(guest, comp, at + 8), _load_u32(guest, comp, at + 12))
        out.append((name, value))
    return out


def _store_pairs(guest: Guest, comp: int, entries: Sequence[tuple[str, bytes]], retptr: int) -> None:
    ptr = _alloc(guest, comp, 4, 16 * len(entries))
    for i, (name, value) in enumerate(entries):
        at = ptr + 16 * i
        _store_string(guest, comp, at, name)
        _store_string(guest, comp, at + 8, value)
    _store_u32(guest, comp, retptr, ptr)
    _store_u32(guest, comp, retptr + 4, len(entries))


def wasi_http_types_fields_new(guest: Guest) -> int:
    return fields_new(guest)


def wasi_http_types_fields_from_list(guest: Guest, comp: int, ptr: int, len: int, retptr: int) -> None:
    pairs = _load_list_pairs(guest, comp, ptr, len)
    for name, value in pairs:
        err = _check_entry(name, value)
        if err is not None:
            _store_u8(guest, comp, retptr, 1)
            _store_u8(guest, comp, retptr + 4, err)
            return
    _store_u8(guest, comp, retptr, 0)
    _store_u32(guest, comp, retptr + 4, fields_new(guest, [(n.decode(), v) for n, v in pairs]))


def wasi_http_types_fields_get(guest: Guest, comp: int, self: int, name_ptr: int, name_len: int, retptr: int) -> None:
    values = _fields(guest, self).get(_load_bytes(guest, comp, name_ptr, name_len).decode("latin-1"))
    ptr = _alloc(guest, comp, 4, 8 * len(values))
    for i, value in enumerate(values):
        _store_string(guest, comp, ptr + 8 * i, value)
    _store_u32(guest, comp, retptr, ptr)
    _store_u32(guest, comp, retptr + 4, len(values))


def wasi_http_types_fields_has(guest: Guest, comp: int, self: int, name_ptr: int, name_len: int) -> int:
    return int(bool(_fields(guest, self).get(_load_bytes(guest, comp, name_ptr, name_len).decode("latin-1"))))


def _header_result(guest: Guest, comp: int, retptr: int, err: int | None) -> None:
    _store_u8(guest, comp, retptr, 0 if err is None else 1)
    if err is not None:
        _store_u8(guest, comp, retptr + 1, err)


def _change(f: Fields, name: bytes, values: Sequence[bytes], replace: bool) -> int | None:
    if f.immutable:
        return _IMMUTABLE
    for value in values or [b""]:
        err = _check_entry(name, value)
        if err is not None:
            return err
    key = name.decode().lower()
    if replace:
        f.entries = [e for e in f.entries if e[0] != key]
    f.entries += [(key, v) for v in values]
    return None


def wasi_http_types_fields_set(
    guest: Guest, comp: int, self: int, name_ptr: int, name_len: int, value_ptr: int, value_len: int, retptr: int
) -> None:
    values = [
        _load_bytes(
            guest, comp, _load_u32(guest, comp, value_ptr + 8 * i), _load_u32(guest, comp, value_ptr + 8 * i + 4)
        )
        for i in range(value_len)
    ]
    err = _change(_fields(guest, self), _load_bytes(guest, comp, name_ptr, name_len), values, True)
    _header_result(guest, comp, retptr, err)


def wasi_http_types_fields_delete(
    guest: Guest, comp: int, self: int, name_ptr: int, name_len: int, retptr: int
) -> None:
    err = _change(_fields(guest, self), _load_bytes(guest, comp, name_ptr, name_len), [], True)
    _header_result(guest, comp, retptr, err)


def wasi_http_types_fields_append(
    guest: Guest, comp: int, self: int, name_ptr: int, name_len: int, value_ptr: int, value_len: int, retptr: int
) -> None:
    value = _load_bytes(guest, comp, value_ptr, value_len)
    err = _change(_fields(guest, self), _load_bytes(guest, comp, name_ptr, name_len), [value], False)
    _header_result(guest, comp, retptr, err)


def wasi_http_types_fields_entries(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_pairs(guest, comp, _fields(guest, self).entries, retptr)


def wasi_http_types_fields_clone(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_FIELDS, Fields(_fields(guest, self).entries))


def wasi_http_types_fields_drop(guest: Guest, handle: int) -> None:
    _fields(guest, handle)
    _table_drop(guest, handle, KIND_FIELDS)


# method, scheme, authority, path


def _lift_method(guest: Guest, comp: int, tag: int, ptr: int, n: int) -> str:
    if tag < 9:
        return _STD_METHODS[tag]
    if tag > 9:
        _trap(guest, f"bad method {tag}")
    return _load_bytes(guest, comp, ptr, n).decode("latin-1")


def _lift_scheme(guest: Guest, comp: int, tag: int, ptr: int, n: int) -> str:
    if tag < 2:
        return ("http", "https")[tag]
    if tag > 2:
        _trap(guest, f"bad scheme {tag}")
    return _load_bytes(guest, comp, ptr, n).decode("latin-1")


def _store_method(guest: Guest, comp: int, at: int, method: str) -> None:
    if method in _STD_METHODS:
        _store_u8(guest, comp, at, _STD_METHODS.index(method))
    else:
        _store_u8(guest, comp, at, 9)
        _store_string(guest, comp, at + 4, method)


def _store_scheme(guest: Guest, comp: int, at: int, scheme: str) -> None:
    if scheme in ("http", "https"):
        _store_u8(guest, comp, at, ("http", "https").index(scheme))
    else:
        _store_u8(guest, comp, at, 2)
        _store_string(guest, comp, at + 4, scheme)


def _valid_path(path: str) -> bool:
    return (path.startswith("/") or path == "*") and all(32 < ord(c) < 127 for c in path)


def _split_authority(authority: str, default_port: int) -> tuple[str, int] | None:
    """(host, port) of `host`, `host:port` or `[v6]:port`; None if it is not an authority."""
    if not authority or any(c in " /@?#\\" or ord(c) < 32 or ord(c) > 126 for c in authority):
        return None
    if authority.startswith("["):
        end = authority.find("]")
        if end < 0:
            return None
        host, rest = authority[1:end], authority[end + 1 :]
        if rest and not rest.startswith(":"):
            return None
        port_text = rest[1:] if rest else ""
    else:
        host, _, port_text = authority.partition(":")
        if ":" in port_text:
            return None
    if not host:
        return None
    if not port_text and (authority.endswith((":", "]:"))):
        return None
    if port_text:
        if not port_text.isdigit() or not 0 < int(port_text) < 65536:
            return None
        return host, int(port_text)
    return host, default_port


class _OutgoingBody:
    def __init__(self, expected: int | None, kind: str) -> None:
        self.stream: BufferOutput = _RequestBodyOutput() if kind == "request" else BufferOutput(limit=MAX_BODY)
        self.stream_handle: int | None = None
        self.expected = expected  # the content-length the guest declared
        self.kind = kind  # "request" or "response"
        self.trailers: Fields | None = None
        self.finished = False
        self.abandoned = False

    def data(self) -> bytes:
        return bytes(self.stream.data)

    def size(self) -> int:
        """All that the guest wrote (a request that went out while it was written no longer keeps it)."""
        return self.stream.written if isinstance(self.stream, _RequestBodyOutput) else len(self.stream.data)


def _declared_length(headers: Fields) -> int | None:
    for value in headers.get("content-length"):
        if value.isdigit():
            return int(value)
    return None


class _OutgoingRequest:
    def __init__(self, headers: Fields) -> None:
        self.headers = headers
        self.method = "GET"
        self.path: str | None = None
        self.scheme: str | None = None
        self.authority: str | None = None
        self.body: _OutgoingBody | None = None


class _RequestOptions:
    def __init__(self) -> None:
        self.connect: int | None = None
        self.first_byte: int | None = None
        self.between_bytes: int | None = None


def _request(guest: Guest, handle: int) -> _OutgoingRequest:
    return _table_get(guest, handle, KIND_OUTGOING_REQUEST)  # type: ignore[return-value]


def wasi_http_types_outgoing_request_new(guest: Guest, headers: int) -> int:
    return _table_new(guest, KIND_OUTGOING_REQUEST, _OutgoingRequest(_take_fields(guest, headers)))


def _new_body(
    guest: Guest, comp: int, owner_headers: Fields, kind: str, slot: _OutgoingBody | None, retptr: int
) -> _OutgoingBody | None:
    if slot is not None:
        _store_u8(guest, comp, retptr, 1)
        return None
    body = _OutgoingBody(_declared_length(owner_headers), kind)
    _store_u8(guest, comp, retptr, 0)
    _store_u32(guest, comp, retptr + 4, _table_new(guest, KIND_OUTGOING_BODY, body))
    return body


def wasi_http_types_outgoing_request_body(guest: Guest, comp: int, self: int, retptr: int) -> None:
    req = _request(guest, self)
    body = _new_body(guest, comp, req.headers, "request", req.body, retptr)
    if body is not None:
        req.body = body


def wasi_http_types_outgoing_request_method(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_method(guest, comp, retptr, _request(guest, self).method)


def wasi_http_types_outgoing_request_set_method(
    guest: Guest, comp: int, self: int, tag: int, ptr: int, len: int
) -> int:
    method = _lift_method(guest, comp, tag, ptr, len)
    if not _TOKEN.fullmatch(method.encode("latin-1")):
        return 1
    _request(guest, self).method = method
    return 0


def _store_opt_text(guest: Guest, comp: int, retptr: int, text: str | None) -> None:
    _store_opt_string(guest, comp, retptr, text)


def wasi_http_types_outgoing_request_path_with_query(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_opt_text(guest, comp, retptr, _request(guest, self).path)


def wasi_http_types_outgoing_request_set_path_with_query(
    guest: Guest, comp: int, self: int, tag: int, ptr: int, len: int
) -> int:
    req = _request(guest, self)
    path = _load_bytes(guest, comp, ptr, len).decode("latin-1") if tag else None
    if path is not None and not _valid_path(path):
        return 1
    req.path = path
    return 0


def wasi_http_types_outgoing_request_scheme(guest: Guest, comp: int, self: int, retptr: int) -> None:
    scheme = _request(guest, self).scheme
    _store_u8(guest, comp, retptr, 0 if scheme is None else 1)
    if scheme is not None:
        _store_scheme(guest, comp, retptr + 4, scheme)


def wasi_http_types_outgoing_request_set_scheme(
    guest: Guest, comp: int, self: int, tag: int, scheme_tag: int, ptr: int, len: int
) -> int:
    req = _request(guest, self)
    scheme = _lift_scheme(guest, comp, scheme_tag, ptr, len) if tag else None
    if scheme is not None and not _SCHEME.fullmatch(scheme):
        return 1
    req.scheme = scheme
    return 0


def wasi_http_types_outgoing_request_authority(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_opt_text(guest, comp, retptr, _request(guest, self).authority)


def wasi_http_types_outgoing_request_set_authority(
    guest: Guest, comp: int, self: int, tag: int, ptr: int, len: int
) -> int:
    req = _request(guest, self)
    authority = _load_bytes(guest, comp, ptr, len).decode("latin-1") if tag else None
    if authority is not None and _split_authority(authority, 80) is None:
        return 1
    req.authority = authority
    return 0


def wasi_http_types_outgoing_request_headers(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_FIELDS, _request(guest, self).headers)


def wasi_http_types_outgoing_request_drop(guest: Guest, handle: int) -> None:
    _request(guest, handle)
    _table_drop(guest, handle, KIND_OUTGOING_REQUEST)


# request-options


def _options(guest: Guest, handle: int) -> _RequestOptions:
    return _table_get(guest, handle, KIND_REQUEST_OPTIONS)  # type: ignore[return-value]


def wasi_http_types_request_options_new(guest: Guest) -> int:
    return _table_new(guest, KIND_REQUEST_OPTIONS, _RequestOptions())


def _get_timeout(guest: Guest, comp: int, value: int | None, retptr: int) -> None:
    _store_u8(guest, comp, retptr, 0 if value is None else 1)
    if value is not None:
        _store_u64(guest, comp, retptr + 8, value)


def wasi_http_types_request_options_connect_timeout(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _get_timeout(guest, comp, _options(guest, self).connect, retptr)


def wasi_http_types_request_options_set_connect_timeout(guest: Guest, self: int, tag: int, value: int) -> int:
    _options(guest, self).connect = (value & 0xFFFFFFFFFFFFFFFF) if tag else None
    return 0


def wasi_http_types_request_options_first_byte_timeout(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _get_timeout(guest, comp, _options(guest, self).first_byte, retptr)


def wasi_http_types_request_options_set_first_byte_timeout(guest: Guest, self: int, tag: int, value: int) -> int:
    _options(guest, self).first_byte = (value & 0xFFFFFFFFFFFFFFFF) if tag else None
    return 0


def wasi_http_types_request_options_between_bytes_timeout(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _get_timeout(guest, comp, _options(guest, self).between_bytes, retptr)


def wasi_http_types_request_options_set_between_bytes_timeout(guest: Guest, self: int, tag: int, value: int) -> int:
    _options(guest, self).between_bytes = (value & 0xFFFFFFFFFFFFFFFF) if tag else None
    return 0


def wasi_http_types_request_options_drop(guest: Guest, handle: int) -> None:
    _options(guest, handle)
    _table_drop(guest, handle, KIND_REQUEST_OPTIONS)


# bodies


class _IncomingBody:
    def __init__(self, source: _BodySource | _WholeBody) -> None:
        self.source = source
        self.stream_handle: int | None = None
        self.stream_taken = False


class _ReadyPollable(Pollable):
    def ready(self, state: State) -> bool:
        return True


def _incoming_body(guest: Guest, handle: int) -> _IncomingBody:
    return _table_get(guest, handle, KIND_INCOMING_BODY)  # type: ignore[return-value]


def _outgoing_body(guest: Guest, handle: int) -> _OutgoingBody:
    return _table_get(guest, handle, KIND_OUTGOING_BODY)  # type: ignore[return-value]


def _consume(
    guest: Guest, comp: int, data: tuple[bytes, Fields | None] | _BodySource | _WholeBody | None, retptr: int
) -> tuple[bytes, Fields | None] | _BodySource | _WholeBody | None:
    """`result<incoming-body>`: the body once, then err."""
    if data is None:
        _store_u8(guest, comp, retptr, 1)
        return None
    _store_u8(guest, comp, retptr, 0)
    source = _WholeBody(*data) if isinstance(data, tuple) else data
    _store_u32(guest, comp, retptr + 4, _table_new(guest, KIND_INCOMING_BODY, _IncomingBody(source)))
    return data


def wasi_http_types_incoming_body_stream(guest: Guest, comp: int, self: int, retptr: int) -> None:
    body = _incoming_body(guest, self)
    if body.stream_taken:
        _store_u8(guest, comp, retptr, 1)
        return
    body.stream_taken = True
    body.stream_handle = _input_stream_new(guest, body.source)
    _store_u8(guest, comp, retptr, 0)
    _store_u32(guest, comp, retptr + 4, body.stream_handle)


class _FutureTrailers:
    def __init__(self, source: _BodySource | _WholeBody) -> None:
        self.source = source
        self.taken = False

    def ready(self) -> bool:
        """The trailers are known when the body has ended (what is not read is read on and dropped: the stream is gone)."""
        self.source.drain()
        return self.source.settled()


class _TrailersPollable(Pollable):
    def __init__(self, future: _FutureTrailers) -> None:
        self.future = future

    def ready(self, state: State) -> bool:
        return self.future.ready()


def wasi_http_types_incoming_body_finish(guest: Guest, this: int) -> int:
    body = _incoming_body(guest, this)
    if body.stream_handle is not None and _table_alive(guest, body.stream_handle, 3):  # KIND_INPUT_STREAM
        _trap(guest, "incoming-body.finish with its stream alive")
    _table_drop(guest, this, KIND_INCOMING_BODY)
    return _table_new(guest, KIND_FUTURE_TRAILERS, _FutureTrailers(body.source))


def wasi_http_types_incoming_body_drop(guest: Guest, handle: int) -> None:
    body = _incoming_body(guest, handle)
    _table_drop(guest, handle, KIND_INCOMING_BODY)
    body.source.release()


def _future_trailers(guest: Guest, handle: int) -> _FutureTrailers:
    return _table_get(guest, handle, KIND_FUTURE_TRAILERS)  # type: ignore[return-value]


def wasi_http_types_future_trailers_subscribe(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_POLLABLE, _TrailersPollable(_future_trailers(guest, self)))


def wasi_http_types_future_trailers_get(guest: Guest, comp: int, self: int, retptr: int) -> None:
    future = _future_trailers(guest, self)
    if not future.ready():
        _store_u8(guest, comp, retptr, 0)  # none: not yet
        return
    _store_u8(guest, comp, retptr, 1)
    if future.taken:
        _store_u8(guest, comp, retptr + 8, 1)  # err: it was already taken
        return
    future.taken = True
    _store_u8(guest, comp, retptr + 8, 0)
    failure = future.source.error
    if failure is not None:
        _store_u8(guest, comp, retptr + 16, 1)  # err: the body broke
        _store_error_code(guest, comp, retptr + 24, failure)
        return
    _store_u8(guest, comp, retptr + 16, 0)  # ok: no error-code
    trailers = future.source.trailers()
    if trailers is None:
        _store_u8(guest, comp, retptr + 24, 0)
    else:
        _store_u8(guest, comp, retptr + 24, 1)
        _store_u32(guest, comp, retptr + 28, _table_new(guest, KIND_FIELDS, trailers))


def wasi_http_types_future_trailers_drop(guest: Guest, handle: int) -> None:
    future = _future_trailers(guest, handle)
    _table_drop(guest, handle, KIND_FUTURE_TRAILERS)
    future.source.release()


def wasi_http_types_outgoing_body_write(guest: Guest, comp: int, self: int, retptr: int) -> None:
    body = _outgoing_body(guest, self)
    if body.stream_handle is not None:
        _store_u8(guest, comp, retptr, 1)
        return
    body.stream_handle = _output_stream_new(guest, body.stream)
    _store_u8(guest, comp, retptr, 0)
    _store_u32(guest, comp, retptr + 4, body.stream_handle)


def wasi_http_types_outgoing_body_finish(
    guest: Guest, comp: int, this: int, tag: int, trailers: int, retptr: int
) -> None:
    body = _outgoing_body(guest, this)
    if body.stream_handle is not None and _table_alive(guest, body.stream_handle, KIND_OUTPUT_STREAM):
        _trap(guest, "outgoing-body.finish with its stream alive")
    if tag:
        body.trailers = _take_fields(guest, trailers)
    _table_drop(guest, this, KIND_OUTGOING_BODY)
    size = body.size()
    if body.expected is not None and body.expected != size:
        _store_u8(guest, comp, retptr, 1)
        _store_error_code(guest, comp, retptr + 8, ErrorCode(f"HTTP-{body.kind}-body-size", size))
        body.abandoned = True
        return
    body.finished = True
    if isinstance(body.stream, _RequestBodyOutput) and body.stream.sender is not None:
        try:  # the request is on its way: its end goes out now, and not only when the answer is asked for again
            body.stream.sender.end(list(body.trailers.entries) if body.trailers is not None else None)
            body.stream.sender.pump()
        except ErrorCode as e:
            body.stream.sender.dead = e
    _store_u8(guest, comp, retptr, 0)


def wasi_http_types_outgoing_body_drop(guest: Guest, handle: int) -> None:
    body = _outgoing_body(guest, handle)
    _table_drop(guest, handle, KIND_OUTGOING_BODY)
    if not body.finished:
        body.abandoned = True


# the transport


@dataclass
class HttpRequest:
    method: str
    scheme: str  # "http" or "https"
    authority: str
    path: str
    headers: list[tuple[str, bytes]]
    body: bytes = b""
    trailers: list[tuple[str, bytes]] | None = None
    connect_ns: int | None = None
    first_byte_ns: int | None = None
    between_bytes_ns: int | None = None


@dataclass
class HttpResponse:
    status: int
    headers: list[tuple[str, bytes]]
    body: bytes = b""
    trailers: list[tuple[str, bytes]] | None = None


def _seconds(ns: int | None) -> float:
    return max((_DEFAULT_TIMEOUT_NS if ns is None else ns) / 1e9, 1e-6)  # 0 would be a socket that does not wait at all


def _dns_error(e: socket.gaierror) -> ErrorCode:
    if e.errno in (socket.EAI_NONAME, getattr(socket, "EAI_NODATA", socket.EAI_NONAME)):
        return ErrorCode("DNS-error", "NXDOMAIN", None)
    if e.errno == socket.EAI_AGAIN:
        return ErrorCode("DNS-error", "SERVFAIL", None)
    return ErrorCode("DNS-error", str(e.args[1]) if len(e.args) > 1 else "failed", None)


def _connect_error(e: OSError) -> ErrorCode:
    if e.errno == errno.ECONNREFUSED:
        return ErrorCode("connection-refused")
    if e.errno == errno.ETIMEDOUT:
        return ErrorCode("connection-timeout")
    if e.errno in (errno.EHOSTUNREACH, errno.ENETUNREACH):
        return ErrorCode("destination-unavailable")
    if e.errno in (errno.ECONNRESET, errno.ECONNABORTED, errno.EPIPE):
        return ErrorCode("connection-terminated")
    return ErrorCode("internal-error", e.strerror or "connect failed")


def _connect(host: str, port: int, timeout: float) -> socket.socket:
    try:
        infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    except socket.gaierror as e:
        raise _dns_error(e) from None
    last: ErrorCode | None = None
    for family, kind, proto, _, addr in infos:
        s = socket.socket(family, kind, proto)
        try:
            s.settimeout(timeout)
            s.connect(addr)
            return s
        except TimeoutError:
            s.close()
            last = ErrorCode("connection-timeout")
        except OSError as e:
            s.close()
            last = _connect_error(e)
    raise last or ErrorCode("destination-not-found")


_WOULD_BLOCK = object()


class _Answer:
    """The answer to one request, read as the events of `h11`: the head first, then the body in pieces, then the end and the trailers.

    `h11` does the protocol (the head, Content-Length, chunked with its extensions and trailers, the end by close, HEAD and the statuses without a body); this class does what it does
    not: the socket and its timeouts, the limits, and the `ErrorCode` for a failure. A failure is told by **where it happened** (the head, the body, the end of the stream), and not by the
    text of the error of `h11`."""

    def __init__(
        self, sock: socket.socket, conn: h11.Connection, method: str, first_s: float, between_s: float
    ) -> None:
        self.sock, self.conn, self.method = sock, conn, method
        self.first_s, self.between_s = first_s, between_s
        self.first = True  # the next read is the first byte of the answer
        self.eof = False
        self.in_head = True
        self.raw = (
            bytearray()
        )  # the bytes of the head that is being read: to tell a 101 or a coding from another protocol error
        self.total = 0
        self.trailers: list[tuple[str, bytes]] | None = None
        self.status = 0
        self.headers: list[tuple[str, bytes]] = []
        self.done = False  # the end of the body was read
        self.deadline = 0.0  # the body, when it is read without waiting: the time by which the next byte is due

    def _got(self, data: bytes) -> None:
        self.first = False
        if self.in_head:
            self.raw += data
        if not data:
            self.eof = True
        else:
            self.deadline = time.monotonic() + self.between_s
        self.conn.receive_data(data)  # b"" tells `h11` that the peer closed

    def _more(self) -> None:
        self.sock.settimeout(self.first_s if self.first else self.between_s)
        try:
            data = self.sock.recv(65536)
        except TimeoutError:
            raise ErrorCode("connection-read-timeout") from None
        except OSError:
            raise ErrorCode("connection-terminated") from None
        self._got(data)

    def _try_more(self) -> bool:
        """Reads what is there without waiting: False if there is nothing now."""
        self.sock.setblocking(False)
        try:
            data = self.sock.recv(65536)
        except (BlockingIOError, ssl.SSLWantReadError):
            return False
        except OSError:
            raise ErrorCode("connection-terminated") from None
        self._got(data)
        return True

    def _head_error(self) -> ErrorCode:
        raw = bytes(self.raw)
        if re.match(rb"HTTP/1\.[01] 101[ \r]", raw):
            return ErrorCode("HTTP-upgrade-failed")
        end = raw.find(b"\r\n\r\n")
        if end < 0:
            if self.eof:
                return ErrorCode("HTTP-response-incomplete")
            if len(raw) > MAX_HEAD:
                return ErrorCode("HTTP-response-header-section-size", MAX_HEAD)
            return ErrorCode("HTTP-protocol-error")
        coding = [
            v.decode("latin-1").strip().lower()
            for line in raw[:end].split(b"\r\n")[1:]
            for name, _, value in [line.partition(b":")]
            if name.strip().lower() == b"transfer-encoding"
            for v in value.split(b",")
        ]
        if coding and coding != ["chunked"]:
            return ErrorCode("HTTP-response-transfer-coding", ", ".join(coding))
        return ErrorCode("HTTP-protocol-error")

    def _event(self, block: bool = True) -> object:
        """The next event of `h11`; without `block`, `_WOULD_BLOCK` when the bytes of it are not here yet."""
        while True:
            try:
                ev = self.conn.next_event()
            except h11.RemoteProtocolError:
                if self.in_head:
                    raise self._head_error() from None
                raise ErrorCode("HTTP-response-incomplete" if self.eof else "HTTP-protocol-error") from None
            if ev is h11.NEED_DATA:
                if self.eof:
                    raise ErrorCode("HTTP-response-incomplete")
                if block:
                    self._more()
                elif not self._try_more():
                    return _WOULD_BLOCK
                continue
            return ev

    def head(self, block: bool = True) -> tuple[int, list[tuple[str, bytes]]] | None:
        """The status and the fields of the answer (the informational ones, 1xx, are passed over); without `block`, None while the head is not all here."""
        while True:
            ev = self._event(block)
            if ev is _WOULD_BLOCK:
                return None
            if isinstance(ev, h11.InformationalResponse):
                if ev.status_code == 101:
                    raise ErrorCode("HTTP-upgrade-failed")
                del self.raw[: self.raw.find(b"\r\n\r\n") + 4]
                continue
            if not isinstance(ev, h11.Response):
                raise ErrorCode("HTTP-protocol-error")
            end = self.raw.find(b"\r\n\r\n")
            if end + 4 > MAX_HEAD:
                raise ErrorCode("HTTP-response-header-section-size", MAX_HEAD)
            self.in_head = False
            self.raw = bytearray()
            fields = [(name.decode("latin-1").lower(), value) for name, value in ev.headers]
            if self.method != "HEAD" and ev.status_code not in (204, 304):
                lengths = [value for name, value in fields if name == "content-length"]
                if lengths and not any(n == "transfer-encoding" for n, _ in fields):
                    declared = min(int(lengths[0]), (1 << 64) - 1)
                    if declared > MAX_BODY:
                        raise ErrorCode("HTTP-response-body-size", declared)
            return ev.status_code, fields

    def begin_body(self) -> None:
        """The head is read: from now on the body can be read without waiting, and each byte is due `between_s` after the one before."""
        self.deadline = time.monotonic() + self.between_s
        self.sock.setblocking(False)

    def poll(self) -> bytes | None:
        """A piece of the body if there is one now; None if there is none now, or if the body ended (`self.done`, and the trailers). A failure is the `ErrorCode`."""
        if self.done:
            return None
        ev = self._event(block=False)
        if ev is _WOULD_BLOCK:
            if time.monotonic() > self.deadline:
                raise ErrorCode("connection-read-timeout")
            return None
        if isinstance(ev, h11.Data):
            self.total += len(ev.data)
            if self.total > MAX_BODY:
                raise ErrorCode("HTTP-response-body-size", self.total)
            return bytes(ev.data)
        if isinstance(ev, h11.EndOfMessage):
            trailers = [(name.decode("latin-1").lower(), value) for name, value in ev.headers]
            self.trailers = trailers or None
        self.done = True  # the end, or the connection closed after it
        return None

    def close(self) -> None:
        self.sock.close()

    def pieces(self) -> Iterator[bytes]:
        """The body, piece by piece; the trailers are in `self.trailers` when it ends."""
        while True:
            ev = self._event()
            if isinstance(ev, h11.Data):
                self.total += len(ev.data)
                if self.total > MAX_BODY:
                    raise ErrorCode("HTTP-response-body-size", self.total)
                yield bytes(ev.data)
            elif isinstance(ev, h11.EndOfMessage):
                trailers = [(name.decode("latin-1").lower(), value) for name, value in ev.headers]
                self.trailers = trailers or None
                return
            else:  # the connection closed after the end, or the connection is paused: nothing more of this answer
                return


class _RequestSender:
    """A request that goes out while the guest still writes its body (the answer may come first: a refusal, a redirect, an early reply).

    The bytes are made by `h11` (the head, and the chunks if the length was not declared), kept in `out`, and given to the socket as it takes them (`pump`); nothing here waits."""

    def __init__(self, sock: socket.socket, conn: h11.Connection) -> None:
        self.sock, self.conn = sock, conn
        self.out = bytearray()
        self.ended = False
        self.dead: ErrorCode | None = None  # the connection broke while the request went out

    def head(self, req: HttpRequest, declared: bool) -> None:
        headers: list[tuple[bytes, bytes]] = [(b"host", req.authority.encode("latin-1"))]
        headers += [(n.encode("latin-1"), v) for n, v in req.headers or []]
        if not declared:
            headers.append((b"transfer-encoding", b"chunked"))
        headers.append((b"connection", b"close"))
        self._make(h11.Request(method=req.method, target=req.path, headers=headers))

    def data(self, piece: bytes) -> None:
        if piece:
            self._make(h11.Data(data=piece))

    def end(self, trailers: list[tuple[str, bytes]] | None) -> None:
        end_headers = None if not trailers else [(n.encode("latin-1"), v) for n, v in trailers]
        self._make(h11.EndOfMessage(headers=end_headers))
        self.ended = True

    def _make(self, event: h11.Event) -> None:
        try:
            self.out += self.conn.send(event) or b""
        except h11.LocalProtocolError:
            raise ErrorCode("HTTP-protocol-error") from None

    def pump(self) -> None:
        """Gives the socket what it takes now; the rest stays in `out`."""
        while self.out and self.dead is None:
            try:
                n = self.sock.send(self.out)
            except (BlockingIOError, ssl.SSLWantReadError, ssl.SSLWantWriteError):
                return
            except OSError:
                self.dead = ErrorCode("connection-terminated")
                self.out.clear()
                return
            del self.out[:n]

    def sent(self) -> bool:
        return self.ended and not self.out


class _RequestBodyOutput(BufferOutput):
    """The stream of the body of a request. Until the answer is asked for the bytes are kept, and the request is sent whole, with its length. If the answer is asked for while the body is
    still being written, the request starts (`sender`) and the bytes go to the connection as they come."""

    _AHEAD = 1 << 16  # the most that waits for the socket

    def __init__(self) -> None:
        super().__init__(limit=MAX_BODY)
        self.sender: _RequestSender | None = None
        self.written = 0  # all that the guest wrote, sent or not

    def check_write(self) -> int:
        if self.sender is None:
            return super().check_write()
        self.sender.pump()
        if self.sender.dead is not None:
            raise StreamFailed(str(self.sender.dead), http_code=self.sender.dead)
        return max(0, min(MAX_BODY - self.written, self._AHEAD - len(self.sender.out)))

    def write(self, data: bytes) -> None:
        self.written += len(data)
        if self.sender is None:
            super().write(data)
            return
        try:
            self.sender.data(bytes(data))
        except ErrorCode as e:
            raise StreamFailed(str(e), http_code=e) from None
        self.sender.pump()

    def flush(self) -> None:
        if self.sender is not None:
            self.sender.pump()

    def start(self, sender: _RequestSender) -> None:
        """The request goes out now: what was written is the first of it."""
        pending = bytes(self.data)
        self.data.clear()
        sender.data(pending)
        self.sender = sender


def _send_request(sock: socket.socket, conn: h11.Connection, req: HttpRequest) -> None:
    """The request: head, then the body (in chunks, with the trailers, if trailers are given). The fields are the guest's, in its order, after `Host`."""
    headers: list[tuple[bytes, bytes]] = [(b"host", req.authority.encode("latin-1"))]
    names: set[str] = set()
    for n, v in req.headers or []:  # None: no headers (as NULL is in the C twin)
        headers.append((n.encode("latin-1"), v))
        names.add(n)
    if req.trailers is not None:
        headers.append((b"transfer-encoding", b"chunked"))
    elif "content-length" not in names and (req.body or req.method in ("POST", "PUT", "PATCH")):
        headers.append((b"content-length", str(len(req.body)).encode()))
    headers.append((b"connection", b"close"))
    try:
        out = [conn.send(h11.Request(method=req.method, target=req.path, headers=headers))]
        if req.body:
            out.append(conn.send(h11.Data(data=req.body)))
        end_headers = None if req.trailers is None else [(n.encode("latin-1"), v) for n, v in req.trailers]
        out.append(conn.send(h11.EndOfMessage(headers=end_headers)))
    except h11.LocalProtocolError:
        raise ErrorCode("HTTP-protocol-error") from None
    sock.sendall(b"".join(out))


def _dial(req: HttpRequest) -> socket.socket:
    """The connection for a request: the socket is connected and, for `https`, wrapped in TLS. A failure is the `ErrorCode`."""
    # what a request must have, as in the C twin: a missing field is an error that is answered, not a crash (`headers` None is no headers)
    if getattr(req, "method", None) is None:
        raise ErrorCode("internal-error", "the request has no method")
    if getattr(req, "scheme", None) is None:
        raise ErrorCode("internal-error", "the request has no scheme")
    if getattr(req, "authority", None) is None or getattr(req, "path", None) is None:
        raise ErrorCode("HTTP-request-URI-invalid")
    https = req.scheme == "https"
    parsed = _split_authority(req.authority, 443 if https else 80)
    if parsed is None:
        raise ErrorCode("HTTP-request-URI-invalid")
    name, port = parsed
    sock = _connect(name, port, _seconds(req.connect_ns))
    if https:
        try:
            sock.settimeout(_seconds(req.connect_ns))
            return ssl.create_default_context().wrap_socket(sock, server_hostname=name)
        except ssl.SSLCertVerificationError:
            sock.close()
            raise ErrorCode("TLS-certificate-error") from None
        except TimeoutError:
            sock.close()
            raise ErrorCode("connection-timeout") from None
        except ssl.SSLError:
            sock.close()
            raise ErrorCode("TLS-protocol-error") from None
        except OSError:
            sock.close()
            raise ErrorCode("connection-terminated") from None
    return sock


def _open(req: HttpRequest) -> _Answer:
    """Connects, sends the request and reads the head of the answer; the body is left in the socket. The answer owns the socket (`close`)."""
    sock = _dial(req)
    try:
        between = _seconds(req.between_bytes_ns)
        sock.settimeout(between)
        conn = h11.Connection(h11.CLIENT, max_incomplete_event_size=MAX_HEAD)
        try:
            _send_request(sock, conn, req)
        except TimeoutError:
            raise ErrorCode("connection-write-timeout") from None
        except OSError:
            raise ErrorCode("connection-terminated") from None
        answer = _Answer(sock, conn, req.method, _seconds(req.first_byte_ns), between)
        head = answer.head()
        assert head is not None  # (it waited)
        answer.status, answer.headers = head
        return answer
    except BaseException:
        sock.close()
        raise


def default_send(req: HttpRequest) -> HttpResponse:
    """The transport of the host: a connection for each request, `Connection: close`; the answer is read whole."""
    answer = _open(req)
    try:
        body = b"".join(answer.pieces())
        # the framing was undone by `h11`, so the guest is not shown it (as in Wasmtime)
        return HttpResponse(
            answer.status, [h for h in answer.headers if h[0] != "transfer-encoding"], body, answer.trailers
        )
    finally:
        answer.close()


def _default_open(req: HttpRequest) -> tuple[int, list[tuple[str, bytes]], _BodySource]:
    """The same transport, with the body left to be read as it comes: the status, the fields and the source of the body."""
    answer = _open(req)
    answer.begin_body()
    return answer.status, [h for h in answer.headers if h[0] != "transfer-encoding"], _BodySource(answer)


class _BodySource(InputStream):
    """The body of an answer, read from the socket as it comes (the stream of an `incoming-body`), and then its trailers.

    `read` gives what is here and never waits (`b""` if nothing is); a failure (the connection, a timeout between bytes, the limit of the total) comes after the bytes that were read before
    it, as `StreamFailed` that carries the `ErrorCode` for `http-error-code`. Dropping the stream does not close the socket: the trailers are asked for after it. The socket is closed when the body
    ends or fails, and when the body or its trailers are dropped."""

    _AHEAD = 1 << 16  # the most that is read in advance

    def __init__(self, answer: _Answer, sender: _RequestSender | None = None) -> None:
        self.answer = answer
        self.sender = sender  # the request, if the answer came while the guest still wrote its body
        self.buf = bytearray()
        self.error: ErrorCode | None = None

    def _fill(self) -> None:
        if self.sender is not None and not self.sender.sent():
            self.sender.pump()  # (a request that broke after the answer came is not the answer's failure)
        while len(self.buf) < self._AHEAD and not self.answer.done and self.error is None:
            try:
                piece = self.answer.poll()
            except ErrorCode as e:
                self.error = e
                break
            if piece is None:
                break
            self.buf += piece
        if self.answer.done or self.error is not None:
            self.release()

    def read(self, n: int) -> bytes:
        self._fill()
        if self.buf:
            out = bytes(self.buf[:n])
            del self.buf[:n]
            return out
        if self.error is not None:
            raise StreamFailed(str(self.error), http_code=self.error)
        if self.answer.done and n:
            raise StreamClosed
        return b""

    def ready(self) -> bool:
        self._fill()
        return bool(self.buf) or self.answer.done or self.error is not None

    def close(self) -> None:
        pass  # see the class

    def drain(self) -> None:
        """What is not read is not wanted (the stream is dropped and the trailers are asked for): reads on to the end."""
        self.buf.clear()
        while not self.answer.done and self.error is None:
            try:
                if self.answer.poll() is None:
                    break
            except ErrorCode as e:
                self.error = e
        if self.answer.done or self.error is not None:
            self.release()

    def settled(self) -> bool:
        return self.answer.done or self.error is not None

    def trailers(self) -> Fields | None:
        t = self.answer.trailers
        return None if t is None else Fields([(n.lower(), v) for n, v in t], True)

    def release(self) -> None:
        self.answer.close()

    def __del__(self) -> None:
        with contextlib.suppress(OSError, AttributeError):  # a socket that is gone, or an object that was not finished
            self.answer.close()


class _WholeBody(MemoryInput):
    """A body that is all here (an answer of `http_send`, a request that the embedder gave): the same methods as `_BodySource`."""

    def __init__(self, data: bytes, trailers: Fields | None) -> None:
        super().__init__(data)
        self._trailers = trailers
        self.error = None

    def drain(self) -> None:
        pass

    def settled(self) -> bool:
        return True

    def trailers(self) -> Fields | None:
        return self._trailers

    def release(self) -> None:
        pass


# the answer


class _IncomingResponse:
    def __init__(
        self, status: int, headers: Fields, body: tuple[bytes, Fields | None] | _BodySource | _WholeBody | None
    ) -> None:
        self.status = status
        self.headers = headers
        self.body = body  # None once consumed


class _FutureIncomingResponse:
    def __init__(self, request: _OutgoingRequest, options: _RequestOptions | None, prepared: HttpRequest) -> None:
        self.request, self.options, self.prepared = request, options, prepared
        self.result: _IncomingResponse | ErrorCode | None = None
        self.taken = False
        self.sender: _RequestSender | None = None  # the request is going out while the guest writes its body
        self.answer: _Answer | None = None
        self.head_due: float | None = None  # when the head of the answer is due, once the request is all out

    def advance(self, state: State) -> bool:
        if self.result is not None:
            return True
        body = self.request.body
        if self.sender is None:
            if body is not None and not (body.finished or body.abandoned):
                if state.http_send is not None:
                    return False  # a transport of the embedder takes the request whole
                self._start(body)  # our own: the request goes out now, and the body as the guest writes it
                if self.result is not None:
                    return True
            else:
                return self._send_whole(state, body)
        return self._go_on(body)

    def _send_whole(self, state: State, body: _OutgoingBody | None) -> bool:
        if body is not None and body.abandoned:
            self.result = ErrorCode("HTTP-protocol-error")
            return True
        if body is not None:
            self.prepared.body = body.data()
            if body.trailers is not None:
                self.prepared.trailers = list(body.trailers.entries)
        if state.http_send is None:  # our own transport: the body is read as it comes
            try:
                status, fields, source = _default_open(self.prepared)
            except ErrorCode as e:
                self.result = e
            else:
                self.result = _IncomingResponse(status, Fields([(n.lower(), v) for n, v in fields], True), source)
            return True
        try:
            response = state.http_send(self.prepared)
        except ErrorCode as e:
            self.result = e
        else:
            trailers = (
                None if response.trailers is None else Fields([(n.lower(), v) for n, v in response.trailers], True)
            )
            self.result = _IncomingResponse(
                response.status,
                Fields([(n.lower(), v) for n, v in response.headers], True),
                (bytes(response.body), trailers),
            )
        return True

    def _start(self, body: _OutgoingBody) -> None:
        """The answer is asked for before the body is finished: connect, send the head, and from now on the body goes out as it is written."""
        req = self.prepared
        try:
            sock = _dial(req)
        except ErrorCode as e:
            self.result = e
            return
        try:
            sock.setblocking(False)
            conn = h11.Connection(h11.CLIENT, max_incomplete_event_size=MAX_HEAD)
            sender = _RequestSender(sock, conn)
            sender.head(req, declared=body.expected is not None)
            assert isinstance(body.stream, _RequestBodyOutput)
            body.stream.start(sender)
            sender.pump()
        except BaseException as e:
            sock.close()
            if isinstance(e, ErrorCode):
                self.result = e
                return
            raise
        self.sender = sender
        self.answer = _Answer(sock, conn, req.method, _seconds(req.first_byte_ns), _seconds(req.between_bytes_ns))

    def _go_on(self, body: _OutgoingBody | None) -> bool:
        sender, answer = self.sender, self.answer
        assert sender is not None and answer is not None and body is not None
        try:
            if body.abandoned:
                raise ErrorCode("HTTP-protocol-error")
            if body.finished and not sender.ended:
                sender.end(list(body.trailers.entries) if body.trailers is not None else None)
            sender.pump()
            if sender.dead is not None and answer.in_head:
                raise sender.dead
            if sender.sent() and self.head_due is None:
                self.head_due = (
                    time.monotonic() + answer.first_s
                )  # the first byte is due after the whole request is out
            head = answer.head(block=False)
            if head is None and self.head_due is not None and time.monotonic() > self.head_due:
                raise ErrorCode("connection-read-timeout")
        except ErrorCode as e:
            answer.close()
            self.result = e
            return True
        if head is None:
            return False
        answer.status, answer.headers = head
        answer.begin_body()
        fields = [h for h in answer.headers if h[0] != "transfer-encoding"]
        self.result = _IncomingResponse(
            answer.status, Fields([(n.lower(), v) for n, v in fields], True), _BodySource(answer, sender)
        )
        return True


class _FuturePollable(Pollable):
    def __init__(self, future: _FutureIncomingResponse) -> None:
        self.future = future

    def ready(self, state: State) -> bool:
        return self.future.advance(state)


def _future(guest: Guest, handle: int) -> _FutureIncomingResponse:
    return _table_get(guest, handle, KIND_FUTURE_INCOMING_RESPONSE)  # type: ignore[return-value]


def _prepare(guest: Guest, req: _OutgoingRequest, options: _RequestOptions | None) -> HttpRequest:
    """The request as the transport takes it, or the `ErrorCode` for what is wrong with it (also: the policy)."""
    if not _TOKEN.fullmatch(req.method.encode("latin-1")):
        raise ErrorCode("HTTP-request-method-invalid")
    scheme = "https" if req.scheme is None else req.scheme
    if scheme not in ("http", "https") or req.authority is None:
        raise ErrorCode("HTTP-request-URI-invalid")
    if _split_authority(req.authority, 80) is None:
        raise ErrorCode("HTTP-request-URI-invalid")
    if not guest.state.http_check(scheme, req.authority):
        raise ErrorCode("HTTP-request-denied")
    options = options or _RequestOptions()
    return HttpRequest(
        req.method,
        scheme,
        req.authority,
        req.path or "/",
        list(req.headers.entries),
        connect_ns=options.connect,
        first_byte_ns=options.first_byte,
        between_bytes_ns=options.between_bytes,
    )


def wasi_http_outgoing_handler_handle(
    guest: Guest, comp: int, request: int, opt_tag: int, opt_handle: int, retptr: int
) -> None:
    req = _request(guest, request)
    options = _options(guest, opt_handle) if opt_tag else None
    _table_drop(guest, request, KIND_OUTGOING_REQUEST)
    if opt_tag:
        _table_drop(guest, opt_handle, KIND_REQUEST_OPTIONS)
    try:
        prepared = _prepare(guest, req, options)
    except ErrorCode as e:
        _store_u8(guest, comp, retptr, 1)
        _store_error_code(guest, comp, retptr + 8, e)
        return
    _store_u8(guest, comp, retptr, 0)
    _store_u32(
        guest,
        comp,
        retptr + 8,
        _table_new(guest, KIND_FUTURE_INCOMING_RESPONSE, _FutureIncomingResponse(req, options, prepared)),
    )


def wasi_http_types_future_incoming_response_subscribe(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_POLLABLE, _FuturePollable(_future(guest, self)))


def wasi_http_types_future_incoming_response_get(guest: Guest, comp: int, self: int, retptr: int) -> None:
    future = _future(guest, self)
    if not future.advance(guest.state):
        _store_u8(guest, comp, retptr, 0)
        return
    _store_u8(guest, comp, retptr, 1)
    if future.taken:
        _store_u8(guest, comp, retptr + 8, 1)
        return
    future.taken = True
    _store_u8(guest, comp, retptr + 8, 0)
    if isinstance(future.result, ErrorCode):
        _store_u8(guest, comp, retptr + 16, 1)
        _store_error_code(guest, comp, retptr + 24, future.result)
    else:
        _store_u8(guest, comp, retptr + 16, 0)
        _store_u32(guest, comp, retptr + 24, _table_new(guest, KIND_INCOMING_RESPONSE, future.result))


def wasi_http_types_future_incoming_response_drop(guest: Guest, handle: int) -> None:
    future = _future(guest, handle)
    _table_drop(guest, handle, KIND_FUTURE_INCOMING_RESPONSE)
    if isinstance(future.result, _IncomingResponse) and not future.taken and future.result.body is not None:
        _release(future.result.body)  # an answer that the guest never asked for


def _response(guest: Guest, handle: int) -> _IncomingResponse:
    return _table_get(guest, handle, KIND_INCOMING_RESPONSE)  # type: ignore[return-value]


def wasi_http_types_incoming_response_status(guest: Guest, self: int) -> int:
    return _response(guest, self).status


def wasi_http_types_incoming_response_headers(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_FIELDS, _response(guest, self).headers)


def wasi_http_types_incoming_response_consume(guest: Guest, comp: int, self: int, retptr: int) -> None:
    resp = _response(guest, self)
    if _consume(guest, comp, resp.body, retptr) is not None:
        resp.body = None


def _release(body: tuple[bytes, Fields | None] | _BodySource | _WholeBody) -> None:
    if not isinstance(body, tuple):
        body.release()


def wasi_http_types_incoming_response_drop(guest: Guest, handle: int) -> None:
    resp = _response(guest, handle)
    _table_drop(guest, handle, KIND_INCOMING_RESPONSE)
    if resp.body is not None:
        _release(resp.body)  # the body was not consumed


# the server side: what an embedder gives a component that exports incoming-handler, and what it gets back


class _IncomingRequest:
    def __init__(
        self,
        method: str,
        path: str | None,
        scheme: str | None,
        authority: str | None,
        headers: Fields,
        body: tuple[bytes, Fields | None] | None,
    ) -> None:
        self.method, self.path, self.scheme, self.authority = method, path, scheme, authority
        self.headers = headers
        self.body = body


def incoming_request_new(
    guest: Guest,
    method: str,
    path: str | None,
    scheme: str | None,
    authority: str | None,
    headers: Sequence[tuple[str, bytes]] = (),
    body: bytes = b"",
    trailers: Sequence[tuple[str, bytes]] | None = None,
) -> int:
    """A request for the guest (its handle); `scheme` is "http", "https" or another text."""
    fields = Fields([(n.lower(), v) for n, v in headers], True)
    t = None if trailers is None else Fields([(n.lower(), v) for n, v in trailers], True)
    return _table_new(
        guest, KIND_INCOMING_REQUEST, _IncomingRequest(method, path, scheme, authority, fields, (bytes(body), t))
    )


def _incoming_request(guest: Guest, handle: int) -> _IncomingRequest:
    return _table_get(guest, handle, KIND_INCOMING_REQUEST)  # type: ignore[return-value]


def wasi_http_types_incoming_request_method(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_method(guest, comp, retptr, _incoming_request(guest, self).method)


def wasi_http_types_incoming_request_path_with_query(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_opt_text(guest, comp, retptr, _incoming_request(guest, self).path)


def wasi_http_types_incoming_request_scheme(guest: Guest, comp: int, self: int, retptr: int) -> None:
    scheme = _incoming_request(guest, self).scheme
    _store_u8(guest, comp, retptr, 0 if scheme is None else 1)
    if scheme is not None:
        _store_scheme(guest, comp, retptr + 4, scheme)


def wasi_http_types_incoming_request_authority(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _store_opt_text(guest, comp, retptr, _incoming_request(guest, self).authority)


def wasi_http_types_incoming_request_headers(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_FIELDS, _incoming_request(guest, self).headers)


def wasi_http_types_incoming_request_consume(guest: Guest, comp: int, self: int, retptr: int) -> None:
    req = _incoming_request(guest, self)
    if _consume(guest, comp, req.body, retptr) is not None:
        req.body = None


def wasi_http_types_incoming_request_drop(guest: Guest, handle: int) -> None:
    _incoming_request(guest, handle)
    _table_drop(guest, handle, KIND_INCOMING_REQUEST)


class _OutgoingResponse:
    def __init__(self, headers: Fields) -> None:
        self.headers = headers
        self.status = 200
        self.body: _OutgoingBody | None = None

    def body_bytes(self) -> bytes:
        return b"" if self.body is None else self.body.data()

    @property
    def trailers(self) -> Fields | None:
        return None if self.body is None else self.body.trailers

    @property
    def finished(self) -> bool:
        return self.body is not None and self.body.finished


class OutParam:
    """Where the guest puts its answer: `response` or `error` is set when it calls `response-outparam.set`; `error` too if the handle is dropped without it."""

    def __init__(self) -> None:
        self.response: _OutgoingResponse | None = None
        self.error: ErrorCode | None = None
        self.done = False


def response_outparam_new(guest: Guest) -> tuple[int, OutParam]:
    param = OutParam()
    return _table_new(guest, KIND_RESPONSE_OUTPARAM, param), param


def _outparam(guest: Guest, handle: int) -> OutParam:
    return _table_get(guest, handle, KIND_RESPONSE_OUTPARAM)  # type: ignore[return-value]


def wasi_http_types_response_outparam_set(
    guest: Guest, comp: int, param: int, tag: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int
) -> None:
    out = _outparam(guest, param)
    if tag == 0:
        response: _OutgoingResponse = _table_get(guest, a1, KIND_OUTGOING_RESPONSE)  # type: ignore[assignment]
        _table_drop(guest, a1, KIND_OUTGOING_RESPONSE)
        out.response = response
    elif tag == 1:
        out.error = _lift_error_code_flat(guest, comp, [a1, a2, a3, a4, a5, a6, a7])
    else:
        _trap(guest, f"bad result {tag}")
    out.done = True
    _table_drop(guest, param, KIND_RESPONSE_OUTPARAM)


def wasi_http_types_response_outparam_drop(guest: Guest, handle: int) -> None:
    out = _outparam(guest, handle)
    _table_drop(guest, handle, KIND_RESPONSE_OUTPARAM)
    if not out.done:
        out.error = ErrorCode("internal-error", "response-outparam dropped without a response")
        out.done = True


def _outgoing_response(guest: Guest, handle: int) -> _OutgoingResponse:
    return _table_get(guest, handle, KIND_OUTGOING_RESPONSE)  # type: ignore[return-value]


def wasi_http_types_outgoing_response_new(guest: Guest, headers: int) -> int:
    return _table_new(guest, KIND_OUTGOING_RESPONSE, _OutgoingResponse(_take_fields(guest, headers)))


def wasi_http_types_outgoing_response_status_code(guest: Guest, self: int) -> int:
    return _outgoing_response(guest, self).status


def wasi_http_types_outgoing_response_set_status_code(guest: Guest, self: int, status: int) -> int:
    if not 100 <= status <= 599:
        return 1
    _outgoing_response(guest, self).status = status
    return 0


def wasi_http_types_outgoing_response_headers(guest: Guest, self: int) -> int:
    return _table_new(guest, KIND_FIELDS, _outgoing_response(guest, self).headers)


def wasi_http_types_outgoing_response_body(guest: Guest, comp: int, self: int, retptr: int) -> None:
    resp = _outgoing_response(guest, self)
    body = _new_body(guest, comp, resp.headers, "response", resp.body, retptr)
    if body is not None:
        resp.body = body


def wasi_http_types_outgoing_response_drop(guest: Guest, handle: int) -> None:
    _outgoing_response(guest, handle)
    _table_drop(guest, handle, KIND_OUTGOING_RESPONSE)


# errors of the io resource


def wasi_http_types_http_error_code(guest: Guest, comp: int, err: int, retptr: int) -> None:
    error = _table_get(guest, err, KIND_ERROR)
    code = getattr(error, "http_code", None)
    if code is None:
        _store_u8(guest, comp, retptr, 0)  # none: the error did not come from a body of http
        return
    _store_u8(guest, comp, retptr, 1)
    _store_error_code(guest, comp, retptr + 8, code)
