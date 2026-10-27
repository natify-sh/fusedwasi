"""wasi:io (23 imports): `error`, `poll` and `streams` (the twin of `include/fusedwasi/p2/wasi_p2_io.h`).

Names carry the interface and the resource: `wasi_io_streams_input_stream_read`. A result that does not fit in one flat value is written through the pointer that is
the last argument (the canonical ABI). The layouts used here:

- `result<list<u8>, stream-error>`: 12 bytes, tag at 0, the list (pointer, length) at 4; `result<_, stream-error>`: 12 bytes; `result<u64, stream-error>`: 16 bytes, the u64 at 8.
- `stream-error`: a variant {last-operation-failed(own<error>), closed} of 8 bytes: tag at 0, the handle at 4; it is at 4 in the results of 12 bytes and at 8 in the one of 16.

A stream of the host is an `InputStream` or an `OutputStream` (any object with these methods; `MemoryInput` and `BufferOutput` are here, files and sockets come with their
packages). `read` returns at most `n` bytes (`b""` if none is ready), raises `StreamClosed` at the end and `StreamFailed(message)` when the operation failed; a write is
limited by `check_write`, and a write of more than that is a trap.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from .wasi_p2_core import (
    KIND_ERROR,
    KIND_INPUT_STREAM,
    KIND_OUTPUT_STREAM,
    KIND_POLLABLE,
    Guest,
    Pollable,
    State,
    alloc,
    load_bytes,
    load_u32,
    store_bytes,
    store_u8,
    store_u32,
    store_u64,
    table_add,
    table_drop,
    table_get,
    table_new,
    trap,
)

READ_CAP = 1 << 16  # the most a call of `read` gives back


class StreamClosed(Exception):
    """The stream has ended, or was closed: the `closed` case of `stream-error`."""


class StreamFailed(Exception):
    """The operation failed: the case `last-operation-failed`, with an `error` resource that carries the message (and, from a file, the `error-code` of wasi:filesystem)."""

    def __init__(
        self, message: str = "", code: int | None = None, network_code: int | None = None, errno: int | None = None
    ) -> None:
        super().__init__(message)
        self.errno = (
            errno  # the errno of the system, when the failure came from it: WASI 0.1 gives it to the program as it is
        )
        self.code = code
        self.network_code = network_code  # the `error-code` of wasi:sockets, for a socket stream


def _call(stream: object, fn: Callable[..., Any], *args: Any) -> Any:
    """Calls a method of a stream under the rule of `stream-error`: after `last-operation-failed` the stream is closed, and every later call is `closed`."""
    if getattr(stream, "dead", False):
        raise StreamClosed
    try:
        return fn(*args)
    except StreamFailed:
        stream.dead = True  # type: ignore[attr-defined]
        raise


class InputStream:
    dead = False

    def read(self, n: int) -> bytes:
        raise NotImplementedError

    def ready(self) -> bool:
        return True

    def close(self) -> None:
        pass


class OutputStream:
    dead = False

    def check_write(self) -> int:
        raise NotImplementedError

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def flush(self) -> None:
        pass

    def ready(self) -> bool:
        return self.check_write() > 0

    def close(self) -> None:
        pass


class MemoryInput(InputStream):
    """Reads from bytes it was given; closed when they are all read."""

    def __init__(self, data: bytes) -> None:
        self.data, self.pos = bytes(data), 0

    def read(self, n: int) -> bytes:
        if self.pos >= len(self.data) and n:
            raise StreamClosed
        chunk = self.data[self.pos : self.pos + n]
        self.pos += len(chunk)
        return chunk


class BufferOutput(OutputStream):
    """Collects what is written in `data`; takes at most `limit` bytes in all."""

    def __init__(self, limit: int = 65536) -> None:
        self.data, self.limit = bytearray(), limit

    def check_write(self) -> int:
        return self.limit - len(self.data)

    def write(self, data: bytes) -> None:
        self.data += data


class StreamPollable(Pollable):
    def __init__(self, stream: InputStream | OutputStream) -> None:
        self.stream = stream

    def ready(self, state: State) -> bool:
        return bool(getattr(self.stream, "dead", False)) or self.stream.ready()  # a closed stream is always ready


class Error:
    """The `error` resource: a message, and the `error-code` of wasi:filesystem if the error came from a file (`filesystem-error-code` reads it)."""

    __slots__ = ("code", "message", "network_code")

    def __init__(self, message: str, code: int | None = None, network_code: int | None = None) -> None:
        self.message = message
        self.code = code
        self.network_code = network_code


# making resources for the guest


def error_new(guest: Guest, message: str, code: int | None = None, network_code: int | None = None) -> int:
    return table_new(guest, KIND_ERROR, Error(message, code, network_code))


def error_add(state: State, message: str, code: int | None = None, network_code: int | None = None) -> int:
    """An `error` resource in the table of the state (no guest needed): the handle, or 0 when the table is full."""
    return table_add(state, KIND_ERROR, Error(message, code, network_code))


def input_stream_add(state: State, stream: InputStream) -> int:
    """A stream of the caller in the table of the state as an input stream (no guest needed): the handle, or 0 when the table is full."""
    return table_add(state, KIND_INPUT_STREAM, stream)


def output_stream_add(state: State, stream: OutputStream) -> int:
    return table_add(state, KIND_OUTPUT_STREAM, stream)


def input_stream_new(guest: Guest, stream: InputStream) -> int:
    return table_new(guest, KIND_INPUT_STREAM, stream)


def output_stream_new(guest: Guest, stream: OutputStream) -> int:
    return table_new(guest, KIND_OUTPUT_STREAM, stream)


# error


def wasi_io_error_error_to_debug_string(guest: Guest, comp: int, self: int, retptr: int) -> None:
    error: Error = table_get(guest, self, KIND_ERROR)  # type: ignore[assignment]
    data = error.message.encode()
    ptr = alloc(guest, comp, 1, len(data))
    store_bytes(guest, comp, ptr, data)
    store_u32(guest, comp, retptr, ptr)
    store_u32(guest, comp, retptr + 4, len(data))


def wasi_io_error_error_drop(guest: Guest, handle: int) -> None:
    table_drop(guest, handle, KIND_ERROR)


# poll


def _pollable(guest: Guest, handle: int) -> Pollable:
    return table_get(guest, handle, KIND_POLLABLE)  # type: ignore[return-value]


def wasi_io_poll_pollable_ready(guest: Guest, self: int) -> int:
    return int(_pollable(guest, self).ready(guest.state))


def wasi_io_poll_pollable_block(guest: Guest, self: int) -> None:
    pollable = _pollable(guest, self)
    while not pollable.ready(guest.state):
        guest.wait()


def wasi_io_poll_pollable_drop(guest: Guest, handle: int) -> None:
    table_drop(guest, handle, KIND_POLLABLE)


def wasi_io_poll_poll(guest: Guest, comp: int, in_ptr: int, in_len: int, retptr: int) -> None:
    """Waits until one of the pollables is ready and gives the indexes of all that are (a list of u32). An empty list is a trap."""
    if in_len == 0:
        trap(guest, "poll of an empty list")
    pollables = [_pollable(guest, load_u32(guest, comp, in_ptr + 4 * i)) for i in range(in_len)]
    while not (ready := [i for i, p in enumerate(pollables) if p.ready(guest.state)]):
        guest.wait()
    ptr = alloc(guest, comp, 4, 4 * len(ready))
    for k, i in enumerate(ready):
        store_u32(guest, comp, ptr + 4 * k, i)
    store_u32(guest, comp, retptr, ptr)
    store_u32(guest, comp, retptr + 4, len(ready))


# streams


def _input(guest: Guest, handle: int) -> InputStream:
    return table_get(guest, handle, KIND_INPUT_STREAM)  # type: ignore[return-value]


def _output(guest: Guest, handle: int) -> OutputStream:
    return table_get(guest, handle, KIND_OUTPUT_STREAM)  # type: ignore[return-value]


def _ok(guest: Guest, comp: int, retptr: int) -> None:
    store_u8(guest, comp, retptr, 0)


def _err(guest: Guest, comp: int, retptr: int, error_at: int, exc: Exception) -> None:
    """Writes `err(stream-error)`: the tag of the result at `retptr`, the variant at `error_at`."""
    store_u8(guest, comp, retptr, 1)
    if isinstance(exc, StreamFailed):
        store_u8(guest, comp, error_at, 0)
        store_u32(guest, comp, error_at + 4, error_new(guest, str(exc), exc.code, exc.network_code))
    else:
        store_u8(guest, comp, error_at, 1)


def _read(guest: Guest, stream: InputStream, n: int, block: bool) -> bytes:
    """Up to `n` bytes; when `block`, waits for at least one (a read of 0 bytes is empty at once)."""
    while True:
        data = _call(stream, stream.read, min(n, READ_CAP))
        if data or not block or n == 0:
            return data
        guest.wait()


def wasi_io_streams_input_stream_read(guest: Guest, comp: int, self: int, len: int, retptr: int) -> None:
    _read_list(guest, comp, self, len, retptr, False)


def wasi_io_streams_input_stream_blocking_read(guest: Guest, comp: int, self: int, len: int, retptr: int) -> None:
    _read_list(guest, comp, self, len, retptr, True)


def _read_list(guest: Guest, comp: int, self: int, n: int, retptr: int, block: bool) -> None:
    stream = _input(guest, self)
    try:
        data = _read(guest, stream, n, block)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 4, exc)
        return
    ptr = alloc(guest, comp, 1, len(data))
    store_bytes(guest, comp, ptr, data)
    _ok(guest, comp, retptr)
    store_u32(guest, comp, retptr + 4, ptr)
    store_u32(guest, comp, retptr + 8, len(data))


def _skip(guest: Guest, comp: int, self: int, n: int, retptr: int, block: bool) -> None:
    stream = _input(guest, self)
    try:
        done = len(_read(guest, stream, n, block))
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    store_u64(guest, comp, retptr + 8, done)


def wasi_io_streams_input_stream_skip(guest: Guest, comp: int, self: int, len: int, retptr: int) -> None:
    _skip(guest, comp, self, len, retptr, False)


def wasi_io_streams_input_stream_blocking_skip(guest: Guest, comp: int, self: int, len: int, retptr: int) -> None:
    _skip(guest, comp, self, len, retptr, True)


def wasi_io_streams_input_stream_subscribe(guest: Guest, self: int) -> int:
    return table_new(guest, KIND_POLLABLE, StreamPollable(_input(guest, self)))


def wasi_io_streams_input_stream_drop(guest: Guest, handle: int) -> None:
    stream = _input(guest, handle)
    table_drop(guest, handle, KIND_INPUT_STREAM)
    if not any(stream is std for std in guest.state.std.values()):  # a standard stream outlives the handles to it
        stream.close()


def wasi_io_streams_output_stream_check_write(guest: Guest, comp: int, self: int, retptr: int) -> None:
    stream = _output(guest, self)
    try:
        budget = _call(stream, stream.check_write)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    store_u64(guest, comp, retptr + 8, budget)


def _write(guest: Guest, comp: int, stream: OutputStream, data: bytes, retptr: int) -> bool:
    """One write, limited by the budget (a bigger one traps); False if it wrote an error."""
    try:
        if len(data) > _call(stream, stream.check_write):
            trap(guest, "write of more than check-write allows")
        _call(stream, stream.write, data)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 4, exc)
        return False
    return True


def wasi_io_streams_output_stream_write(guest: Guest, comp: int, self: int, ptr: int, len: int, retptr: int) -> None:
    stream = _output(guest, self)
    if _write(guest, comp, stream, load_bytes(guest, comp, ptr, len), retptr):
        _ok(guest, comp, retptr)


def _write_all_and_flush(guest: Guest, comp: int, stream: OutputStream, data: bytes, retptr: int) -> None:
    """Writes it all in pieces as the budget allows, waiting when it is 0, then flushes."""
    try:
        view = memoryview(data)
        while len(view):
            budget = _call(stream, stream.check_write)
            if budget == 0:
                guest.wait()
                continue
            _call(stream, stream.write, bytes(view[:budget]))
            view = view[budget:]
        _call(stream, stream.flush)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)


def wasi_io_streams_output_stream_blocking_write_and_flush(
    guest: Guest, comp: int, self: int, ptr: int, len: int, retptr: int
) -> None:
    _write_all_and_flush(guest, comp, _output(guest, self), load_bytes(guest, comp, ptr, len), retptr)


def wasi_io_streams_output_stream_write_zeroes(guest: Guest, comp: int, self: int, len: int, retptr: int) -> None:
    stream = _output(guest, self)
    if _write(guest, comp, stream, bytes(len), retptr):
        _ok(guest, comp, retptr)


def wasi_io_streams_output_stream_blocking_write_zeroes_and_flush(
    guest: Guest, comp: int, self: int, len: int, retptr: int
) -> None:
    _write_all_and_flush(guest, comp, _output(guest, self), bytes(len), retptr)


def wasi_io_streams_output_stream_flush(guest: Guest, comp: int, self: int, retptr: int) -> None:
    stream = _output(guest, self)
    try:
        _call(stream, stream.flush)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)


def wasi_io_streams_output_stream_blocking_flush(guest: Guest, comp: int, self: int, retptr: int) -> None:
    wasi_io_streams_output_stream_flush(guest, comp, self, retptr)


def _splice(guest: Guest, comp: int, self: int, src: int, n: int, retptr: int, block: bool) -> None:
    out, source = _output(guest, self), _input(guest, src)
    try:
        while True:
            budget = _call(out, out.check_write)
            if budget == 0:
                if not block:
                    data = b""
                    break
                guest.wait()
                continue
            data = _read(guest, source, min(n, budget), block)
            break
        _call(out, out.write, data)
    except (StreamClosed, StreamFailed) as exc:
        _err(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    store_u64(guest, comp, retptr + 8, len(data))


def wasi_io_streams_output_stream_splice(guest: Guest, comp: int, self: int, src: int, len: int, retptr: int) -> None:
    _splice(guest, comp, self, src, len, retptr, False)


def wasi_io_streams_output_stream_blocking_splice(
    guest: Guest, comp: int, self: int, src: int, len: int, retptr: int
) -> None:
    _splice(guest, comp, self, src, len, retptr, True)


def wasi_io_streams_output_stream_subscribe(guest: Guest, self: int) -> int:
    return table_new(guest, KIND_POLLABLE, StreamPollable(_output(guest, self)))


def wasi_io_streams_output_stream_drop(guest: Guest, handle: int) -> None:
    stream = _output(guest, handle)
    table_drop(guest, handle, KIND_OUTPUT_STREAM)
    if not any(stream is std for std in guest.state.std.values()):
        stream.close()
