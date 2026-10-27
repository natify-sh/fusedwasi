#!/usr/bin/env python3
"""The public names of fusedwasi, the mark of the private ones, and the check that keeps the two apart.

The data is `src/fusedwasi/public_api.json` (it travels with the wheel, like the tables of signatures): the names that are public, in C and in Python, with one line of purpose
for each name or group, and the fields of the public structs. Everything else is private and carries a mark (docs/design_model.md, section 5):

* C: a double underscore right after the prefix of the library: `fusedwasi_p2__table_get`, `FUSEDWASI_P2__KIND_P1_FDS`, `fusedwasi__x`; the private fields of a public struct end
  with two underscores: `state.slots__`. A public name never has `__` in it.
* Python: a leading underscore (`_check`, `State._slots`).

    python tools/public_api.py mark     rename every unmarked private name of C in the headers, the C of the tests, the tools and the documents (idempotent; run it after adding code)
    python tools/public_api.py check    list what breaks the rule (the test `tests/test_public_api.py` runs it)
    python tools/public_api.py doc      write docs/public_api.md from the data (`doc --check` compares)
    python tools/public_api.py headers  write the "Host API" block at the top of each header (`headers --check` compares)
    python tools/public_api.py stats    count the names

The renaming of the Python names is not done here: a leading underscore is written by the author, and `check` finds the name that has none.
"""

from __future__ import annotations

import ast
import fnmatch
import importlib
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from collections.abc import Iterable
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INCLUDE = SRC / "fusedwasi" / "include"
API = SRC / "fusedwasi" / "public_api.json"
DOC = ROOT / "docs" / "public_api.md"
MARK = "__"

if str(SRC) not in sys.path:  # run from a checkout that is not installed
    sys.path.insert(0, str(SRC))

# every identifier of the library: `fusedwasi_...` and `FUSEDWASI_...` that ends with a letter or a digit (`fusedwasi_p2_fs_*` in a text is not a name)
IDENT = re.compile(r"\b(?:fusedwasi|FUSEDWASI)_[A-Za-z0-9_]*[A-Za-z0-9]\b")
PREFIX = re.compile(r"^(?:fusedwasi|FUSEDWASI)_(?:(?:p[12]|P[12])_)?")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
MEMBER = re.compile(r"(?:\.|->)\s*([A-Za-z_][A-Za-z0-9_]*)")

# files the renaming leaves alone: the review of 2026-10-26 and the lists of what changed are a record of the names of their day
RECORDS = {"docs/interface_review.md", "docs/public_api.md", "docs/private_names.md"}
TEXT_SUFFIXES = {".h", ".c", ".py", ".md", ".txt", ".cmake", ".in", ".yml", ".toml"}


def load() -> dict[str, Any]:
    data: dict[str, Any] = json.loads(API.read_text(encoding="utf-8"))
    return data


def strip_comments(text: str) -> str:
    return COMMENT.sub(" ", text)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def headers() -> list[Path]:
    return sorted(INCLUDE.rglob("*.h"))


def universe() -> set[str]:
    """The identifiers of the library in the code of the headers (comments left out)."""
    names: set[str] = set()
    for header in headers():
        names.update(IDENT.findall(strip_comments(header.read_text(encoding="utf-8"))))
    return names


def signature_names() -> set[str]:
    names: set[str] = set()
    for snapshot in ("p1", "p2"):
        table = json.loads((SRC / "fusedwasi" / "signatures" / f"wasi_{snapshot}.json").read_text(encoding="utf-8"))
        names.update(row["c_name"] for row in table["functions"] if row.get("c_name"))
    return names


class CNames:
    """Which identifiers of C are public."""

    def __init__(self, api: dict[str, Any]) -> None:
        self.api = api["c"]
        self.patterns: list[str] = []
        for group in self.api["groups"]:
            self.patterns += [name for name in group["names"] if "<" not in name]
        self.private: list[str] = self.api["private"]
        self.signatures = signature_names()

    def is_public(self, name: str) -> bool:
        if MARK in name:
            return False
        if any(fnmatch.fnmatchcase(name, pattern) for pattern in self.private):
            return False
        return name in self.signatures or any(fnmatch.fnmatchcase(name, pattern) for pattern in self.patterns)

    def is_marked(self, name: str) -> bool:
        return MARK in name

    def marked(self, name: str) -> str:
        return PREFIX.sub(lambda m: m.group(0) + "_", name, count=1)

    def unmarked(self, name: str) -> str:
        return name.replace(MARK, "_", 1)


# ---- the fields of the public structs -----------------------------------------------------------------------------------------------------------------------------------------


def struct_bodies(text: str) -> Iterable[tuple[str, int, int]]:
    """(name, start, end) of the body of each `struct` that has one, by the tag or by the name of the typedef; start and end are inside the braces."""
    for match in re.finditer(r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)?\s*\{", text):
        start = match.end()
        depth, i = 1, start
        while depth and i < len(text):
            depth += (text[i] == "{") - (text[i] == "}")
            i += 1
        end = i - 1
        tail = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*;", text[i:])
        typedef = tail.group(1) if tail else None
        for name in {match.group(1), typedef} - {None}:
            yield name, start, end


def declarators(body: str) -> list[str]:
    """The names of the fields declared in the body of a struct (comments already left out)."""
    names: list[str] = []
    depth = 0
    piece = ""
    for ch in body:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        if ch == ";" and depth == 0:
            names += _declared(piece)
            piece = ""
        else:
            piece += ch
    return names


def _declared(declaration: str) -> list[str]:
    declaration = declaration.strip()
    if not declaration:
        return []
    function_pointer = re.search(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(", declaration)
    if function_pointer:
        return [function_pointer.group(1)]
    out: list[str] = []
    for part in re.split(r",(?![^(\[]*[\)\]])", declaration):
        found = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", re.sub(r"\[[^\]]*\]", "", part))
        if found:
            out.append(found[-1])
    return out


def fields_of(struct: str) -> list[str] | None:
    for header in headers():
        text = header.read_text(encoding="utf-8")
        code = strip_comments(text)
        for name, start, end in struct_bodies(code):
            if name == struct:
                return declarators(code[start:end])
    return None


# ---- C: the renaming -------------------------------------------------------------------------------------------------------------------------------------------------------


def tracked_files() -> list[Path]:
    listed = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split(
        "\n"
    )
    files: list[Path] = []
    for name in listed:
        path = ROOT / name
        if (
            not name
            or "spec_sources" in name
            or name in RECORDS
            or path.suffix not in TEXT_SUFFIXES
            and path.name != "CMakeLists.txt"
        ):
            continue
        files.append(path)
    return files


def c_strings_in_python(text: str) -> list[tuple[int, int]]:
    """The spans of the string literals of a Python file that hold C (an `#include`, or a name of the library)."""
    import io
    import tokenize

    spans: list[tuple[int, int]] = []
    lines = text.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type == tokenize.STRING and (
            "#include" in token.string or "fusedwasi_" in token.string or "FUSEDWASI_" in token.string
        ):
            spans.append((offsets[token.start[0] - 1] + token.start[1], offsets[token.end[0] - 1] + token.end[1]))
    return spans


def rename_fields(text: str, privates: dict[str, list[str]], *, whole: bool) -> str:
    """The private fields of the public structs get the mark: at their declaration (the body of the struct) and wherever they are reached with `.` or `->`."""
    names = sorted({p for fields in privates.values() for p in fields}, key=len, reverse=True)
    if not names:
        return text
    alternatives = "|".join(re.escape(n) for n in names)
    uses = re.compile(rf"((?:\.|->)\s*)({alternatives})\b(?!__)")
    if whole:
        code = text
        for struct, fields in privates.items():
            # the declarations: in the body of the struct, outside comments
            for name, start, end in sorted(struct_bodies(strip_comments_keep_length(code)), key=lambda s: -s[1]):
                if name != struct:
                    continue
                body = code[start:end]
                for field in fields:
                    body = replace_outside_comments(body, rf"\b{re.escape(field)}\b(?!__)", field + MARK)
                code = code[:start] + body + code[end:]
        return uses.sub(lambda m: m.group(1) + m.group(2) + MARK, code)
    return uses.sub(lambda m: m.group(1) + m.group(2) + MARK, text)


def strip_comments_keep_length(text: str) -> str:
    return COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def replace_outside_comments(text: str, pattern: str, replacement: str) -> str:
    out: list[str] = []
    last = 0
    for comment in COMMENT.finditer(text):
        out.append(re.sub(pattern, replacement, text[last : comment.start()]))
        out.append(comment.group(0))
        last = comment.end()
    out.append(re.sub(pattern, replacement, text[last:]))
    return "".join(out)


def mark_c(names: CNames, files: list[Path], universe_: set[str]) -> dict[str, str]:
    renames = {n: names.marked(n) for n in universe_ if not names.is_public(n) and not names.is_marked(n)}
    privates = {struct: spec["private"] for struct, spec in names.api["fields"].items() if spec["private"]}
    pattern = re.compile("|".join(sorted((re.escape(n) for n in renames), key=len, reverse=True)) or r"(?!x)x")
    word = re.compile(rf"\b(?:{pattern.pattern})\b")
    for path in files:
        text = path.read_text(encoding="utf-8")
        new = word.sub(lambda m: renames[m.group(0)], text) if renames else text
        if path.suffix in {".h", ".c"}:
            new = rename_fields(new, privates, whole=path.suffix == ".h")
        elif path.suffix == ".py" and ("fusedwasi_" in new or "FUSEDWASI_" in new):
            spans = c_strings_in_python(new)
            for start, end in reversed(spans):
                new = new[:start] + rename_fields(new[start:end], privates, whole=False) + new[end:]
        if new != text:
            path.write_text(new, encoding="utf-8")
    return renames


# ---- C: the check -----------------------------------------------------------------------------------------------------------------------------------------------------------


def check_c(api: dict[str, Any]) -> list[str]:
    problems: list[str] = []
    names = CNames(api)
    known = universe()
    # 1. every name of the public headers is public, or marked
    for header in headers():
        for name in sorted(set(IDENT.findall(header.read_text(encoding="utf-8")))):
            if name in known and not names.is_public(name) and not names.is_marked(name):
                problems.append(
                    f"{rel(header)}: `{name}` is neither listed in public_api.json nor marked (run tools/public_api.py mark, or list it)"
                )
    for name in sorted(known):
        if names.is_marked(name) and names.is_public(name):
            problems.append(f"`{name}` is marked and listed as public")
    # 2. every public name exists
    for pattern in names.patterns:
        if not any(
            fnmatch.fnmatchcase(name, pattern) and not any(fnmatch.fnmatchcase(name, p) for p in names.private)
            for name in known
        ):
            problems.append(f"public name `{pattern}` is not in the headers")
    for pattern in names.private:
        if not any(
            fnmatch.fnmatchcase(name, pattern) or fnmatch.fnmatchcase(names.unmarked(name), pattern) for name in known
        ):
            problems.append(f"private exception `{pattern}` is not in the headers")
    missing = sorted(n for n in names.signatures if n not in known)
    problems += [f"`{n}` of the tables of signatures is not in the headers" for n in missing]
    # 3. the fields of the public structs
    for struct, spec in api["c"]["fields"].items():
        found = fields_of(struct)
        if found is None:
            problems.append(f"struct `{struct}` has no body in the headers")
            continue
        expected = set(spec["public"]) | {p + MARK for p in spec["private"]}
        for field in found:
            if field not in expected:
                problems.append(
                    f"field `{struct}.{field}` is neither listed as public nor marked (a private field ends with `{MARK}`)"
                )
        for field in sorted(expected - set(found)):
            problems.append(f"listed field `{struct}.{field}` is not in the struct")
    return problems


def neutral_c_sources() -> dict[str, str]:
    """The C of the neutral consumers: their source and the glue that is generated for them."""
    folder = ROOT / "tests" / "neutral" / "c_wasm3"
    sources = {
        str(p.relative_to(ROOT)): p.read_text(encoding="utf-8")
        for p in sorted(folder.glob("*.c")) + sorted(folder.glob("*.h"))
    }
    spec = importlib.util.spec_from_file_location("gen_wasm3_glue", folder / "gen_wasm3_glue.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory() as tmp:
        target = Path(tmp) / "glue.h"
        module.main(str(target))
        sources["the generated glue (gen_wasm3_glue.py)"] = target.read_text(encoding="utf-8")
    return sources


def check_neutral_c(api: dict[str, Any]) -> list[str]:
    problems: list[str] = []
    names = CNames(api)
    private_fields = {f for spec in api["c"]["fields"].values() for f in spec["private"]}
    for where, text in neutral_c_sources().items():
        code = strip_comments(text)
        for name in sorted(set(IDENT.findall(code))):
            if not names.is_public(name):
                problems.append(f"{where}: uses `{name}`, which is not public")
        for word in sorted(set(re.findall(r"\b[A-Za-z0-9][A-Za-z0-9_]*__[A-Za-z0-9_]*\b", code))):
            problems.append(f"{where}: `{word}` carries the mark of a private name")
        for member in sorted(set(MEMBER.findall(code))):
            if member in private_fields or member.endswith(MARK):
                problems.append(f"{where}: reaches the private field `{member}`")
    return problems


# ---- Python ---------------------------------------------------------------------------------------------------------------------------------------------------------------


def module_path(module: str) -> Path:
    base = SRC / Path(*module.split("."))
    return base / "__init__.py" if base.is_dir() else base.with_suffix(".py")


def definitions(tree: ast.Module) -> list[str]:
    names: list[str] = []

    def targets(node: ast.expr) -> list[str]:
        if isinstance(node, ast.Name):
            return [node.id]
        if isinstance(node, (ast.Tuple, ast.List)):
            return [n for element in node.elts for n in targets(element)]
        return []

    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names.append(node.name)
        elif isinstance(node, ast.Assign):
            names += [n for target in node.targets for n in targets(target)]
        elif isinstance(node, ast.AnnAssign):
            names += targets(node.target)
    return names


def python_modules() -> list[str]:
    modules: list[str] = []
    for path in sorted((SRC / "fusedwasi").rglob("*.py")):
        relative = path.relative_to(SRC).with_suffix("")
        parts = list(relative.parts)
        if parts[-1] == "__init__":
            parts.pop()
        modules.append(".".join(parts))
    return modules


def python_table_functions() -> dict[str, set[str]]:
    rows: dict[str, set[str]] = {}
    for snapshot in ("p1", "p2"):
        table = json.loads((SRC / "fusedwasi" / "signatures" / f"wasi_{snapshot}.json").read_text(encoding="utf-8"))
        for row in table["functions"]:
            if row.get("python_name"):
                rows.setdefault(row["python_module"], set()).add(row["python_name"])
    return rows


def public_python_names(api: dict[str, Any], module: str) -> list[str]:
    spec = api["python"]["modules"][module]
    patterns = list(spec["names"])
    if "reexported_by" in spec:
        patterns += list(api["python"]["modules"][spec["reexported_by"]]["names"])
    return patterns


def matches(name: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)


def check_python(api: dict[str, Any]) -> list[str]:
    problems: list[str] = []
    modules = api["python"]["modules"]
    table = python_table_functions()
    present = python_modules()
    for module in present:
        if module not in modules and module != "fusedwasi.__main__":
            problems.append(f"module `{module}` is not in public_api.json")
    for module in modules:
        if module not in present:
            problems.append(f"module `{module}` of public_api.json does not exist")
            continue
        tree = ast.parse(module_path(module).read_text(encoding="utf-8"))
        patterns = public_python_names(api, module)
        functions = table.get(module, set()) if modules[module].get("imports") else set()
        defined = definitions(tree)
        imported = importlib.import_module(module)
        if module_path(module).name == "__init__.py" and hasattr(imported, "__all__"):
            exported = set(imported.__all__)
            expected = {n for n in vars(imported) if matches(n, patterns)}
            for name in sorted(exported ^ expected):
                problems.append(f"`{module}.__all__` and public_api.json differ about `{name}`")
        for name in defined:
            if name.startswith("_"):
                continue
            if name not in functions and not matches(name, patterns):
                problems.append(
                    f"{module}: `{name}` is neither listed in public_api.json nor private (a private name starts with an underscore)"
                )
        for pattern in patterns:
            if "*" in pattern or "?" in pattern:
                if not any(matches(n, [pattern]) for n in vars(imported)):
                    problems.append(f"{module}: public name `{pattern}` matches nothing")
            elif not hasattr(imported, pattern):
                problems.append(f"{module}: public name `{pattern}` does not exist")
        for name in sorted(functions):
            if not hasattr(imported, name):
                problems.append(f"{module}: the table of signatures names `{name}`, which does not exist")
    problems += check_members(api)
    return problems


def check_members(api: dict[str, Any]) -> list[str]:
    problems: list[str] = []
    from fusedwasi.p2 import Guest, State

    samples: dict[str, object] = {}
    state = State()
    samples["fusedwasi.p2.State"] = state
    samples["fusedwasi.p2.Guest"] = Guest(state, bytearray(16))
    for qualified, spec in api["python"]["members"].items():
        obj = samples[qualified]
        found = {n for n in dir(obj) if not n.startswith("_")}
        public = set(spec["public"])
        for name in sorted(found - public):
            problems.append(
                f"{qualified}.{name} is neither listed in public_api.json nor private (a private member starts with an underscore)"
            )
        for name in sorted(public - found):
            problems.append(f"{qualified}.{name} is listed and does not exist")
        for name in spec["private"]:
            if not hasattr(obj, "_" + name):
                problems.append(f"{qualified}._{name} (listed as private) does not exist")
    return problems


def private_python_names() -> set[str]:
    """Every name with a leading underscore (not a dunder) that the library defines, at the top of a module or in a class."""
    names: set[str] = set()
    for path in (SRC / "fusedwasi").rglob("*.py"):
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            found: list[str] = []
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
                found.append(node.name)
            elif isinstance(node, ast.Attribute) and isinstance(node.ctx, ast.Store):
                found.append(node.attr)
            elif isinstance(node, ast.Name) and isinstance(node.ctx, ast.Store):
                found.append(node.id)
            names.update(n for n in found if n.startswith("_") and not n.startswith("__"))
    return names


def check_neutral_python(api: dict[str, Any]) -> list[str]:
    """The neutral users reach only public names: no name with an underscore of the library, and every name taken from a module of fusedwasi is listed."""
    problems: list[str] = []
    private = private_python_names()
    modules = api["python"]["modules"]
    for path in sorted((ROOT / "tests" / "neutral").glob("*.py")) + sorted(
        (ROOT / "tests" / "neutral" / "c_wasm3").glob("*.py")
    ):
        tree = ast.parse(path.read_text(encoding="utf-8"))
        where = str(path.relative_to(ROOT))
        aliases: dict[str, str] = {}
        for node in ast.walk(tree):
            if (
                isinstance(node, ast.ImportFrom)
                and node.module
                and (node.module == "fusedwasi" or node.module.startswith("fusedwasi."))
            ):
                for alias in node.names:
                    target = f"{node.module}.{alias.name}"
                    if target in modules:
                        aliases[alias.asname or alias.name] = target
                    elif not matches(alias.name, public_python_names(api, node.module)) and not (
                        node.module in modules
                        and modules[node.module].get("imports")
                        and alias.name in python_table_functions().get(node.module, set())
                    ):
                        problems.append(f"{where}: imports `{alias.name}` from `{node.module}`, which is not public")
            elif isinstance(node, ast.Import):
                for alias in node.names:
                    if alias.name == "fusedwasi" or alias.name.startswith("fusedwasi."):
                        aliases[alias.asname or alias.name.split(".")[0]] = (
                            alias.name if alias.asname else alias.name.split(".")[0]
                        )
        for node in ast.walk(tree):
            if isinstance(node, ast.Attribute):
                chain: list[str] = []
                cursor: ast.expr = node
                while isinstance(cursor, ast.Attribute):
                    chain.append(cursor.attr)
                    cursor = cursor.value
                if isinstance(cursor, ast.Name) and cursor.id in aliases:
                    module = aliases[cursor.id]
                    for attribute in reversed(chain):
                        if f"{module}.{attribute}" in modules:
                            module = f"{module}.{attribute}"
                            continue
                        functions = python_table_functions().get(module, set())
                        if attribute not in functions and not matches(attribute, public_python_names(api, module)):
                            problems.append(f"{where}: `{cursor.id}...{attribute}` is not a public name of `{module}`")
                        break
                if not (isinstance(cursor, ast.Name) and cursor.id == "self") and node.attr in private:
                    problems.append(f"{where}: reaches `{node.attr}`, a private name of the library")
    return problems


# ---- Python: the renaming -----------------------------------------------------------------------------------------------------------------------------------------------


def python_private_definitions(api: dict[str, Any]) -> dict[str, set[str]]:
    """For each module, the names it defines at its top that are neither public nor a function of the tables (they get a leading underscore)."""
    table = python_table_functions()
    out: dict[str, set[str]] = {}
    for module, spec in api["python"]["modules"].items():
        tree = ast.parse(module_path(module).read_text(encoding="utf-8"))
        functions = table.get(module, set()) if spec.get("imports") else set()
        patterns = public_python_names(api, module)
        names = {
            d for d in definitions(tree) if not d.startswith("_") and d not in functions and not matches(d, patterns)
        }
        if names:
            out[module] = names
    return out


class _Scopes:
    """Which Name nodes of a module refer to its top level: not a parameter, not a local, not a name of a class body (outside the class body itself)."""

    def __init__(self, tree: ast.Module) -> None:
        self.refs: list[ast.Name] = []
        self.frames: list[tuple[str, set[str]]] = []
        self.visit_body(tree.body, None)

    @staticmethod
    def bound(body: list[ast.stmt], arguments: ast.arguments | None) -> set[str]:
        names: set[str] = set()
        declared_global: set[str] = set()
        if arguments:
            for a in [
                *arguments.posonlyargs,
                *arguments.args,
                *arguments.kwonlyargs,
                arguments.vararg,
                arguments.kwarg,
            ]:
                if a:
                    names.add(a.arg)

        def walk(node: ast.AST) -> None:
            for child in ast.iter_child_nodes(node):
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                    names.add(child.name)
                    continue
                if isinstance(child, ast.Lambda):
                    continue
                if isinstance(child, ast.Name) and isinstance(child.ctx, (ast.Store, ast.Del)):
                    names.add(child.id)
                elif isinstance(child, (ast.Import, ast.ImportFrom)):
                    names.update((a.asname or a.name).split(".")[0] for a in child.names)
                elif isinstance(child, ast.ExceptHandler) and child.name:
                    names.add(child.name)
                elif isinstance(child, ast.Global):
                    declared_global.update(child.names)
                walk(child)

        for statement in body:
            holder = ast.Module(body=[statement], type_ignores=[])
            if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                names.add(statement.name)
            else:
                if isinstance(statement, (ast.Import, ast.ImportFrom)):
                    names.update((a.asname or a.name).split(".")[0] for a in statement.names)
                walk(holder)
        return names - declared_global

    def visit_body(self, body: list[ast.stmt], arguments: ast.arguments | None, kind: str = "module") -> None:
        if kind != "module":
            self.frames.append((kind, self.bound(body, arguments)))
        for statement in body:
            self.visit(statement)
        if kind != "module":
            self.frames.pop()

    def local(self, name: str) -> bool:
        for index in range(len(self.frames) - 1, -1, -1):
            kind, names = self.frames[index]
            if kind == "class" and index != len(self.frames) - 1:
                continue
            if name in names:
                return True
        return False

    def visit(self, node: ast.AST) -> None:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            for decorator in node.decorator_list:
                self.visit(decorator)
            for default in [*node.args.defaults, *[d for d in node.args.kw_defaults if d]]:
                self.visit(default)
            for a in [
                *node.args.posonlyargs,
                *node.args.args,
                *node.args.kwonlyargs,
                node.args.vararg,
                node.args.kwarg,
            ]:
                if a and a.annotation:
                    self.visit(a.annotation)
            if node.returns:
                self.visit(node.returns)
            self.visit_body(node.body, node.args, "function")
            return
        if isinstance(node, ast.Lambda):
            self.frames.append(("function", self.bound([], node.args)))
            self.visit(node.body)
            self.frames.pop()
            return
        if isinstance(node, ast.ClassDef):
            for expression in [*node.bases, *[k.value for k in node.keywords], *node.decorator_list]:
                self.visit(expression)
            self.visit_body(node.body, None, "class")
            return
        if isinstance(node, (ast.ListComp, ast.SetComp, ast.GeneratorExp, ast.DictComp)):
            targets: set[str] = set()
            for generator in node.generators:
                for sub in ast.walk(generator.target):
                    if isinstance(sub, ast.Name):
                        targets.add(sub.id)
            self.frames.append(("function", targets))
            for child in ast.iter_child_nodes(node):
                self.visit(child)
            self.frames.pop()
            return
        if isinstance(node, ast.Name):
            if not self.local(node.id):
                self.refs.append(node)
            return
        for child in ast.iter_child_nodes(node):
            self.visit(child)


def module_of_file(path: Path) -> str | None:
    try:
        relative = path.relative_to(SRC).with_suffix("")
    except ValueError:
        return None
    parts = list(relative.parts)
    if parts[-1] == "__init__":
        parts.pop()
    return ".".join(parts)


def dotted(node: ast.expr) -> str | None:
    parts: list[str] = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if isinstance(node, ast.Name):
        parts.append(node.id)
        return ".".join(reversed(parts))
    return None


def rename_python_file(path: Path, privates: dict[str, set[str]]) -> int:
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source)
    this = module_of_file(path)
    raw = source.encode("utf-8")
    starts = [0]
    for line in raw.split(b"\n"):
        starts.append(starts[-1] + len(line) + 1)
    edits: list[tuple[int, int, str]] = []

    def at(lineno: int, col: int) -> int:
        return starts[lineno - 1] + col

    own = privates.get(this, set()) if this else set()
    imported: set[str] = set()  # names bound here by `from <module> import <private name>`
    aliases: dict[str, str] = {}  # a name that stands for a module of the library -> its dotted name
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            if node.level and this:
                base = this.split(".")
                package = base if module_path(this).name == "__init__.py" else base[:-1]
                package = package[: len(package) - (node.level - 1)] if node.level > 1 else package
                resolved = ".".join([*package, *([node.module] if node.module else [])])
            else:
                resolved = node.module or ""
            for alias in node.names:
                if alias.name in privates.get(resolved, set()):
                    edits.append(
                        (
                            at(alias.lineno, alias.col_offset),
                            at(alias.lineno, alias.col_offset) + len(alias.name.encode()),
                            "_" + alias.name,
                        )
                    )
                    if not alias.asname:
                        imported.add(alias.name)
                elif f"{resolved}.{alias.name}" in privates or f"{resolved}.{alias.name}" in python_modules():
                    aliases[alias.asname or alias.name] = f"{resolved}.{alias.name}"
        elif isinstance(node, ast.Import):
            for alias in node.names:
                if alias.name.startswith("fusedwasi"):
                    aliases[alias.asname or alias.name.split(".")[0]] = (
                        alias.name if alias.asname else alias.name.split(".")[0]
                    )
    scopes = _Scopes(tree)
    top = own | imported
    for ref in scopes.refs:
        if ref.id in top:
            edits.append(
                (at(ref.lineno, ref.col_offset), at(ref.lineno, ref.col_offset) + len(ref.id.encode()), "_" + ref.id)
            )
    # the definitions at the top of the module
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and node.name in own:
            begin = at(node.lineno, node.col_offset)
            found = re.compile(rb"\b(def|class)\s+(" + re.escape(node.name.encode()) + rb")\b").search(raw, begin)
            assert found, (path, node.name)
            edits.append((found.start(2), found.end(2), "_" + node.name))
    # `module.name` where the module is one of the library's
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute):
            base = dotted(node.value)
            if base is None:
                continue
            head, _, rest = base.partition(".")
            if head in aliases:
                module = aliases[head] + ("." + rest if rest else "")
            elif base.startswith("fusedwasi"):
                module = base
            else:
                continue
            if node.attr in privates.get(module, set()):
                end = starts[node.end_lineno - 1] + node.end_col_offset  # type: ignore[operator]
                edits.append((end - len(node.attr.encode()), end, "_" + node.attr))
    if not edits:
        return 0
    result = bytearray(raw)
    for begin, end, text in sorted(set(edits), reverse=True):
        result[begin:end] = text.encode()
    path.write_bytes(bytes(result))
    return len(set(edits))


def mark_python(api: dict[str, Any]) -> int:
    privates = python_private_definitions(api)
    total = 0
    skip = {ROOT / "tools" / "public_api.py"}
    for path in tracked_files():
        if path.suffix == ".py" and path not in skip and "tools/spec_sources" not in str(path):
            total += rename_python_file(path, privates)
    return total


# ---- the "Host API" block at the top of each header -----------------------------------------------------------------------------------------------------------------------

IMPORT_HEADERS = {"fusedwasi/p1/wasi_p1_core.h"} | {
    f"fusedwasi/p2/wasi_p2_{p}.h" for p in ("cli", "clocks", "filesystem", "http", "io", "random", "sockets")
}
BLOCK = re.compile(r"\n[^\n]*Host API \(docs/public_api\.md\).*?(?=\*/\Z)", re.S)


def header_block(api: dict[str, Any], relative: str, prefix: str) -> list[str]:
    names: list[str] = api["c"]["headers"].get(relative, [])
    table = "wasi_p1.json" if "/p1/" in relative else "wasi_p2.json"
    imports = (
        f"the functions of its imports (`c_name` in signatures/{table})"
        if relative in IMPORT_HEADERS
        else "no function of an import"
    )
    if names:
        head = f"Host API (docs/public_api.md): {imports}, and"
    else:
        head = f"Host API (docs/public_api.md): {imports}; no other name is public."
    lines = [head]
    if names:
        words: list[str] = []
        for name in names:
            words.append(name + ",")
        words[-1] = words[-1][:-1] + "."
        line = "  "
        for word in words:
            if len(line) + len(word) > 150:
                lines.append(line.rstrip())
                line = "  "
            line += word + " "
        lines.append(line.rstrip())
    lines.append(
        "Every other name in this file is private: a double underscore after the prefix (`fusedwasi_p2__x`, `FUSEDWASI_P2__X`), and a private field ends with two underscores (`x__`)."
    )
    return [prefix + line if line else prefix.rstrip() for line in lines]


def with_block(api: dict[str, Any], relative: str, text: str) -> str:
    first = re.match(r"\s*/\*(.*?)\*/", text, re.S)
    assert first, relative
    body = first.group(1)
    body = BLOCK.sub("", body + "*/")[:-2] if BLOCK.search(body + "*/") else body
    prefix = " * " if "\n * " in body else "   " if "\n   " in body else " * "
    body = re.sub(r"(\s*\n[ ]*\*?)*\s*$", "", body)
    # a header with a single line of comment becomes a block of several
    separator = prefix.rstrip() if prefix.strip() else ""
    block = "\n".join(header_block(api, relative, prefix))
    comment = "/*" + body + "\n" + separator + "\n" + block + " */"
    return text[: first.start()] + comment + text[first.end() :]


def check_headers(api: dict[str, Any], *, write: bool = False) -> list[str]:
    problems: list[str] = []
    listed: list[str] = []
    for relative, names in api["c"]["headers"].items():
        listed += names
    expected = [name for group in api["c"]["groups"] if group["id"] != "imports" for name in group["names"]]
    for name in sorted(set(expected) - set(listed)):
        problems.append(f"public name `{name}` is in no header block (`headers` of public_api.json)")
    for name in sorted(set(listed) - set(expected)):
        problems.append(f"`{name}` of `headers` is not a public name")
    for name in {n for n in listed if listed.count(n) > 1}:
        problems.append(f"`{name}` is in the block of two headers")
    for header in headers():
        relative = str(header.relative_to(INCLUDE))
        text = header.read_text(encoding="utf-8")
        if relative == "fusedwasi.h":
            continue
        new = with_block(api, relative, text)
        if new != text:
            if write:
                header.write_text(new, encoding="utf-8")
            else:
                problems.append(
                    f'{relative}: the "Host API" block at the top is not the one public_api.json gives (run tools/public_api.py headers)'
                )
    return problems


def check_all() -> list[str]:
    api = load()
    return (
        check_c(api)
        + check_neutral_c(api)
        + check_python(api)
        + check_neutral_python(api)
        + check_doc(api)
        + check_headers(api)
    )


# ---- the document ---------------------------------------------------------------------------------------------------------------------------------------------------------


def render(api: dict[str, Any]) -> str:
    names = CNames(api)
    known = universe()
    lines = [
        "# The public names",
        "",
        "Written by `tools/public_api.py doc` from `src/fusedwasi/public_api.json`; do not edit. The rule (`design_model.md`, section 5): a name is public only if it is listed here "
        "(or is a function of the tables of signatures); every other name of the library is private and carries a mark that a user cannot miss, so that a change of a private "
        "name breaks nobody.",
        "",
        "* **C**: a private name has a double underscore right after the prefix of the library (`fusedwasi_p2__table_get`, `FUSEDWASI_P2__INDEX_BITS`); a private field of a public "
        "struct ends with two underscores (`state.slots__`). No public name has `__` in it.",
        "* **Python**: a private name has a leading underscore (`_check`, `State._slots`). A module lists no other name than the ones below and the functions of its imports.",
        "* **unstable**: public in the alpha, with no promise: wasi:http and wasi:sockets.",
        "",
        f"`tests/test_public_api.py` fails when a name of the headers is neither here nor marked, when a name here does not exist, when a neutral user (`tests/neutral/`) uses a name that is not here, "
        f"and when a module of the Python twin has a name with no underscore that is not here. {count_line(api, names, known)}",
        "",
        "## C",
        "",
    ]
    for group in api["c"]["groups"]:
        title = group["title"] + (" (unstable in the alpha)" if group["stability"] == "unstable" else "")
        lines += [f"### {title}", ""]
        if group.get("note"):
            lines += [f"_{group['note']}_", ""]
        lines += ["| Name | Purpose |", "| --- | --- |"]
        lines += [f"| `{name}` | {purpose} |" for name, purpose in group["names"].items()]
        lines.append("")
    lines += [
        "Not public although they match a pattern above: " + ", ".join(f"`{p}`" for p in api["c"]["private"]) + ".",
        "",
        "### Fields of the public structs",
        "",
    ]
    for struct, spec in api["c"]["fields"].items():
        lines += [f"`{struct}`:", "", "| Field | Purpose |", "| --- | --- |"]
        lines += [f"| `{field}` | {purpose} |" for field, purpose in spec["public"].items()]
        if spec["private"]:
            lines.append(
                "| "
                + ", ".join(f"`{p}{MARK}`" for p in spec["private"])
                + " | private: not to be read or written by a host |"
            )
        lines.append("")
    lines += ["## Python", ""]
    for module, spec in api["python"]["modules"].items():
        title = f"`{module}`" + (" (unstable in the alpha)" if spec.get("stability") == "unstable" else "")
        lines += [
            f"### {title}",
            "",
            spec["purpose"]
            + (
                ".  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`)."
                if spec.get("imports")
                else "."
            ),
            "",
        ]
        if spec["names"]:
            lines += ["| Name | Purpose |", "| --- | --- |"]
            lines += [f"| `{name}` | {purpose} |" for name, purpose in spec["names"].items()]
            lines.append("")
    lines += ["### Members of the classes", ""]
    for qualified, spec in api["python"]["members"].items():
        lines += [f"`{qualified}`:", "", "| Member | Purpose |", "| --- | --- |"]
        lines += [f"| `{member}` | {purpose} |" for member, purpose in spec["public"].items()]
        lines.append("| " + ", ".join(f"`_{p}`" for p in spec["private"]) + " | private |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def stats(api: dict[str, Any]) -> dict[str, int]:
    names = CNames(api)
    known = universe()
    public_c = {n for n in known if names.is_public(n)}
    private_c = {n for n in known if not names.is_public(n)}
    imports = len(names.signatures)
    return {
        "c_public_all": len(public_c),
        "c_public_without_imports": len(public_c - names.signatures),
        "c_imports": imports,
        "c_private": len(private_c),
        "c_private_fields": sum(len(s["private"]) for s in api["c"]["fields"].values()),
    }


def count_line(api: dict[str, Any], names: CNames, known: set[str]) -> str:
    s = stats(api)
    return (
        f"Counts at the last run: C {s['c_public_without_imports']} public names besides the {s['c_imports']} functions of the imports, {s['c_private']} private names"
        f" and {s['c_private_fields']} private fields."
    )


def check_doc(api: dict[str, Any]) -> list[str]:
    if not DOC.exists() or DOC.read_text(encoding="utf-8") != render(api):
        return ["docs/public_api.md is not the one that public_api.json gives (run tools/public_api.py doc)"]
    return []


def main(argv: list[str]) -> int:
    command = argv[1] if len(argv) > 1 else "check"
    api = load()
    if command == "mark":
        names = CNames(api)
        renames = mark_c(names, tracked_files(), universe())
        print(f"{len(renames)} names marked")
        return 0
    if command == "headers":
        problems = check_headers(api, write="--check" not in argv)
        print("\n".join(problems) or "ok")
        return 1 if problems and "--check" in argv else 0
    if command == "mark-python":
        print(f"{mark_python(api)} edits")
        return 0
    if command == "check":
        problems = check_all()
        print("\n".join(problems) or "ok")
        return 1 if problems else 0
    if command == "doc":
        if "--check" in argv:
            problems = check_doc(api)
            print("\n".join(problems) or "ok")
            return 1 if problems else 0
        DOC.write_text(render(api), encoding="utf-8")
        return 0
    if command == "stats":
        print(json.dumps(stats(api), indent=2))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
