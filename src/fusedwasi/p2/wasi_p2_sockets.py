"""wasi:sockets (58 imports): `network`, `instance-network`, `ip-name-lookup`, `tcp`, `tcp-create-socket`, `udp`, `udp-create-socket` (the twin of `include/fusedwasi/p2/wasi_p2_sockets.h`). POSIX and Windows (Winsock).

**Nothing is allowed unless the caller says so** (as in Wasmtime): `Host(network=True)` allows every address and the lookup of names; `Host(socket_addr_check=f)` decides for each bind or connect
(`f(ip_text, port, use)`, `use` is a number, `USE_TCP_BIND` ... `USE_UDP_OUTGOING` of `wasi_p2_core`, as FUSEDWASI_P2_USE_* in C; `use_name(use)` gives its name), `Host(allow_ip_name_lookup=...)` for names. A refusal is `access-denied`.

The sockets are non-blocking, and the operations of WASI that are "start" and "finish" are done as the WIT lets a host do them: the system call is made in `start-*` and `finish-*` only
reports; `finish-connect` is `would-block` until the connection is made. TCP follows the states of the WIT (unbound, bind-in-progress, bound, listen-in-progress, listening,
connect-in-progress, connected, closed); what is wrong for the state is `invalid-state`, an operation that is not in progress is `not-in-progress`. Streams of a socket are
`SocketInput`/`SocketOutput`. A UDP socket is bound explicitly; `stream` with a remote address is kept in the socket and not made a kernel `connect` (datagrams from other addresses are dropped,
and a datagram has no address of its own to send), so it can be changed any number of times. Layouts (Canonical ABI): an `ip-socket-address` is 32 bytes, the tag at 0, the data at 4: IPv4 port at 4 and four bytes at 6;
IPv6 port at 4, flow-info at 8, eight u16 at 12, scope-id at 28; as a flat argument it is a tag and eleven numbers (the IPv4 uses five of them). `result<ip-socket-address>`: 36 bytes, the address at 4.
`result<_>`: tag 0, code 1. `result<handle>`: handle (or code) at 4. `result<tuple<input, output>>`: 12 bytes; `result<tuple<tcp-socket, input, output>>`: 16 bytes. `result<u64>` 16 bytes, the value
at 8; `result<u32>`: 8 bytes, at 4; `result<bool | u8>`: 2 bytes, at 1. `result<option<ip-address>>`: 22 bytes, option tag at 2, `ip-address` tag at 4, data at 6. A datagram in a list: `incoming-datagram` 40 bytes (data at 0,
address at 8), `outgoing-datagram` 44 bytes (data at 0, option tag at 8, address at 12).

Not done: the properties of a listener are not copied to the sockets it accepts (the system gives its own); a write to a socket waits when the system buffer is full
(`check-write` is a fixed amount); the lookup of a name is done at once and blocks (names with characters outside letters, digits, `.`, `-`, `_` are `invalid-argument`; no IDNA).
"""

from __future__ import annotations

import errno
import ipaddress
import select
import socket
import sys
from collections.abc import Callable
from typing import Any

from .wasi_p2_core import (
    KIND_ERROR,
    KIND_INCOMING_DATAGRAM_STREAM,
    KIND_NETWORK,
    KIND_OUTGOING_DATAGRAM_STREAM,
    KIND_POLLABLE,
    KIND_RESOLVE_STREAM,
    KIND_TCP_SOCKET,
    KIND_UDP_SOCKET,
    USE_TCP_BIND,
    USE_TCP_CONNECT,
    USE_UDP_BIND,
    USE_UDP_CONNECT,
    USE_UDP_OUTGOING,
    Guest,
    Pollable,
    State,
    _alloc,
    _load_bytes,
    _load_u8,
    _load_u16,
    _load_u32,
    _store_bytes,
    _store_u8,
    _store_u16,
    _store_u32,
    _store_u64,
    _table_drop,
    _table_get,
    _table_new,
    _trap,
)
from .wasi_p2_io import (
    InputStream,
    OutputStream,
    StreamClosed,
    StreamFailed,
    _Error,
    _input_stream_new,
    _output_stream_new,
)

_NETWORK_ERRORS = [
    "unknown", "access-denied", "not-supported", "invalid-argument", "out-of-memory", "timeout", "concurrency-conflict", "not-in-progress", "would-block",
    "invalid-state", "new-socket-limit", "address-not-bindable", "address-in-use", "remote-unreachable", "connection-refused", "connection-reset",
    "connection-aborted", "datagram-too-large", "name-unresolvable", "temporary-resolver-failure", "permanent-resolver-failure",
]  # fmt: skip
_CODE = {name: i for i, name in enumerate(_NETWORK_ERRORS)}

_ERRNO = {
    errno.EACCES: "access-denied", errno.EPERM: "access-denied", errno.EAFNOSUPPORT: "not-supported", errno.EOPNOTSUPP: "not-supported",
    errno.ENOPROTOOPT: "not-supported", errno.EPROTONOSUPPORT: "not-supported", errno.ENOMEM: "out-of-memory", errno.ENOBUFS: "out-of-memory",
    errno.ETIMEDOUT: "timeout", errno.EALREADY: "concurrency-conflict", errno.EAGAIN: "would-block", errno.EWOULDBLOCK: "would-block", errno.EINPROGRESS: "would-block",
    errno.EMFILE: "new-socket-limit", errno.ENFILE: "new-socket-limit", errno.EADDRNOTAVAIL: "address-not-bindable", errno.EADDRINUSE: "address-in-use",
    errno.EHOSTUNREACH: "remote-unreachable", errno.EHOSTDOWN: "remote-unreachable", errno.ENETUNREACH: "remote-unreachable",
    errno.ENETDOWN: "remote-unreachable", errno.ECONNREFUSED: "connection-refused", errno.ECONNRESET: "connection-reset", errno.EPIPE: "connection-reset",
    errno.ECONNABORTED: "connection-aborted", errno.EMSGSIZE: "datagram-too-large", errno.ENOTCONN: "invalid-state", errno.EISCONN: "invalid-state",
    errno.EINVAL: "invalid-argument",
}  # fmt: skip

_UNBOUND, _BIND_IN_PROGRESS, _BOUND, _LISTEN_IN_PROGRESS, _LISTENING, _CONNECT_IN_PROGRESS, _CONNECTED, _CLOSED = range(
    8
)
_CHUNK = 65536
_CHECK_SEND_PERMIT = 16
_BACKLOG_MAX = 4096


class _NetError(Exception):
    def __init__(self, name: str) -> None:
        super().__init__(name)
        self.code = _CODE[name]


def _from_oserror(exc: OSError) -> _NetError:
    return _NetError(_ERRNO.get(exc.errno or 0, "unknown"))


# addresses: (family, port, parts, flow, scope); family 0 is IPv4 (parts: four bytes), 1 is IPv6 (eight 16-bit numbers)
_Addr = tuple[int, int, tuple[int, ...], int, int]


def _from_flat(tag: int, f: tuple[int, ...]) -> _Addr:
    if tag == 0:
        return (0, f[0] & 0xFFFF, tuple(x & 0xFF for x in f[1:5]), 0, 0)
    return (1, f[0] & 0xFFFF, tuple(x & 0xFFFF for x in f[2:10]), f[1] & 0xFFFFFFFF, f[10] & 0xFFFFFFFF)


def _ip_text(a: _Addr) -> str:
    if a[0] == 0:
        return ".".join(str(x) for x in a[2])
    return str(ipaddress.IPv6Address(b"".join(x.to_bytes(2, "big") for x in a[2])))


def _sockaddr(a: _Addr) -> tuple[Any, ...]:
    return (_ip_text(a), a[1]) if a[0] == 0 else (_ip_text(a), a[1], a[3], a[4])


def _from_sockaddr(sa: tuple[Any, ...]) -> _Addr:
    ip = ipaddress.ip_address(sa[0].split("%")[0])
    if ip.version == 4:
        return (0, sa[1], tuple(ip.packed), 0, 0)
    words = tuple(int.from_bytes(ip.packed[i : i + 2], "big") for i in range(0, 16, 2))
    return (1, sa[1], words, sa[2], sa[3])


def _write_addr(guest: Guest, comp: int, ptr: int, a: _Addr) -> None:
    """The `ip-socket-address` variant at `ptr` (32 bytes)."""
    _store_u8(guest, comp, ptr, a[0])
    _store_u16(guest, comp, ptr + 4, a[1])
    if a[0] == 0:
        _store_bytes(guest, comp, ptr + 6, bytes(a[2]))
    else:
        _store_u32(guest, comp, ptr + 8, a[3])
        for i, w in enumerate(a[2]):
            _store_u16(guest, comp, ptr + 12 + 2 * i, w)
        _store_u32(guest, comp, ptr + 28, a[4])


def _read_addr(guest: Guest, comp: int, ptr: int) -> _Addr:
    tag = _load_u8(guest, comp, ptr)
    port = _load_u16(guest, comp, ptr + 4)
    if tag == 0:
        return (0, port, tuple(_load_bytes(guest, comp, ptr + 6, 4)), 0, 0)
    words = tuple(_load_u16(guest, comp, ptr + 12 + 2 * i) for i in range(8))
    return (1, port, words, _load_u32(guest, comp, ptr + 8), _load_u32(guest, comp, ptr + 28))


def _unicast(a: _Addr) -> bool:
    """A unicast address: not multicast, not the broadcast, not an IPv4-mapped IPv6 address (the unspecified address is allowed here)."""
    ip = ipaddress.ip_address(_ip_text(a))
    if ip.is_multicast:
        return False
    if a[0] == 0:
        return ip != ipaddress.IPv4Address("255.255.255.255")
    return not ipaddress.IPv6Address(_ip_text(a)).ipv4_mapped


def _remote_ok(a: _Addr) -> bool:
    return _unicast(a) and not ipaddress.ip_address(_ip_text(a)).is_unspecified and a[1] != 0


# results


def _ok(guest: Guest, comp: int, retptr: int) -> None:
    _store_u8(guest, comp, retptr, 0)


def _fail(guest: Guest, comp: int, retptr: int, at: int, exc: _NetError) -> None:
    _store_u8(guest, comp, retptr, 1)
    _store_u8(guest, comp, at, exc.code)


def _status(guest: Guest, comp: int, retptr: int, action: Callable[[], object]) -> None:
    """For a `result<_, error-code>`."""
    try:
        action()
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 1, exc)
        return
    except OSError as exc:
        _fail(guest, comp, retptr, retptr + 1, _from_oserror(exc))
        return
    _ok(guest, comp, retptr)


def _value(
    guest: Guest,
    comp: int,
    retptr: int,
    at: int,
    action: Callable[[], int],
    store: Callable[[Guest, int, int, int], None],
) -> None:
    """For a `result<number, error-code>`: the value is stored at `at`."""
    try:
        value = action()
    except _NetError as exc:
        _fail(guest, comp, retptr, at, exc)
        return
    except OSError as exc:
        _fail(guest, comp, retptr, at, _from_oserror(exc))
        return
    _ok(guest, comp, retptr)
    store(guest, comp, retptr + at, value)


# the objects


class _Network:
    pass


class _TcpSock:
    def __init__(self, family: int, sock: socket.socket | None = None, state: int = _UNBOUND) -> None:
        self.family = family
        self.sock = sock or _new_socket(family, socket.SOCK_STREAM)
        self.state = state
        self.backlog = 128
        self.recv_shut = self.send_shut = False


class _UdpSock:
    def __init__(self, family: int) -> None:
        self.family = family
        self.sock = _new_socket(family, socket.SOCK_DGRAM)
        self.state = _UNBOUND
        self.remote: _Addr | None = None
        self.closed = False


class _SocketPollable(Pollable):
    def __init__(self, ready: Callable[[], bool]) -> None:
        self.check = ready

    def ready(self, state: State) -> bool:
        return self.check()


def _new_socket(family: int, kind: int) -> socket.socket:
    try:
        sock = socket.socket(socket.AF_INET if family == 0 else socket.AF_INET6, kind)
    except OSError as exc:
        raise _from_oserror(exc) from exc
    sock.setblocking(False)
    if family == 1:
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
    return sock


def _readable(sock: socket.socket) -> bool:
    return bool(select.select([sock], [], [], 0)[0])


def _writable(sock: socket.socket) -> bool:
    _, writable, failed = select.select([], [sock], [sock], 0)
    # on Windows a connection that failed is not writable but in the third set (the system call that finishes it reports the error)
    return bool(writable) or (sys.platform == "win32" and bool(failed))


def _network(guest: Guest, handle: int) -> None:
    _table_get(guest, handle, KIND_NETWORK)


def _tcp(guest: Guest, handle: int) -> _TcpSock:
    return _table_get(guest, handle, KIND_TCP_SOCKET)  # type: ignore[return-value]


def _udp(guest: Guest, handle: int) -> _UdpSock:
    return _table_get(guest, handle, KIND_UDP_SOCKET)  # type: ignore[return-value]


# network


def wasi_sockets_network_network_drop(guest: Guest, handle: int) -> None:
    _table_drop(guest, handle, KIND_NETWORK)


def wasi_sockets_network_network_error_code(guest: Guest, comp: int, err: int, retptr: int) -> None:
    error: _Error = _table_get(guest, err, KIND_ERROR)  # type: ignore[assignment]
    if error.network_code is None:
        _store_u8(guest, comp, retptr, 0)
    else:
        _store_u8(guest, comp, retptr, 1)
        _store_u8(guest, comp, retptr + 1, error.network_code)


def wasi_sockets_instance_network_instance_network(guest: Guest) -> int:
    return _table_new(guest, KIND_NETWORK, _Network())


# the socket streams


class _SocketInput(InputStream):
    def __init__(self, t: _TcpSock) -> None:
        self.t = t

    def ready(self) -> bool:
        return self.t.recv_shut or self.t.state == _CLOSED or _readable(self.t.sock)

    def read(self, n: int) -> bytes:
        if self.t.recv_shut or self.t.state == _CLOSED:
            raise StreamClosed
        if n == 0:
            return b""
        try:
            data = self.t.sock.recv(n)
        except BlockingIOError:
            return b""
        except OSError as exc:
            net = _from_oserror(exc)
            raise StreamFailed(exc.strerror or "read failed", network_code=net.code) from exc
        if not data:
            raise StreamClosed
        return data


class _SocketOutput(OutputStream):
    def __init__(self, t: _TcpSock) -> None:
        self.t = t

    def check_write(self) -> int:
        if self.t.send_shut or self.t.state == _CLOSED:
            raise StreamClosed
        return _CHUNK

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        try:
            while len(view):
                try:
                    view = view[self.t.sock.send(view) :]
                except BlockingIOError:
                    select.select([], [self.t.sock], [], 0.05)  # the system buffer is full: wait for room
        except OSError as exc:
            net = _from_oserror(exc)
            raise StreamFailed(exc.strerror or "write failed", network_code=net.code) from exc

    def ready(self) -> bool:
        return self.t.send_shut or self.t.state == _CLOSED or _writable(self.t.sock)


# tcp-create-socket


def wasi_sockets_tcp_create_socket_create_tcp_socket(guest: Guest, comp: int, address_family: int, retptr: int) -> None:
    try:
        handle = _table_new(guest, KIND_TCP_SOCKET, _TcpSock(address_family))
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, handle)


def wasi_sockets_tcp_tcp_socket_drop(guest: Guest, handle: int) -> None:
    t = _tcp(guest, handle)
    _table_drop(guest, handle, KIND_TCP_SOCKET)
    t.state = _CLOSED
    t.sock.close()


def _open(t: _TcpSock) -> None:
    if t.state == _CLOSED:
        raise _NetError("invalid-state")


def _check_bind(guest: Guest, family: int, a: _Addr, use: int) -> None:
    if a[0] != family or not _unicast(a):
        raise _NetError("invalid-argument")
    if not guest.state.socket_addr_check(_ip_text(a), a[1], use):
        raise _NetError("access-denied")


def _check_connect(guest: Guest, family: int, a: _Addr, use: int) -> None:
    if a[0] != family or not _remote_ok(a):
        raise _NetError("invalid-argument")
    if not guest.state.socket_addr_check(_ip_text(a), a[1], use):
        raise _NetError("access-denied")


def wasi_sockets_tcp_tcp_socket_start_bind(
    guest: Guest, comp: int, self: int, network: int, tag: int, p1: int, p2: int, p3: int, p4: int, p5: int, p6: int, p7: int, p8: int, p9: int, p10: int, p11: int, retptr: int
) -> None:  # fmt: skip
    t = _tcp(guest, self)
    _network(guest, network)
    a = _from_flat(tag, (p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11))

    def action() -> None:
        if t.state != _UNBOUND:
            raise _NetError("invalid-state")
        _check_bind(guest, t.family, a, USE_TCP_BIND)
        if sys.platform != "win32":
            t.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        t.sock.bind(_sockaddr(a))
        t.state = _BIND_IN_PROGRESS

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_finish_bind(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        if t.state != _BIND_IN_PROGRESS:
            raise _NetError("not-in-progress")
        t.state = _BOUND

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_start_connect(
    guest: Guest, comp: int, self: int, network: int, tag: int, p1: int, p2: int, p3: int, p4: int, p5: int, p6: int, p7: int, p8: int, p9: int, p10: int, p11: int, retptr: int
) -> None:  # fmt: skip
    t = _tcp(guest, self)
    _network(guest, network)
    a = _from_flat(tag, (p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11))

    def action() -> None:
        if t.state not in (_UNBOUND, _BOUND):
            raise _NetError("invalid-state")
        _check_connect(guest, t.family, a, USE_TCP_CONNECT)
        err = t.sock.connect_ex(_sockaddr(a))
        if err not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK):
            t.state = _CLOSED
            raise _from_oserror(OSError(err, ""))
        t.state = _CONNECT_IN_PROGRESS

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_finish_connect(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)
    try:
        if t.state != _CONNECT_IN_PROGRESS:
            raise _NetError("not-in-progress")
        if not _writable(t.sock):
            raise _NetError("would-block")
        err = t.sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err:
            t.state = _CLOSED
            raise _from_oserror(OSError(err, ""))
        t.state = _CONNECTED
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, _input_stream_new(guest, _SocketInput(t)))
    _store_u32(guest, comp, retptr + 8, _output_stream_new(guest, _SocketOutput(t)))


def wasi_sockets_tcp_tcp_socket_start_listen(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        if t.state != _BOUND:
            raise _NetError("invalid-state")
        t.sock.listen(t.backlog)
        t.state = _LISTEN_IN_PROGRESS

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_finish_listen(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        if t.state != _LISTEN_IN_PROGRESS:
            raise _NetError("not-in-progress")
        t.state = _LISTENING

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_accept(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)
    try:
        if t.state != _LISTENING:
            raise _NetError("invalid-state")
        try:
            conn, _ = t.sock.accept()
        except BlockingIOError as exc:
            raise _NetError("would-block") from exc
        except OSError as exc:
            raise _from_oserror(exc) from exc
        conn.setblocking(False)
        new = _TcpSock(t.family, conn, _CONNECTED)
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, _table_new(guest, KIND_TCP_SOCKET, new))
    _store_u32(guest, comp, retptr + 8, _input_stream_new(guest, _SocketInput(new)))
    _store_u32(guest, comp, retptr + 12, _output_stream_new(guest, _SocketOutput(new)))


def _address_result(guest: Guest, comp: int, retptr: int, action: Callable[[], _Addr]) -> None:
    try:
        a = action()
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    except OSError as exc:
        _fail(guest, comp, retptr, retptr + 4, _from_oserror(exc))
        return
    _ok(guest, comp, retptr)
    _write_addr(guest, comp, retptr + 4, a)


def wasi_sockets_tcp_tcp_socket_local_address(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> _Addr:
        if t.state in (_UNBOUND, _BIND_IN_PROGRESS, _CLOSED):
            raise _NetError("invalid-state")
        return _from_sockaddr(t.sock.getsockname())

    _address_result(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_remote_address(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> _Addr:
        if t.state != _CONNECTED:
            raise _NetError("invalid-state")
        return _from_sockaddr(t.sock.getpeername())

    _address_result(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_is_listening(guest: Guest, self: int) -> int:
    return int(_tcp(guest, self).state == _LISTENING)


def wasi_sockets_tcp_tcp_socket_address_family(guest: Guest, self: int) -> int:
    return _tcp(guest, self).family


def wasi_sockets_tcp_tcp_socket_set_listen_backlog_size(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        _open(t)
        if value == 0:
            raise _NetError("invalid-argument")
        if t.state in (_CONNECT_IN_PROGRESS, _CONNECTED):
            raise _NetError("invalid-state")
        t.backlog = min(value, _BACKLOG_MAX)
        if t.state == _LISTENING:
            t.sock.listen(t.backlog)

    _status(guest, comp, retptr, action)


_WINDOWS_TCP_OPTIONS = {"TCP_KEEPIDLE": 3, "TCP_KEEPCNT": 16, "TCP_KEEPINTVL": 17}


def _sockopt(sock: socket.socket, level: int, name: str, alt: str | None = None) -> tuple[int, int]:
    option = getattr(socket, name, None) if hasattr(socket, name) else (getattr(socket, alt, None) if alt else None)
    if option is None and sys.platform == "win32":
        option = _WINDOWS_TCP_OPTIONS.get(name)  # Windows 10 1709 has them; Python has the names from 3.12
    if option is None:
        raise _NetError("not-supported")
    return level, option


def _seconds(ns: int) -> int:
    """A duration in nanoseconds as whole seconds for the system: rounded up, at least 1."""
    return max(1, min((ns + 999_999_999) // 1_000_000_000, 2**31 - 1))


def wasi_sockets_tcp_tcp_socket_keep_alive_enabled(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def get() -> int:
        _open(t)
        return int(t.sock.getsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE) != 0)

    _value(guest, comp, retptr, 1, get, _store_u8)


def wasi_sockets_tcp_tcp_socket_set_keep_alive_enabled(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        _open(t)
        t.sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, int(bool(value)))

    _status(guest, comp, retptr, action)


def _keepalive(guest: Guest, comp: int, retptr: int, self: int, name: str, alt: str | None, factor: int) -> None:
    t = _tcp(guest, self)

    def get() -> int:
        _open(t)
        level, option = _sockopt(t.sock, socket.IPPROTO_TCP, name, alt)
        return t.sock.getsockopt(level, option) * factor

    _value(guest, comp, retptr, 8, get, _store_u64)


def _set_keepalive(
    guest: Guest, comp: int, retptr: int, self: int, value: int, name: str, alt: str | None, seconds: bool
) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        _open(t)
        if value == 0:
            raise _NetError("invalid-argument")
        level, option = _sockopt(t.sock, socket.IPPROTO_TCP, name, alt)
        t.sock.setsockopt(level, option, _seconds(value) if seconds else min(value, 2**31 - 1))

    _status(guest, comp, retptr, action)


def wasi_sockets_tcp_tcp_socket_keep_alive_idle_time(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _keepalive(guest, comp, retptr, self, "TCP_KEEPIDLE", "TCP_KEEPALIVE", 1_000_000_000)


def wasi_sockets_tcp_tcp_socket_set_keep_alive_idle_time(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    _set_keepalive(guest, comp, retptr, self, value, "TCP_KEEPIDLE", "TCP_KEEPALIVE", True)


def wasi_sockets_tcp_tcp_socket_keep_alive_interval(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _keepalive(guest, comp, retptr, self, "TCP_KEEPINTVL", None, 1_000_000_000)


def wasi_sockets_tcp_tcp_socket_set_keep_alive_interval(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    _set_keepalive(guest, comp, retptr, self, value, "TCP_KEEPINTVL", None, True)


def wasi_sockets_tcp_tcp_socket_keep_alive_count(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def get() -> int:
        _open(t)
        level, option = _sockopt(t.sock, socket.IPPROTO_TCP, "TCP_KEEPCNT")
        return t.sock.getsockopt(level, option)

    _value(guest, comp, retptr, 4, get, _store_u32)


def wasi_sockets_tcp_tcp_socket_set_keep_alive_count(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    _set_keepalive(guest, comp, retptr, self, value, "TCP_KEEPCNT", None, False)


def _hop_get(sock: socket.socket, family: int) -> int:
    level, option = (
        (socket.IPPROTO_IP, socket.IP_TTL) if family == 0 else (socket.IPPROTO_IPV6, socket.IPV6_UNICAST_HOPS)
    )
    return min(sock.getsockopt(level, option), 255)


def _hop_set(sock: socket.socket, family: int, value: int) -> None:
    if value & 0xFF == 0:
        raise _NetError("invalid-argument")
    level, option = (
        (socket.IPPROTO_IP, socket.IP_TTL) if family == 0 else (socket.IPPROTO_IPV6, socket.IPV6_UNICAST_HOPS)
    )
    sock.setsockopt(level, option, value & 0xFF)


def _buffer_set(sock: socket.socket, option: int, value: int) -> None:
    if value == 0:
        raise _NetError("invalid-argument")
    sock.setsockopt(socket.SOL_SOCKET, option, min(value, 2**31 - 1))


def wasi_sockets_tcp_tcp_socket_hop_limit(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)
    _value(guest, comp, retptr, 1, lambda: (_open(t), _hop_get(t.sock, t.family))[1], _store_u8)


def wasi_sockets_tcp_tcp_socket_set_hop_limit(guest: Guest, comp: int, self: int, value: int, retptr: int) -> None:
    t = _tcp(guest, self)
    _status(guest, comp, retptr, lambda: (_open(t), _hop_set(t.sock, t.family, value)))


def wasi_sockets_tcp_tcp_socket_receive_buffer_size(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)
    _value(
        guest,
        comp,
        retptr,
        8,
        lambda: (_open(t), t.sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF))[1],
        _store_u64,
    )


def wasi_sockets_tcp_tcp_socket_set_receive_buffer_size(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    t = _tcp(guest, self)
    _status(guest, comp, retptr, lambda: (_open(t), _buffer_set(t.sock, socket.SO_RCVBUF, value)))


def wasi_sockets_tcp_tcp_socket_send_buffer_size(guest: Guest, comp: int, self: int, retptr: int) -> None:
    t = _tcp(guest, self)
    _value(
        guest,
        comp,
        retptr,
        8,
        lambda: (_open(t), t.sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF))[1],
        _store_u64,
    )


def wasi_sockets_tcp_tcp_socket_set_send_buffer_size(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    t = _tcp(guest, self)
    _status(guest, comp, retptr, lambda: (_open(t), _buffer_set(t.sock, socket.SO_SNDBUF, value)))


def wasi_sockets_tcp_tcp_socket_subscribe(guest: Guest, self: int) -> int:
    t = _tcp(guest, self)

    def ready() -> bool:
        if t.state == _CONNECT_IN_PROGRESS:
            return _writable(t.sock)
        if t.state == _LISTENING:
            return _readable(t.sock)
        return True

    return _table_new(guest, KIND_POLLABLE, _SocketPollable(ready))


def wasi_sockets_tcp_tcp_socket_shutdown(guest: Guest, comp: int, self: int, shutdown_type: int, retptr: int) -> None:
    t = _tcp(guest, self)

    def action() -> None:
        if t.state != _CONNECTED:
            raise _NetError("invalid-state")
        how = (socket.SHUT_RD, socket.SHUT_WR, socket.SHUT_RDWR)[shutdown_type]
        try:
            t.sock.shutdown(how)
        except OSError as exc:
            if exc.errno != errno.ENOTCONN:
                raise
        t.recv_shut |= how != socket.SHUT_WR
        t.send_shut |= how != socket.SHUT_RD

    _status(guest, comp, retptr, action)


# udp-create-socket and udp


def wasi_sockets_udp_create_socket_create_udp_socket(guest: Guest, comp: int, address_family: int, retptr: int) -> None:
    try:
        handle = _table_new(guest, KIND_UDP_SOCKET, _UdpSock(address_family))
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, handle)


def wasi_sockets_udp_udp_socket_drop(guest: Guest, handle: int) -> None:
    u = _udp(guest, handle)
    _table_drop(guest, handle, KIND_UDP_SOCKET)
    u.closed = True
    u.sock.close()


def _uopen(u: _UdpSock) -> None:
    if u.closed:
        raise _NetError("invalid-state")


def wasi_sockets_udp_udp_socket_start_bind(
    guest: Guest, comp: int, self: int, network: int, tag: int, p1: int, p2: int, p3: int, p4: int, p5: int, p6: int, p7: int, p8: int, p9: int, p10: int, p11: int, retptr: int
) -> None:  # fmt: skip
    u = _udp(guest, self)
    _network(guest, network)
    a = _from_flat(tag, (p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11))

    def action() -> None:
        if u.state != _UNBOUND:
            raise _NetError("invalid-state")
        _check_bind(guest, u.family, a, USE_UDP_BIND)
        u.sock.bind(_sockaddr(a))
        u.state = _BIND_IN_PROGRESS

    _status(guest, comp, retptr, action)


def wasi_sockets_udp_udp_socket_finish_bind(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)

    def action() -> None:
        if u.state != _BIND_IN_PROGRESS:
            raise _NetError("not-in-progress")
        u.state = _BOUND

    _status(guest, comp, retptr, action)


class _IncomingDatagrams:
    def __init__(self, u: _UdpSock) -> None:
        self.u = u


class _OutgoingDatagrams:
    def __init__(self, u: _UdpSock) -> None:
        self.u = u
        self.permit = 0


def wasi_sockets_udp_udp_socket_stream(
    guest: Guest, comp: int, self: int, remote_tag: int, tag: int, p1: int, p2: int, p3: int, p4: int, p5: int, p6: int, p7: int, p8: int, p9: int, p10: int, p11: int, retptr: int
) -> None:  # fmt: skip
    u = _udp(guest, self)
    a = _from_flat(tag, (p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11)) if remote_tag else None
    try:
        _uopen(u)
        if u.state != _BOUND:
            raise _NetError("invalid-state")
        if a is not None:
            _check_connect(guest, u.family, a, USE_UDP_CONNECT)
        u.remote = a
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, _table_new(guest, KIND_INCOMING_DATAGRAM_STREAM, _IncomingDatagrams(u)))
    _store_u32(guest, comp, retptr + 8, _table_new(guest, KIND_OUTGOING_DATAGRAM_STREAM, _OutgoingDatagrams(u)))


def wasi_sockets_udp_udp_socket_local_address(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)

    def action() -> _Addr:
        if u.state != _BOUND or u.closed:
            raise _NetError("invalid-state")
        return _from_sockaddr(u.sock.getsockname())

    _address_result(guest, comp, retptr, action)


def wasi_sockets_udp_udp_socket_remote_address(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)

    def action() -> _Addr:
        if u.remote is None or u.closed:
            raise _NetError("invalid-state")
        return u.remote

    _address_result(guest, comp, retptr, action)


def wasi_sockets_udp_udp_socket_address_family(guest: Guest, self: int) -> int:
    return _udp(guest, self).family


def wasi_sockets_udp_udp_socket_unicast_hop_limit(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)
    _value(guest, comp, retptr, 1, lambda: (_uopen(u), _hop_get(u.sock, u.family))[1], _store_u8)


def wasi_sockets_udp_udp_socket_set_unicast_hop_limit(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    u = _udp(guest, self)
    _status(guest, comp, retptr, lambda: (_uopen(u), _hop_set(u.sock, u.family, value)))


def wasi_sockets_udp_udp_socket_receive_buffer_size(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)
    _value(
        guest,
        comp,
        retptr,
        8,
        lambda: (_uopen(u), u.sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF))[1],
        _store_u64,
    )


def wasi_sockets_udp_udp_socket_set_receive_buffer_size(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    u = _udp(guest, self)
    _status(guest, comp, retptr, lambda: (_uopen(u), _buffer_set(u.sock, socket.SO_RCVBUF, value)))


def wasi_sockets_udp_udp_socket_send_buffer_size(guest: Guest, comp: int, self: int, retptr: int) -> None:
    u = _udp(guest, self)
    _value(
        guest,
        comp,
        retptr,
        8,
        lambda: (_uopen(u), u.sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF))[1],
        _store_u64,
    )


def wasi_sockets_udp_udp_socket_set_send_buffer_size(
    guest: Guest, comp: int, self: int, value: int, retptr: int
) -> None:
    u = _udp(guest, self)
    _status(guest, comp, retptr, lambda: (_uopen(u), _buffer_set(u.sock, socket.SO_SNDBUF, value)))


def wasi_sockets_udp_udp_socket_subscribe(guest: Guest, self: int) -> int:
    u = _udp(guest, self)
    return _table_new(guest, KIND_POLLABLE, _SocketPollable(lambda: _readable(u.sock) or _writable(u.sock)))


def wasi_sockets_udp_incoming_datagram_stream_drop(guest: Guest, handle: int) -> None:
    _table_drop(guest, handle, KIND_INCOMING_DATAGRAM_STREAM)


def wasi_sockets_udp_outgoing_datagram_stream_drop(guest: Guest, handle: int) -> None:
    _table_drop(guest, handle, KIND_OUTGOING_DATAGRAM_STREAM)


def _incoming(guest: Guest, handle: int) -> _IncomingDatagrams:
    return _table_get(guest, handle, KIND_INCOMING_DATAGRAM_STREAM)  # type: ignore[return-value]


def _outgoing(guest: Guest, handle: int) -> _OutgoingDatagrams:
    return _table_get(guest, handle, KIND_OUTGOING_DATAGRAM_STREAM)  # type: ignore[return-value]


def wasi_sockets_udp_incoming_datagram_stream_receive(
    guest: Guest, comp: int, self: int, max_results: int, retptr: int
) -> None:
    u = _incoming(guest, self).u
    received: list[tuple[bytes, _Addr]] = []
    try:
        _uopen(u)
        while len(received) < max_results:
            try:
                data, sa = u.sock.recvfrom(65536)
            except BlockingIOError:
                break
            except OSError as exc:
                if isinstance(exc, ConnectionResetError) and sys.platform == "win32":
                    continue  # Windows tells a receive about an earlier datagram that reached a closed port (no other system does)
                if received:
                    break
                raise _from_oserror(exc) from exc
            src = _from_sockaddr(sa)
            if u.remote is not None and src[:3] != u.remote[:3]:
                continue  # a datagram from another address than the one the stream is for
            received.append((data, src))
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    ptr = _alloc(guest, comp, 4, 40 * len(received))
    for i, (data, src) in enumerate(received):
        dptr = _alloc(guest, comp, 1, len(data))
        _store_bytes(guest, comp, dptr, data)
        _store_u32(guest, comp, ptr + 40 * i, dptr)
        _store_u32(guest, comp, ptr + 40 * i + 4, len(data))
        _write_addr(guest, comp, ptr + 40 * i + 8, src)
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, ptr)
    _store_u32(guest, comp, retptr + 8, len(received))


def wasi_sockets_udp_incoming_datagram_stream_subscribe(guest: Guest, self: int) -> int:
    u = _incoming(guest, self).u
    return _table_new(guest, KIND_POLLABLE, _SocketPollable(lambda: _readable(u.sock)))


def wasi_sockets_udp_outgoing_datagram_stream_check_send(guest: Guest, comp: int, self: int, retptr: int) -> None:
    o = _outgoing(guest, self)
    try:
        _uopen(o.u)
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    o.permit = _CHECK_SEND_PERMIT
    _ok(guest, comp, retptr)
    _store_u64(guest, comp, retptr + 8, _CHECK_SEND_PERMIT)


def wasi_sockets_udp_outgoing_datagram_stream_send(
    guest: Guest, comp: int, self: int, ptr: int, length: int, retptr: int
) -> None:
    o = _outgoing(guest, self)
    u = o.u
    if length > o.permit:
        _trap(guest, "send of more datagrams than check-send permitted")
    o.permit = 0
    sent = 0
    try:
        _uopen(u)
        for i in range(length):
            base = ptr + 44 * i
            data = _load_bytes(guest, comp, _load_u32(guest, comp, base), _load_u32(guest, comp, base + 4))
            has_remote = _load_u8(guest, comp, base + 8)
            try:
                if u.remote is not None:
                    if has_remote and _read_addr(guest, comp, base + 12) != u.remote:
                        raise _NetError(
                            "invalid-argument"
                        )  # the stream has its peer: the datagram names none, or exactly that one
                    target = u.remote
                else:
                    if not has_remote:
                        raise _NetError("invalid-argument")
                    target = _read_addr(guest, comp, base + 12)
                    _check_connect(guest, u.family, target, USE_UDP_OUTGOING)
                try:
                    u.sock.sendto(data, _sockaddr(target))
                except BlockingIOError:
                    break  # the system cannot take it now: the rest is not sent
                except OSError as exc:
                    raise _from_oserror(exc) from exc
            except _NetError:
                if sent:
                    break  # an error after something was sent is not reported
                raise
            sent += 1
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    _store_u64(guest, comp, retptr + 8, sent)


def wasi_sockets_udp_outgoing_datagram_stream_subscribe(guest: Guest, self: int) -> int:
    u = _outgoing(guest, self).u
    return _table_new(guest, KIND_POLLABLE, _SocketPollable(lambda: _writable(u.sock)))


# ip-name-lookup


class _Resolved:
    def __init__(self, addresses: list[tuple[int, tuple[int, ...]]]) -> None:
        self.addresses, self.pos = addresses, 0


_NAME_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_")
_GAI = {
    socket.EAI_NONAME: "name-unresolvable", socket.EAI_AGAIN: "temporary-resolver-failure", socket.EAI_FAIL: "permanent-resolver-failure",
}  # fmt: skip


def wasi_sockets_ip_name_lookup_resolve_addresses(
    guest: Guest, comp: int, network: int, ptr: int, length: int, retptr: int
) -> None:
    _network(guest, network)
    raw = _load_bytes(guest, comp, ptr, length)
    try:
        if not guest.state.allow_ip_name_lookup:
            raise _NetError("access-denied")
        try:
            name = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise _NetError("invalid-argument") from exc
        if not name or "%" in name:
            raise _NetError("invalid-argument")
        found: list[tuple[int, tuple[int, ...]]] = []
        try:
            ip = ipaddress.ip_address(name)
        except ValueError:
            if not set(name) <= _NAME_CHARS:
                raise _NetError("invalid-argument") from None
            try:
                infos = socket.getaddrinfo(name, None, socket.AF_UNSPEC, socket.SOCK_STREAM)
            except socket.gaierror as exc:
                raise _NetError(_GAI.get(exc.errno or 0, "name-unresolvable")) from exc
            for info in infos:
                a = _from_sockaddr(info[4])
                if (a[0], a[2]) not in found:
                    found.append((a[0], a[2]))
        else:
            found.append(
                (0, tuple(ip.packed))
                if ip.version == 4
                else (1, tuple(int.from_bytes(ip.packed[i : i + 2], "big") for i in range(0, 16, 2)))
            )
        handle = _table_new(guest, KIND_RESOLVE_STREAM, _Resolved(found))
    except _NetError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _store_u32(guest, comp, retptr + 4, handle)


def wasi_sockets_ip_name_lookup_resolve_address_stream_resolve_next_address(
    guest: Guest, comp: int, self: int, retptr: int
) -> None:
    r: _Resolved = _table_get(guest, self, KIND_RESOLVE_STREAM)  # type: ignore[assignment]
    _ok(guest, comp, retptr)
    if r.pos >= len(r.addresses):
        _store_u8(guest, comp, retptr + 2, 0)  # none
        return
    family, parts = r.addresses[r.pos]
    r.pos += 1
    _store_u8(guest, comp, retptr + 2, 1)
    _store_u8(guest, comp, retptr + 4, family)
    if family == 0:
        _store_bytes(guest, comp, retptr + 6, bytes(parts))
    else:
        for i, w in enumerate(parts):
            _store_u16(guest, comp, retptr + 6 + 2 * i, w)


def wasi_sockets_ip_name_lookup_resolve_address_stream_subscribe(guest: Guest, self: int) -> int:
    _table_get(guest, self, KIND_RESOLVE_STREAM)
    return _table_new(guest, KIND_POLLABLE, _SocketPollable(lambda: True))


def wasi_sockets_ip_name_lookup_resolve_address_stream_drop(guest: Guest, handle: int) -> None:
    _table_drop(guest, handle, KIND_RESOLVE_STREAM)
