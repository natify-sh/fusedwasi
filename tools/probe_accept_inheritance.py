#!/usr/bin/env python3
"""What does this system copy from a listening socket to the socket that `accept()` makes?

The WIT says an accepted socket inherits `keep-alive-enabled`, the keep-alive times and count, `hop-limit` and the buffer sizes of its listener. The host sets them itself after `accept`
(`docs/host_scope.md`, Known limits) because a system may not copy them. This script asks the system, with plain sockets and no fusedwasi: for each option it prints the value on the
listener (set to something that is not the default), on the accepted socket, and on a new socket (the default), and says whether the system copied it. It prints a table and exits 0;
it is a measurement and not a test: the CI runs it on each system so that the answer is in the log.

    python tools/probe_accept_inheritance.py
"""

from __future__ import annotations

import platform
import socket
import sys

# (name, level, option, the value to set on the listener)
OPTIONS: list[tuple[str, int, int | None, int]] = [
    ("SO_KEEPALIVE", socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1),
    ("IP_TTL", socket.IPPROTO_IP, socket.IP_TTL, 37),
    ("SO_RCVBUF", socket.SOL_SOCKET, socket.SO_RCVBUF, 70000),
    ("SO_SNDBUF", socket.SOL_SOCKET, socket.SO_SNDBUF, 70000),
    ("TCP_KEEPIDLE", socket.IPPROTO_TCP, getattr(socket, "TCP_KEEPIDLE", getattr(socket, "TCP_KEEPALIVE", None)), 123),
    ("TCP_KEEPINTVL", socket.IPPROTO_TCP, getattr(socket, "TCP_KEEPINTVL", None), 45),
    ("TCP_KEEPCNT", socket.IPPROTO_TCP, getattr(socket, "TCP_KEEPCNT", None), 7),
]


def read(sock: socket.socket, level: int, option: int) -> int | str:
    try:
        return int(sock.getsockopt(level, option))
    except OSError as failure:
        return f"error {failure.errno}"


def main() -> int:
    print(f"system: {platform.platform()} ({sys.platform}), python {platform.python_version()}")
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    fresh = socket.socket()
    rows: list[tuple[str, str, str, str, str]] = []
    set_ok: dict[str, bool] = {}
    for name, level, option, value in OPTIONS:
        if option is None:
            set_ok[name] = False
            continue
        try:
            listener.setsockopt(level, option, value)
            set_ok[name] = True
        except OSError:
            set_ok[name] = False
    listener.listen(1)
    client = socket.create_connection(listener.getsockname(), timeout=5)
    accepted, _ = listener.accept()
    for name, level, option, value in OPTIONS:
        if option is None:
            rows.append((name, "-", "-", "-", "the system has no such option in Python"))
            continue
        if not set_ok[name]:
            rows.append((name, "-", "-", "-", "could not be set on the listener"))
            continue
        on_listener, on_accepted, default = read(listener, level, option), read(accepted, level, option), read(fresh, level, option)
        if on_accepted == on_listener:
            verdict = "copied" if on_accepted != default else "same as the default (no information)"
        else:
            verdict = "NOT copied"
        rows.append((name, str(on_listener), str(on_accepted), str(default), verdict))
    for sock in (client, accepted, listener, fresh):
        sock.close()
    header = ("option", "listener", "accepted", "default", "verdict")
    widths = [max(len(r[i]) for r in [header, *rows]) for i in range(5)]
    for row in [header, *rows]:
        print("  ".join(cell.ljust(width) for cell, width in zip(row, widths)).rstrip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
