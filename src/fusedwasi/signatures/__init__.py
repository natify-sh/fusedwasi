"""The signatures of every WASI import function, from the official witx (0.1) and WIT (0.2), as plain data.

`load("p1")` and `load("p2")` read `wasi_p1.json` / `wasi_p2.json`, which `tools/make_signatures.py` writes. Each function has the module and the
name as a core module imports it, the flat core parameters and results (name, wasm type, role) and the first sentence of the documentation.
Next to the tables are `wasi_p1_protocols.pyi` and `wasi_p2_protocols.pyi`, one `typing.Protocol` per function: a stub generator copies the ones
it needs into the `.pyi` of a module; that `.pyi` must not import fusedwasi. This package knows nothing of any runtime that calls the imports.
"""

from __future__ import annotations

import importlib
import json
import re
from collections.abc import Callable
from dataclasses import dataclass, field
from functools import cache
from pathlib import Path
from typing import Any, Literal

__all__ = ["Function", "Param", "Signatures", "find_import", "get_signatures_path", "load"]

Snapshot = Literal["p1", "p2"]


def get_signatures_path() -> str:
    """The directory with `wasi_p1.json`, `wasi_p2.json` and the `*_protocols.pyi` files."""
    return str(Path(__file__).resolve().parent)


@dataclass(frozen=True)
class Param:
    """One flat core parameter or result. `role` is one of pointer, length, handle, value, tag, retptr, errno."""

    name: str
    type: str  # i32, i64, f32 or f64
    role: str
    source_type: str


@dataclass(frozen=True)
class Function:
    """One import of a core module. For p1 `module` is `wasi_snapshot_preview1`; for p2 it is e.g. `wasi:io/streams@0.2.12`.

    `name` is the name of the import (p2: `[method]input-stream.read`), `official_name` the name in the spec (p2: `read`). The fields after
    `results` are filled for p2 only.
    """

    module: str
    name: str
    official_name: str
    protocol: str
    doc: str
    params: tuple[Param, ...]
    results: tuple[Param, ...]
    package: str | None = None
    interface: str | None = None
    kind: str | None = None  # func, method, static, constructor or resource-drop
    resource: str | None = None
    stability: str = "stable"
    feature: str | None = None
    # where fusedwasi has the function (None for the few that it leaves out on purpose, the unstable items of 0.2)
    c_name: str | None = (
        None  # the C symbol: fusedwasi_p1_<name>, fusedwasi_wasi_<package>_<interface>[_<resource>]_<function>
    )
    python_module: str | None = None  # the module of the twin: fusedwasi.p1.wasi_p1, fusedwasi.p2.wasi_p2_<package>
    python_name: str | None = None  # the function in it
    takes_comp: bool | None = (
        None  # 0.2: the function takes the number of the memory (the component) after the guest, before the parameters of `params`; always False for 0.1
    )

    @property
    def key(self) -> tuple[str, str]:
        return (self.module, self.name)

    def python_function(self) -> Callable[..., Any]:
        """The function of the Python twin (it imports `python_module`). Its first argument is the guest; with `takes_comp`, the second is the number of the memory.
        Raises `LookupError` for a function that fusedwasi leaves out (`c_name` is None)."""
        if self.python_module is None or self.python_name is None:
            raise LookupError(f"fusedwasi has no function for {self.module} {self.name}")
        function: Callable[..., Any] = getattr(importlib.import_module(self.python_module), self.python_name)
        return function


@dataclass(frozen=True)
class Signatures:
    snapshot: str
    api: str
    source: dict[str, Any]
    functions: tuple[Function, ...]
    _index: dict[tuple[str, str], Function] = field(
        default_factory=dict[tuple[str, str], Function], repr=False, compare=False
    )

    def get(self, module: str, name: str) -> Function | None:
        """The function a core module imports as `(module, name)`, or None."""
        return self._index.get((module, name))

    def __len__(self) -> int:
        return len(self.functions)


def _params(items: list[dict[str, str]]) -> tuple[Param, ...]:
    return tuple(Param(i["name"], i["type"], i["role"], i["source_type"]) for i in items)


@cache
def load(snapshot: Snapshot) -> Signatures:
    """The table of `"p1"` (`wasi_snapshot_preview1`) or `"p2"` (`wasi:*@0.2.x`)."""
    if snapshot not in ("p1", "p2"):
        raise ValueError(f"snapshot must be 'p1' or 'p2', not {snapshot!r}")
    data: dict[str, Any] = json.loads(
        (Path(get_signatures_path()) / f"wasi_{snapshot}.json").read_text(encoding="utf-8")
    )
    functions = tuple(
        Function(
            module=f["module"],
            name=f["name"],
            official_name=f["official_name"],
            protocol=f["protocol"],
            doc=f["doc"],
            params=_params(f["params"]),
            results=_params(f["results"]),
            package=f.get("package"),
            interface=f.get("interface"),
            kind=f.get("kind"),
            resource=f.get("resource"),
            stability=f.get("stability", "stable"),
            feature=f.get("feature"),
            c_name=f.get("c_name"),
            python_module=f.get("python_module"),
            python_name=f.get("python_name"),
            takes_comp=f.get("takes_comp"),
        )
        for f in data["functions"]
    )
    return Signatures(data["snapshot"], data["api"], data["source"], functions, {f.key: f for f in functions})


P1_MODULE = "wasi_snapshot_preview1"
_P2_IMPORT = re.compile(r"^(?P<interface>wasi:[a-z0-9-]+/[a-z0-9-]+)@(?P<major>\d+)\.(?P<minor>\d+)\.\d+$")


def find_import(module: str, name: str) -> Function | None:
    """The function for an import of a core module, as the module names it: `("wasi_snapshot_preview1", "fd_write")` or `("wasi:io/streams@0.2.6", "[method]output-stream.write")`.

    A package of WASI 0.2 is matched whatever its patch version is: `@0.2.N` finds the function of the table (which is of one version, `load("p2").api`) for any N, because
    the versions of one major.minor are compatible; `@0.3.x` or `@1.x.y` find nothing. A name that is not in the table, a module that is neither of 0.1 nor of 0.2, and
    `wasi_unstable` (the first snapshot, whose functions are `fusedwasi_p1_unstable_*` where they differ) give None.
    """
    if module == P1_MODULE:
        return load("p1").get(module, name)
    m = _P2_IMPORT.match(module)
    if m is None:
        return None
    table = load("p2")
    version = table.api.split("@", 1)[1]
    if (m["major"], m["minor"]) != tuple(version.split(".")[:2]):
        return None
    return table.get(f"{m['interface']}@{version}", name)
