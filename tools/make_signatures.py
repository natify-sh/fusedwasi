#!/usr/bin/env python3
"""Writes the table of the signatures of every WASI import function, from the official sources kept in tools/spec_sources/.

Reads (as text only; nothing from the sources is executed):
  - WASI 0.1: the witx of `wasi_snapshot_preview1`;
  - WASI 0.2: the WIT of the seven packages (cli, clocks, filesystem, http, io, random, sockets).

Writes into src/fusedwasi/signatures/:
  - wasi_p1.json, wasi_p2.json: for each function the module and the name as a core module imports it, the official name, the flat core
    parameters and results (Canonical ABI for 0.2, the witx ABI for 0.1), the first sentence of the documentation, and the exact source
    (tag, commit, sha256 of every file);
  - the same files also say, for each function, where fusedwasi has it: the C symbol (`c_name`), the Python module and name (`python_module`, `python_name`) and whether a
    function of 0.2 takes the number of the memory after the guest (`takes_comp`). They are read from the headers and the Python files of this repository as text and checked
    against each other (a function that only one twin has stops the generator), so `tests/test_signatures.py` fails when a header and the table drift apart;
  - wasi_p1_protocols.pyi, wasi_p2_protocols.pyi: one typing.Protocol per function, positional-only so the names document and do not constrain.

Standard library only. `python tools/make_signatures.py` writes; `--check` only compares with the files on disk and exits 1 on a difference.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import keyword
import re
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "tools" / "spec_sources"
OUT = ROOT / "src" / "fusedwasi" / "signatures"
SCHEMA = 1
MAX_FLAT_PARAMS = 16
MAX_FLAT_RESULTS = 1
GENERATOR = "tools/make_signatures.py"


# ---- helpers ----------------------------------------------------------------------------------------------------------------


def first_sentence(lines: list[str]) -> str:
    """The first sentence of the first paragraph of a documentation comment, on one line."""
    para: list[str] = []
    for line in lines:
        if not line.strip():
            if para:
                break
            continue
        para.append(line.strip())
    text = " ".join(para)
    for m in re.finditer(r"[.!?](?=\s|$)", text):
        head = text[: m.end()].lower()
        if head.endswith(("e.g.", "i.e.", "etc.", "vs.", "cf.")):
            continue
        return text[: m.end()]
    return text


def camel(text: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in re.split(r"[^A-Za-z0-9]+", text) if p)


def py_ident(name: str) -> str:
    """A valid Python parameter name for a name of the spec: hyphens become underscores, keywords and `self` get a trailing underscore."""
    name = name.replace("-", "_")
    if keyword.iskeyword(name) or name == "self":
        name += "_"
    return name


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_text(path: Path) -> str:
    return path.read_bytes().decode("utf-8")


@dataclass
class Param:
    name: str
    type: str  # i32 i64 f32 f64
    role: str  # pointer length handle value tag retptr errno
    source_type: str

    def as_dict(self) -> dict[str, str]:
        return {"name": self.name, "type": self.type, "role": self.role, "source_type": self.source_type}


@dataclass
class Func:
    module: str
    name: str
    official_name: str
    doc: str
    params: list[Param]
    results: list[Param]
    protocol: str = ""
    extra: dict[str, Any] = field(default_factory=lambda: {})

    def as_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "module": self.module,
            "name": self.name,
            "official_name": self.official_name,
            "protocol": self.protocol,
            "doc": self.doc,
        }
        d.update(self.extra)
        d["params"] = [p.as_dict() for p in self.params]
        d["results"] = [p.as_dict() for p in self.results]
        return d


# ---- WASI 0.1: witx ---------------------------------------------------------------------------------------------------------


class Node(list):  # type: ignore[type-arg]
    """A parenthesised list of a witx file; `doc` is the `;;;` comment right before it."""

    doc: list[str]

    def __init__(self) -> None:
        super().__init__()
        self.doc = []


WITX_TOKEN = re.compile(r';;;[^\n]*|;;[^\n]*|\(|\)|"[^"]*"|[^\s()";]+')


def witx_parse(text: str) -> list[Any]:
    tokens = WITX_TOKEN.findall(text)
    pos = 0

    def items(close: bool) -> Node:
        nonlocal pos
        out = Node()
        pending: list[str] = []
        while pos < len(tokens):
            tok = tokens[pos]
            pos += 1
            if tok.startswith(";;;"):
                pending.append(tok[3:][1:] if tok[3:4] == " " else tok[3:])
            elif tok.startswith(";;"):
                continue
            elif tok == "(":
                node = items(True)
                node.doc = pending
                pending = []
                out.append(node)
            elif tok == ")":
                if not close:
                    raise ValueError("unbalanced )")
                return out
            else:
                pending = []
                out.append(tok)
        if close:
            raise ValueError("missing )")
        return out

    return list(items(False))


def witx_render(node: Any) -> str:
    if isinstance(node, str):
        return node.lstrip("$")
    return "(" + " ".join(witx_render(x) for x in node) + ")"


WITX_INT = {"u8": "i32", "u16": "i32", "u32": "i32", "s8": "i32", "s16": "i32", "s32": "i32", "u64": "i64", "s64": "i64", "f32": "f32", "f64": "f64"}


class Witx:
    def __init__(self, typenames: list[Any]) -> None:
        self.types: dict[str, Any] = {}
        for node in typenames:
            if isinstance(node, list) and node and node[0] == "typename":
                self.types[node[1]] = node[2]

    def flat(self, name: str, ty: Any) -> list[Param]:
        """The core parameters that one parameter of a witx function becomes."""
        src = witx_render(ty)
        seen = ty
        while isinstance(seen, str) and seen.startswith("$"):
            seen = self.types[seen]
        if isinstance(seen, str):
            if seen == "string":
                return [Param(name, "i32", "pointer", src), Param(name + "_len", "i32", "length", src)]
            if seen in WITX_INT:
                role = "length" if isinstance(ty, str) and ty == "$size" else "value"
                return [Param(name, WITX_INT[seen], role, src)]
            raise ValueError(f"witx type {seen!r} of {name}")
        head = seen[0]
        if head == "handle":
            return [Param(name, "i32", "handle", src)]
        if head == "list":
            return [Param(name, "i32", "pointer", src), Param(name + "_len", "i32", "length", src)]
        if head == "@witx" and seen[1] in ("pointer", "const_pointer"):
            return [Param(name, "i32", "pointer", src)]
        if head == "@witx" and seen[1] == "usize":
            return [Param(name, "i32", "length", src)]
        if head in ("enum", "flags"):
            rep = next((x for x in seen if isinstance(x, list) and x and x[0] == "@witx" and x[1] in ("tag", "repr")), None)
            if rep is None:
                raise ValueError(f"{head} of {name} has no repr")
            return [Param(name, WITX_INT[rep[2]], "value", src)]
        raise ValueError(f"witx type {src} of {name} is not passed by value")


def build_p1(src: dict[str, Any]) -> tuple[list[Func], list[dict[str, str]]]:
    d = SOURCES / src["dir"]
    files = [d / n for n in src["files"]]
    witx = Witx(witx_parse(read_text(d / "typenames.witx")))
    top = witx_parse(read_text(d / "wasi_snapshot_preview1.witx"))
    modules = [n for n in top if isinstance(n, list) and n and n[0] == "module"]
    if len(modules) != 1:
        raise ValueError("expected one module")
    mod = modules[0]
    module = mod[1].lstrip("$")
    if module != src["snapshot"]:
        raise ValueError(f"module {module}")
    funcs: list[Func] = []
    for node in mod:
        if not (isinstance(node, list) and node and node[0] == "@interface" and node[1] == "func"):
            continue
        name = next(x[1].strip('"') for x in node if isinstance(x, list) and x and x[0] == "export")
        params: list[Param] = []
        results: list[Param] = []
        for x in node:
            if not (isinstance(x, list) and x):
                continue
            if x[0] == "param":
                params += witx.flat(x[1].lstrip("$"), x[2])
            elif x[0] == "result":
                # (result $error (expected OK? (error $errno))): the errno is the core result, each OK value is returned through a pointer
                exp = x[2]
                if not (isinstance(exp, list) and exp[0] == "expected"):
                    raise ValueError(f"{name}: result {witx_render(x)}")
                oks = exp[1:-1]
                values: list[Any] = []
                for ok in oks:
                    values += list(ok[1:]) if isinstance(ok, list) and ok[0] == "tuple" else [ok]
                for i, v in enumerate(values):
                    params.append(Param("retptr" if len(values) == 1 else f"retptr{i}", "i32", "retptr", witx_render(v)))
                results.append(Param("errno", "i32", "errno", witx_render(exp[-1][1])))
        funcs.append(
            Func(module, name, name, first_sentence(node.doc), params, results, camel(name))
        )
    records = [{"path": f"{src['dir']}/{p.name}", "sha256": sha256_of(p)} for p in sorted(files)]
    return funcs, records


# ---- WASI 0.2: WIT ----------------------------------------------------------------------------------------------------------

WIT_TOKEN = re.compile(
    r"""
      (?P<ws>\s+)
    | (?P<doc>///[^\n]*)
    | (?P<lc>//[^\n]*)
    | (?P<bc>/\*)
    | (?P<use>(?<![\w%-])use(?![\w-])[^;]*;)
    | (?P<arrow>->)
    | (?P<id>%?[A-Za-z][A-Za-z0-9-]*)
    | (?P<num>[0-9][\w.]*)
    | (?P<sym>[=,:;.(){}<>*@/_])
    """,
    re.X,
)

WIT_PRIM = {
    "bool": "i32",
    "u8": "i32",
    "s8": "i32",
    "u16": "i32",
    "s16": "i32",
    "u32": "i32",
    "s32": "i32",
    "char": "i32",
    "u64": "i64",
    "s64": "i64",
    "f32": "f32",
    "f64": "f64",
}


def wit_tokens(text: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    pos = 0
    while pos < len(text):
        m = WIT_TOKEN.match(text, pos)
        if m is None:
            raise ValueError(f"WIT: cannot read {text[pos : pos + 30]!r}")
        kind = m.lastgroup or ""
        pos = m.end()
        if kind in ("ws", "lc"):
            continue
        if kind == "bc":  # block comments nest
            depth = 1
            while depth:
                a, b = text.find("/*", pos), text.find("*/", pos)
                if b < 0:
                    raise ValueError("WIT: unterminated comment")
                if 0 <= a < b:
                    depth += 1
                    pos = a + 2
                else:
                    depth -= 1
                    pos = b + 2
            continue
        out.append((kind, m.group()))
    return out


Type = tuple[Any, ...]  # ("prim", n) ("named", n) ("list", T) ("option", T) ("result", T|None, T|None) ("tuple", [T]) ("own", n) ("borrow", n) ("string",)


@dataclass
class TypeDef:
    kind: str  # alias record variant enum flags resource
    data: Any = None


@dataclass
class WFunc:
    kind: str  # func method static constructor
    name: str
    params: list[tuple[str, Type]]
    result: Type | None
    doc: list[str]
    unstable: str | None


@dataclass
class Iface:
    pkg: str
    name: str
    version: str
    unstable: str | None = None
    uses: dict[str, tuple[tuple[str, str], str]] = field(default_factory=lambda: {})
    types: dict[str, TypeDef] = field(default_factory=lambda: {})
    items: list[tuple[str, str | WFunc, str | None]] = field(default_factory=lambda: [])  # ("func", f, None) | ("resource", name, unstable)
    resource_unstable: dict[str, str | None] = field(default_factory=lambda: {})
    resource_funcs: dict[str, list[WFunc]] = field(default_factory=lambda: {})

    @property
    def module(self) -> str:
        return f"{self.pkg}/{self.name}@{self.version}"


class WitParser:
    def __init__(self, tokens: list[tuple[str, str]], ifaces: dict[tuple[str, str], Iface], pkg: str, version: str) -> None:
        self.t = tokens
        self.i = 0
        self.ifaces = ifaces
        self.pkg = pkg
        self.version = version

    # -- token access
    def peek(self) -> tuple[str, str]:
        while self.t[self.i][0] == "doc":
            self.i += 1
        return self.t[self.i]

    def next(self) -> tuple[str, str]:
        tok = self.peek()
        self.i += 1
        return tok

    def expect(self, text: str) -> None:
        tok = self.next()
        if tok[1] != text:
            raise ValueError(f"WIT: expected {text!r}, got {tok[1]!r} (token {self.i})")

    def accept(self, text: str) -> bool:
        if self.peek()[1] == text:
            self.i += 1
            return True
        return False

    def ident(self) -> str:
        kind, text = self.next()
        if kind != "id":
            raise ValueError(f"WIT: expected a name, got {text!r}")
        return text.lstrip("%")

    def prefix(self) -> tuple[list[str], str | None]:
        """Doc comments and annotations before an item: (doc lines, feature of @unstable or None)."""
        doc: list[str] = []
        unstable: str | None = None
        while True:
            kind, text = self.t[self.i]
            if kind == "doc":
                doc.append(text[3:][1:] if text[3:4] == " " else text[3:])
                self.i += 1
            elif text == "@":
                self.i += 1
                name = self.ident()
                body: list[str] = []
                if self.peek()[1] == "(":
                    depth = 0
                    while True:
                        tok = self.next()[1]
                        depth += tok == "("
                        depth -= tok == ")"
                        body.append(tok)
                        if depth == 0:
                            break
                if name == "unstable":
                    joined = "".join(body)
                    m = re.search(r"feature=([\w-]+)", joined)
                    unstable = m.group(1) if m else ""
            else:
                return doc, unstable

    # -- types
    def parse_type(self) -> Type:
        name = self.ident()
        if name in WIT_PRIM:
            return ("prim", name)
        if name == "string":
            return ("string",)
        if name in ("list", "option", "tuple", "own", "borrow", "result"):
            if name == "result" and self.peek()[1] != "<":
                return ("result", None, None)
            self.expect("<")
            args: list[Type | None] = []
            while True:
                if self.peek()[1] == "_":
                    self.next()
                    args.append(None)
                else:
                    args.append(self.parse_type() if name not in ("own", "borrow") else ("named", self.ident()))
                if not self.accept(","):
                    break
            self.expect(">")
            if name == "tuple":
                return ("tuple", [a for a in args if a is not None])
            if name == "result":
                return ("result", args[0], args[1] if len(args) > 1 else None)
            if len(args) != 1 or args[0] is None:
                raise ValueError(f"WIT: {name}<...>")
            return (name, args[0])
        if name in ("future", "stream", "error-context"):
            raise ValueError(f"WIT: {name} is not WASI 0.2")
        return ("named", name)

    def params(self) -> list[tuple[str, Type]]:
        self.expect("(")
        out: list[tuple[str, Type]] = []
        while not self.accept(")"):
            n = self.ident()
            self.expect(":")
            out.append((n, self.parse_type()))
            self.accept(",")
        return out

    def result(self) -> Type | None:
        if self.peek()[0] == "arrow":
            self.next()
            if self.peek()[1] == "(":
                raise ValueError("WIT: named results are not used by WASI 0.2")
            return self.parse_type()
        return None

    # -- file
    def parse(self) -> None:
        """One file of a package: an optional `package` line (only one file of the directory has it), then interfaces and worlds."""
        if self.peek()[1] == "package":
            self.next()
            parts: list[str] = []
            while self.peek()[1] != ";":
                parts.append(self.next()[1])
            self.next()
            pkg, _, ver = "".join(parts).partition("@")
            if pkg != self.pkg or ver != self.version:
                raise ValueError(f"WIT: package {pkg}@{ver}, expected {self.pkg}@{self.version}")
        while self.i < len(self.t):
            doc, unstable = self.prefix()
            if self.i >= len(self.t):
                break
            kind, text = self.next()
            if text == "world":
                self.ident()
                self.expect("{")
                depth = 1
                while depth:
                    tok = self.next()[1]
                    depth += tok == "{"
                    depth -= tok == "}"
            elif text == "interface":
                self.interface(unstable)
            else:
                raise ValueError(f"WIT: unexpected {text!r} at the top level")

    def interface(self, unstable: str | None) -> None:
        name = self.ident()
        iface = Iface(self.pkg, name, self.version, unstable)
        if (self.pkg, name) in self.ifaces:
            raise ValueError(f"WIT: interface {self.pkg}/{name} twice")
        self.ifaces[(self.pkg, name)] = iface
        self.expect("{")
        while True:
            doc, unst = self.prefix()
            kind, text = self.next()
            if text == "}":
                return
            if kind == "use":
                self.use(iface, text)
            elif text == "type":
                n = self.ident()
                self.expect("=")
                iface.types[n] = TypeDef("alias", self.parse_type())
                self.expect(";")
            elif text == "record":
                n = self.ident()
                self.expect("{")
                fields: list[tuple[str, Type]] = []
                while not self.accept("}"):
                    self.prefix()
                    fn = self.ident()
                    self.expect(":")
                    fields.append((fn, self.parse_type()))
                    self.accept(",")
                iface.types[n] = TypeDef("record", fields)
            elif text == "variant":
                n = self.ident()
                self.expect("{")
                cases: list[tuple[str, Type | None]] = []
                while not self.accept("}"):
                    self.prefix()
                    cn = self.ident()
                    payload = None
                    if self.accept("("):
                        payload = self.parse_type()
                        self.expect(")")
                    cases.append((cn, payload))
                    self.accept(",")
                iface.types[n] = TypeDef("variant", cases)
            elif text in ("enum", "flags"):
                n = self.ident()
                self.expect("{")
                labels: list[str] = []
                while not self.accept("}"):
                    self.prefix()
                    labels.append(self.ident())
                    self.accept(",")
                iface.types[n] = TypeDef(text, labels)
            elif text == "resource":
                n = self.ident()
                iface.types[n] = TypeDef("resource")
                iface.resource_unstable[n] = unst
                iface.resource_funcs[n] = []
                iface.items.append(("resource", n, unst))
                if self.accept(";"):
                    continue
                self.expect("{")
                while True:
                    mdoc, munst = self.prefix()
                    mk, mt = self.next()
                    if mt == "}":
                        break
                    if mt == "constructor":
                        f = WFunc("constructor", "constructor", self.params(), self.result() or ("own", ("named", n)), mdoc, munst)
                    else:
                        mname = mt.lstrip("%")
                        self.expect(":")
                        is_static = self.accept("static")
                        self.expect("func")
                        ps = self.params()
                        f = WFunc("static" if is_static else "method", mname, ps, self.result(), mdoc, munst)
                        if not is_static:
                            f.params.insert(0, ("self", ("borrow", ("named", n))))
                    self.expect(";")
                    iface.resource_funcs[n].append(f)
            else:  # name: func(...)
                fname = text.lstrip("%")
                self.expect(":")
                self.expect("func")
                f = WFunc("func", fname, self.params(), self.result(), doc, unst)
                self.expect(";")
                iface.items.append(("func", f, None))

    def use(self, iface: Iface, text: str) -> None:
        m = re.fullmatch(r"use\s+(.+?)\.\{([^}]*)\}\s*;", text, re.S)
        if m is None:
            raise ValueError(f"WIT: cannot read {text!r}")
        target, names = m.group(1).strip(), m.group(2)
        if ":" in target:
            pkgname, _, rest = target.partition("/")
            key = (pkgname, rest.partition("@")[0])
        else:
            key = (iface.pkg, target)
        for part in names.split(","):
            part = part.strip()
            if not part:
                continue
            orig, _, alias = part.partition(" as ")
            iface.uses[(alias or orig).strip().lstrip("%")] = (key, orig.strip().lstrip("%"))


def wit_render(ty: Type | None) -> str:
    if ty is None:
        return "_"
    k = ty[0]
    if k in ("prim", "named"):
        return ty[1]
    if k == "string":
        return "string"
    if k in ("list", "option", "own", "borrow"):
        return f"{k}<{wit_render(ty[1] if k not in ('own', 'borrow') else ty[1])}>" if k in ("list", "option") else f"{k}<{ty[1][1]}>"
    if k == "tuple":
        return "tuple<" + ", ".join(wit_render(x) for x in ty[1]) + ">"
    if k == "result":
        if ty[1] is None and ty[2] is None:
            return "result"
        if ty[2] is None:
            return f"result<{wit_render(ty[1])}>"
        return f"result<{wit_render(ty[1])}, {wit_render(ty[2])}>"
    raise ValueError(ty)


class Canon:
    """The flattening of the Canonical ABI over the types of the parsed interfaces."""

    def __init__(self, ifaces: dict[tuple[str, str], Iface]) -> None:
        self.ifaces = ifaces

    def resolve(self, iface: Iface, name: str) -> tuple[TypeDef, Iface]:
        if name in iface.types:
            return iface.types[name], iface
        if name in iface.uses:
            key, orig = iface.uses[name]
            return self.resolve(self.ifaces[key], orig)
        raise KeyError(f"{iface.module}: type {name}")

    def flat(self, ty: Type | None, iface: Iface, path: list[str] | None = None) -> list[tuple[list[str], str, str]]:
        """Slots (suffix path, wasm type, role) of a value of type `ty`."""
        path = path or []
        if ty is None:
            return []
        k = ty[0]
        if k == "prim":
            return [(path, WIT_PRIM[ty[1]], "value")]
        if k == "string" or k == "list":
            return [(path, "i32", "pointer"), (path + ["len"], "i32", "length")]
        if k in ("own", "borrow"):
            return [(path, "i32", "handle")]
        if k == "tuple":
            out: list[tuple[list[str], str, str]] = []
            for i, t in enumerate(ty[1]):
                out += self.flat(t, iface, path + [str(i)])
            return out
        if k == "named":
            td, owner = self.resolve(iface, ty[1])
            if td.kind == "alias":
                return self.flat(td.data, owner, path)
            if td.kind == "resource":
                return [(path, "i32", "handle")]
            if td.kind == "record":
                out = []
                for fname, ft in td.data:
                    out += self.flat(ft, owner, path + [fname])
                return out
            if td.kind == "enum":
                return [(path, "i32", "value")]
            if td.kind == "flags":
                n = (len(td.data) + 31) // 32
                return [(path + ([str(i)] if n > 1 else []), "i32", "value") for i in range(n)]
            if td.kind == "variant":
                return self.joined(path, [c[1] for c in td.data], owner)
        if k == "option":
            return self.joined(path, [None, ty[1]], iface)
        if k == "result":
            return self.joined(path, [ty[1], ty[2]], iface)
        raise ValueError(f"cannot flatten {ty}")

    def joined(self, path: list[str], cases: list[Type | None], iface: Iface) -> list[tuple[list[str], str, str]]:
        slots: list[tuple[str, set[str]]] = []
        for c in cases:
            for i, (_, w, r) in enumerate(self.flat(c, iface)):
                if i >= len(slots):
                    slots.append((w, {r}))
                else:
                    old, roles = slots[i]
                    roles.add(r)
                    slots[i] = (old if old == w else "i32" if {old, w} == {"i32", "f32"} else "i64", roles)
        out = [(path + ["tag"], "i32", "tag")]
        for i, (w, roles) in enumerate(slots):
            out.append((path + [f"val{i}"], w, next(iter(roles)) if len(roles) == 1 else "value"))
        return out


def slot_name(base: str, suffix: list[str]) -> str:
    return "_".join([base, *suffix]).replace("-", "_")


def build_p2(src: dict[str, Any]) -> tuple[list[Func], list[dict[str, str]]]:
    version = src["version"]
    d = SOURCES / src["dir"]
    ifaces: dict[tuple[str, str], Iface] = {}
    files: list[Path] = []
    for pkg in src["packages"]:
        paths = sorted((d / pkg).glob("*.wit"))
        files += paths
        texts = [read_text(p) for p in paths]
        names = {m.group(1) for t in texts for m in re.finditer(r"^package\s+([\w:-]+)@", t, re.M)}
        if len(names) != 1:
            raise ValueError(f"WIT: the package of {pkg} is not named once: {names}")
        for text in texts:
            WitParser(wit_tokens(text), ifaces, next(iter(names)), version).parse()
    canon = Canon(ifaces)
    export_only = set(src["export_only"])
    funcs: list[Func] = []
    seen_protocols: dict[str, str] = {}

    def add(iface: Iface, kind: str, resource: str | None, wf: WFunc | None, unstable: str | None) -> None:
        if resource is None:
            assert wf is not None
            name = wf.name
        elif wf is None:
            name = f"[resource-drop]{resource}"
        elif wf.kind == "constructor":
            name = f"[constructor]{resource}"
        else:
            name = f"[{wf.kind}]{resource}.{wf.name}"
        official = "resource-drop" if wf is None else wf.name
        params: list[Param] = []
        results: list[Param] = []
        result_text = ""
        if wf is None:
            params = [Param("self", "i32", "handle", f"own<{resource}>")]
        else:
            for pname, pty in wf.params:
                text = wit_render(pty)
                for suffix, w, role in canon.flat(pty, iface):
                    params.append(Param(slot_name(pname, suffix), w, role, text))
            if len(params) > MAX_FLAT_PARAMS:
                params = [Param("args", "i32", "pointer", "params in memory")]
            if wf.result is not None:
                result_text = wit_render(wf.result)
                flat = canon.flat(wf.result, iface)
                if len(flat) > MAX_FLAT_RESULTS:
                    params.append(Param("retptr", "i32", "retptr", result_text))
                else:
                    results = [Param("result", w, role, result_text) for _, w, role in flat]
        names = [p.name for p in params]
        if len(set(names)) != len(names):
            raise ValueError(f"{iface.module} {name}: duplicate parameter names {names}")
        pkgshort = iface.pkg.split(":")[1]
        proto = camel(f"{pkgshort}-{iface.name}-{resource or ''}-{official}")
        full = f"{iface.module} {name}"
        if proto in seen_protocols:
            raise ValueError(f"protocol name {proto} for {full} and {seen_protocols[proto]}")
        seen_protocols[proto] = full
        extra: dict[str, Any] = {
            "package": iface.pkg,
            "interface": iface.name,
            "kind": kind,
            "resource": resource,
            "stability": "unstable" if unstable is not None else "stable",
            "feature": unstable or None,
        }
        doc = first_sentence(wf.doc) if wf is not None else ""
        funcs.append(Func(iface.module, name, official, doc, params, results, proto, extra))

    for (pkg, iname), iface in ifaces.items():
        if f"{pkg}/{iname}" in export_only:
            continue
        for kind, item, unst in iface.items:
            if kind == "func":
                assert isinstance(item, WFunc)
                add(iface, "func", None, item, item.unstable or iface.unstable)
            else:
                assert isinstance(item, str)
                res_unstable = unst or iface.unstable
                for wf in iface.resource_funcs[item]:
                    add(iface, wf.kind, item, wf, wf.unstable or res_unstable)
                add(iface, "resource-drop", item, None, res_unstable)
    records = [{"path": f"{src['dir']}/{p.parent.name}/{p.name}", "sha256": sha256_of(p)} for p in files]
    return funcs, records


# ---- where fusedwasi has each function ---------------------------------------------------------------------------------------

INCLUDE = ROOT / "src" / "fusedwasi" / "include" / "fusedwasi"
PYTHON = ROOT / "src" / "fusedwasi"
P2_PACKAGES = ("cli", "clocks", "filesystem", "http", "io", "random", "sockets")


def c_prototypes(key: str) -> dict[str, int]:
    """The C functions of the headers by name, with the number of parameters after the guest (and, for 0.1, before `trapped`)."""
    found: dict[str, int] = {}
    if key == "p1":
        text = read_text(INCLUDE / "p1" / "wasi_p1_core.h")
        for m in re.finditer(r"static\s+inline\s+(?:uint32_t|void)\s+(fusedwasi_p1_\w+)\(\s*void\s*\*vg\s*,([^)]*?)int\s*\*trapped\s*\)", text, re.DOTALL):
            found[m.group(1)] = len([x for x in m.group(2).split(",") if x.strip()])
    else:
        for pkg in P2_PACKAGES:
            text = read_text(INCLUDE / "p2" / f"wasi_p2_{pkg}.h")
            for m in re.finditer(r"FUSEDWASI_P2_FN\s+\w+\s+(fusedwasi_wasi_\w+)\(([^)]*)\)", text):
                found[m.group(1)] = len([x for x in m.group(2).split(",") if x.strip()]) - 1
    return found


def python_functions(path: Path) -> dict[str, int]:
    """The functions at the top of a Python file by name, with the number of parameters after the guest."""
    tree = ast.parse(read_text(path))
    return {n.name: len(n.args.posonlyargs) + len(n.args.args) - 1 for n in tree.body if isinstance(n, ast.FunctionDef)}


def p2_function_name(f: Func) -> str:
    """wasi_<package>_<interface>[_<resource>]_<function>: the name of a function of 0.2 in both twins (C adds the prefix `fusedwasi_`)."""
    e = f.extra
    last = {"constructor": "new", "resource-drop": "drop"}.get(e["kind"], f.official_name)
    parts = ["wasi", e["package"].split(":")[1], e["interface"], e["resource"] or "", last]
    return "_".join(p.replace("-", "_") for p in parts if p)


def attach_implementations(funcs: list[Func], key: str) -> None:
    c = c_prototypes(key)
    py: dict[str, tuple[str, dict[str, int]]] = {}
    if key == "p1":
        py["fusedwasi.p1.wasi_p1"] = ("fusedwasi.p1.wasi_p1", python_functions(PYTHON / "p1" / "wasi_p1.py"))
    else:
        for pkg in P2_PACKAGES:
            py[pkg] = (f"fusedwasi.p2.wasi_p2_{pkg}", python_functions(PYTHON / "p2" / f"wasi_p2_{pkg}.py"))
    for f in funcs:
        if key == "p1":
            name, c_name, module_key = f.name, f"fusedwasi_p1_{f.name}", "fusedwasi.p1.wasi_p1"
        else:
            name = p2_function_name(f)
            c_name, module_key = "fusedwasi_" + name, f.extra["package"].split(":")[1]
        module, functions = py[module_key]
        in_c, in_py = c_name in c, name in functions
        if in_c != in_py:
            raise ValueError(f"{f.module} {f.name}: {c_name} is {'in' if in_c else 'not in'} the headers and {name} is {'in' if in_py else 'not in'} {module}")
        if not in_c:  # left out on purpose by both twins (the unstable items)
            f.extra.update(c_name=None, python_module=None, python_name=None, takes_comp=None)
            continue
        wire = len(f.params)
        comp = c[c_name] == wire + 1 if key == "p2" else False
        if c[c_name] != wire + comp or functions[name] != wire + comp:
            raise ValueError(f"{f.module} {f.name}: {wire} parameters in the table, {c[c_name]} in C, {functions[name]} in Python")
        f.extra.update(c_name=c_name, python_module=module, python_name=name, takes_comp=comp)


# ---- output -----------------------------------------------------------------------------------------------------------------


def dumps(obj: Any, level: int = 0) -> str:
    """JSON with two-space indent in which a flat dict (a parameter, a file record) stays on one line."""
    pad = "  " * (level + 1)
    end = "  " * level
    if isinstance(obj, dict):
        if all(not isinstance(v, (dict, list)) for v in obj.values()):  # type: ignore[union-attr]
            return json.dumps(obj, ensure_ascii=False)
        body = ",\n".join(f"{pad}{json.dumps(k)}: {dumps(v, level + 1)}" for k, v in obj.items())  # type: ignore[union-attr]
        return "{\n" + body + "\n" + end + "}"
    if isinstance(obj, list):
        if not obj:
            return "[]"
        body = ",\n".join(pad + dumps(v, level + 1) for v in obj)  # type: ignore[union-attr]
        return "[\n" + body + "\n" + end + "]"
    return json.dumps(obj, ensure_ascii=False)


def source_header(sources: dict[str, Any], key: str, records: list[dict[str, str]]) -> dict[str, Any]:
    src = sources[key]
    head: dict[str, Any] = {
        "repository": sources["repository"],
        "tag": src["tag"],
        "commit": src["commit"],
        "license": "tools/spec_sources/" + sources["license"],
        "files": records,
    }
    return head


def doc_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"""', '\\"\\"\\"')


PY_TYPE = {"i32": "int", "i64": "int", "f32": "float", "f64": "float"}
LINE = 120


def signature(f: Func) -> list[str]:
    names = [py_ident(p.name) for p in f.params]
    if len(set(names)) != len(names):
        raise ValueError(f"{f.module} {f.name}: parameter names clash after escaping: {names}")
    ret = PY_TYPE[f.results[0].type] if f.results else "None"
    args = [f"{n}: {PY_TYPE[p.type]}" for n, p in zip(names, f.params, strict=True)]
    one = "    def __call__(" + ", ".join(["self", *args] + (["/"] if args else [])) + f") -> {ret}: ..."
    if len(one) <= LINE:
        return [one]
    return ["    def __call__(", "        self,", *[f"        {a}," for a in args], "        /,", f"    ) -> {ret}: ..."]


def protocols(snapshot: str, title: str, funcs: list[Func], source_line: str) -> str:
    out = [
        f'"""{title}',
        "",
        "One `typing.Protocol` per function. The parameters are positional-only, so their names document the call and",
        "a function with other names (or none) is accepted.",
        "",
        "Generated by tools/make_signatures.py from " + source_line + "; do not edit.",
        "",
        "To use a Protocol, copy it (and `from typing import Protocol`) into your own stub. Do not import this file from a stub.",
        '"""',
        "",
        "from typing import Protocol",
    ]
    for f in funcs:
        out += ["", f"class {f.protocol}(Protocol):"]
        summary = textwrap.wrap(doc_escape(f.doc), 110, break_long_words=False, break_on_hyphens=False) if f.doc else []
        notes = [f"Import: `{f.module}` `{f.name}`."]
        if any(r.role == "errno" for r in f.results):
            notes += ["Returns a WASI errno (0 is success)."]
        retptrs = [p for p in f.params if p.role == "retptr"]
        if snapshot == "p1" and retptrs:
            notes += ["Results are written to memory at: " + ", ".join(f"`{p.name}` ({p.source_type})" for p in retptrs) + "."]
        if snapshot == "p2" and retptrs:
            notes += [f"The result `{retptrs[0].source_type}` is written to memory at `retptr`."]
        if f.extra.get("stability") == "unstable":
            notes += [f"Unstable (feature `{f.extra['feature']}`)."]
        if summary:
            lines = [*summary, "", *notes]
        else:
            lines = notes
        if len(lines) == 1:
            out += ['    """' + lines[0] + '"""', ""]
        else:
            out += ['    """' + lines[0], *[("    " + x) if x else "" for x in lines[1:]], '    """', ""]
        out += signature(f)
    return "\n".join(out) + "\n"


def generate() -> dict[str, bytes]:
    sources = json.loads(read_text(SOURCES / "sources.json"))
    result: dict[str, bytes] = {}
    for key, builder in (("p1", build_p1), ("p2", build_p2)):
        funcs, records = builder(sources[key])
        attach_implementations(funcs, key)
        if key == "p1":
            api = sources["p1"]["snapshot"]
            title = "Protocols for the functions that a core module imports from WASI 0.1 (`wasi_snapshot_preview1`)."
            ref = f"the witx of {sources['repository']} at {sources['p1']['tag']}"
        else:
            api = "wasi:*@" + sources["p2"]["version"]
            title = f"Protocols for the functions that a core module imports from WASI {sources['p2']['version']} (the lowered imports of `wasi:*@0.2.x`)."
            ref = f"the WIT of {sources['repository']} at {sources['p2']['tag']}"
        head: dict[str, Any] = {
            "schema": SCHEMA,
            "snapshot": key,
            "api": api,
            "generator": GENERATOR,
            "source": source_header(sources, key, records),
            "count": len(funcs),
            "functions": [f.as_dict() for f in funcs],
        }
        result[f"wasi_{key}.json"] = (dumps(head) + "\n").encode("utf-8")
        result[f"wasi_{key}_protocols.pyi"] = protocols(key, title, funcs, ref).encode("utf-8")
    return result


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0] if __doc__ else None)
    ap.add_argument("--check", action="store_true", help="compare with the files on disk, write nothing, exit 1 on a difference")
    ap.add_argument("--out", type=Path, default=OUT, help="the directory to write (default: src/fusedwasi/signatures)")
    args = ap.parse_args(argv)
    files = generate()
    bad = 0
    for name, data in files.items():
        path = args.out / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                print(f"differs: {path}")
                bad += 1
        else:
            args.out.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f"wrote {path}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
