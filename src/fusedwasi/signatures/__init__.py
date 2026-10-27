"""The signatures of every WASI import function, from the official witx (0.1) and WIT (0.2), as plain data.

`load("p1")` and `load("p2")` read `wasi_p1.json` / `wasi_p2.json`, which `tools/make_signatures.py` writes. Each function has the module and the
name as a core module imports it, the flat core parameters and results (name, wasm type, role) and the first sentence of the documentation.
Next to the tables are `wasi_p1_protocols.pyi` and `wasi_p2_protocols.pyi`, one `typing.Protocol` per function: a stub generator copies the ones
it needs into the `.pyi` of a module; that `.pyi` must not import fusedwasi. This package knows nothing of any runtime that calls the imports.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from functools import cache
from pathlib import Path
from typing import Any, Literal

__all__ = ["Function", "Param", "Signatures", "get_signatures_path", "load"]

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

    @property
    def key(self) -> tuple[str, str]:
        return (self.module, self.name)


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
        )
        for f in data["functions"]
    )
    return Signatures(data["snapshot"], data["api"], data["source"], functions, {f.key: f for f in functions})
