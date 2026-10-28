"""A short way to make a `Guest` over a new `State` in the tests of 0.2: the hooks of the guest go to the `Guest`, all the other keywords to the `State`."""

from collections.abc import Callable, Sequence
from typing import Any

from fusedwasi.p2.wasi_p2_core import Guest, State


def guest_of(
    memories: Sequence[object | Callable[[], object]] = (),
    *,
    state: State | None = None,
    realloc: Callable[[int, int, int, int, int], int] | None = None,
    wait: Callable[[], None] | None = None,
    trap: Callable[[str], None] | None = None,
    exit: Callable[[int], None] | None = None,
    clock: Callable[[int], int] | None = None,
    resolution_ns: Sequence[int] | None = None,
    sleep: Callable[[int], None] | None = None,
    random_bytes: Callable[[int], bytes] | None = None,
    insecure_random_bytes: Callable[[int], bytes] | None = None,
    args: Sequence[str] = (),
    env: Sequence[tuple[str, str]] = (),
    initial_cwd: str | None = None,
    stdin: object = None,
    stdout: object = None,
    stderr: object = None,
    terminals: int = 0,
    preopens: Sequence[tuple[str, str] | tuple[str, str, bool]] = (),
    network: bool = False,
    socket_addr_check: Callable[[str, int, int], bool] | None = None,
    allow_ip_name_lookup: bool | None = None,
    http: bool = False,
    http_check: Callable[[str, str], bool] | None = None,
    http_send: Callable[[Any], Any] | None = None,
) -> Guest:
    """`Guest(State(**world), memories, **caller)`, or over `state` when it is given (then no keyword of the world is allowed)."""
    caller: dict[str, Any] = {k: v for k, v in (("realloc", realloc), ("trap", trap), ("exit", exit)) if v is not None}
    if wait is not None:
        caller["wait"] = wait
    world: dict[str, Any] = {
        k: v
        for k, v in (
            ("clock", clock),
            ("resolution_ns", resolution_ns),
            ("sleep", sleep),
            ("random_bytes", random_bytes),
            ("insecure_random_bytes", insecure_random_bytes),
            ("initial_cwd", initial_cwd),
            ("stdin", stdin),
            ("stdout", stdout),
            ("stderr", stderr),
            ("socket_addr_check", socket_addr_check),
            ("allow_ip_name_lookup", allow_ip_name_lookup),
            ("http_check", http_check),
            ("http_send", http_send),
        )
        if v is not None
    }
    world.update(args=args, env=env, terminals=terminals, preopens=preopens, network=network, http=http)
    if state is not None:
        return Guest(state, memories, **caller)
    return Guest(State(**world), memories, **caller)
