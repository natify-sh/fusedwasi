#!/usr/bin/env python3
"""Check `docs/features.json` (the feature matrix of fusedwasi) against both twins, the C headers and the Python modules.

The file lists modules; each module lists features; each feature has a status for `c` and for `python`: `true` (every function of the feature is there and the documents name no limit), `false` (not there) or
`["partial", "<note>"]` (the documents name a limit; the note names the file that says it). The format is described in `docs/features.md`.

    python tools/check_features.py             check the file; exit 1 and a line for each mismatch, or 0
    python tools/check_features.py --list      the modules and the features as a table (and check)
    python tools/check_features.py --json      the computed matrix (what the code has) as JSON (and check)
    python tools/check_features.py --doc FILE  check another file (the tests use it with broken copies)

Both twins are checked in the same way: the functions of a feature come from the tables of signatures (`src/fusedwasi/signatures/`), the C name must be defined in a header of `include/fusedwasi/p1/` or
`p2/` (found with a regular expression, as `tests/test_known_limits.py` does), the Python name must be a function of its module (imported, `getattr`). No network; standard library only.
"""

from __future__ import annotations

import argparse
import fnmatch
import importlib
import inspect
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "src" / "fusedwasi"
C_ROOT = PACKAGE / "include" / "fusedwasi"
DEFAULT_DOC = ROOT / "docs" / "features.json"
TWINS = ("c", "python")
FORMAT = "fusedwasi-features"
SOURCE = re.compile(r"\b((?:docs|src|tests|tools)/[\w./-]*\w)")  # a path of the repository in a note
MISSING_WORD = re.compile(r"\bmissing\b|\bmiss\b|\bnot there\b|\bno function\b", re.IGNORECASE)

Status = Any  # True, False or ["partial", note]


# ---- what the code has ---------------------------------------------------------------------------------------------------------


def c_functions() -> set[str]:
    """The names of the functions defined in the headers of 0.1 and 0.2 (a definition starts with FUSEDWASI_P?_FN)."""
    names: set[str] = set()
    for family in ("p1", "p2"):
        for header in sorted((C_ROOT / family).glob("*.h")):
            text = header.read_text(encoding="utf-8")
            for m in re.finditer(
                rf"^FUSEDWASI_{family.upper()}_FN\s+[\w\s\*]+?\s+(fusedwasi_\w+)\s*\(", text, re.MULTILINE
            ):
                names.add(m.group(1))
    return names


def c_words() -> set[str]:
    """Every identifier of the headers (for the symbols that are not functions: a field, a type)."""
    words: set[str] = set()
    for header in sorted(C_ROOT.rglob("*.h")):
        words.update(re.findall(r"\w+", header.read_text(encoding="utf-8")))
    return words


def python_function(module: str | None, name: str | None) -> bool:
    if module is None or name is None:
        return False
    try:
        mod = importlib.import_module(module)
    except ImportError:
        return False
    return inspect.isfunction(getattr(mod, name, None))


def python_symbol(symbol: str) -> bool:
    """`module:Name`, `module:Class.attr` or `module:Class(param)` (a parameter of the constructor)."""
    module, _, rest = symbol.partition(":")
    try:
        obj: Any = importlib.import_module(module)
    except ImportError:
        return False
    param = None
    if rest.endswith(")") and "(" in rest:
        rest, _, param = rest[:-1].partition("(")
    for part in rest.split("."):
        if not hasattr(obj, part):
            return False
        obj = getattr(obj, part)
    if param is not None:
        try:
            return param in inspect.signature(obj).parameters
        except (TypeError, ValueError):
            return False
    return True


def load_signatures() -> tuple[list[dict[str, Any]], dict[str, Any]]:
    p2: dict[str, Any] = json.loads((PACKAGE / "signatures" / "wasi_p2.json").read_text(encoding="utf-8"))
    p1: dict[str, Any] = json.loads((PACKAGE / "signatures" / "wasi_p1.json").read_text(encoding="utf-8"))
    return p2["functions"], p1


# ---- the imports of each module, and the claim of each feature ----------------------------------------------------------------


def module_imports(name: str, p2: list[dict[str, Any]], p1: dict[str, Any]) -> list[dict[str, Any]]:
    """The imports of a module as dicts with `key` (interface, or the call), `name`, `c`, `python` (the name of the function, or None)."""
    out: list[dict[str, Any]] = []
    if name.startswith("wasi:"):
        for f in p2:
            if f["package"] == name:
                out.append(
                    {
                        "key": f["interface"],
                        "name": f["name"],
                        "c": f["c_name"],
                        "python": (f["python_module"], f["python_name"]),
                    }
                )
    elif name == "wasi_snapshot_preview1":
        for f in p1["functions"]:
            out.append(
                {"key": f["name"], "name": f["name"], "c": f["c_name"], "python": (f["python_module"], f["python_name"])}
            )
    elif name == "wasi_unstable":
        differs = set(p1["unstable"]["differs"])
        for f in p1["unstable"]["functions"]:
            out.append(
                {
                    "key": f["name"],
                    "name": f["name"],
                    "c": f["c_name"],
                    "python": (f["python_module"], f["python_name"]),
                    "differs": f["name"] in differs,
                }
            )
    return out


def claims(feature: dict[str, Any], item: dict[str, Any]) -> int:
    """How strongly a feature claims an import: 3 `interface#name` or a call by its exact name, 2 a pattern or `@differs`, 1 an interface, `@rest` is 0 (what is left), -1 no claim."""
    best = -1
    for cover in feature.get("covers", []):
        if cover == "@rest":
            level = 0
        elif cover == "@differs":
            level = 2 if item.get("differs") else -1
        elif "#" in cover:
            level = 3 if cover == f"{item['key']}#{item['name']}" else -1
        elif cover == item["key"] and item["key"] != item["name"]:
            level = 1
        elif any(ch in cover for ch in "*?["):
            level = 2 if fnmatch.fnmatchcase(item["key"], cover) else -1
        else:
            level = 3 if cover == item["name"] else -1
        best = max(best, level)
    return best


def assign(
    module: str, features: dict[str, Any], imports: list[dict[str, Any]], errors: list[str]
) -> dict[str, list[dict[str, Any]]]:
    """Give each import to exactly one feature: the strongest claim wins; no claim or a tie is an error."""
    owned: dict[str, list[dict[str, Any]]] = {name: [] for name in features}
    for item in imports:
        levels = {n: claims(f, item) for n, f in features.items()}
        top = max(levels.values(), default=-1)
        winners = [n for n, level in levels.items() if level == top and level >= 0]
        label = f"{module}: {item['key']}#{item['name']}" if item["key"] != item["name"] else f"{module}: {item['name']}"
        if not winners:
            errors.append(f"{label} belongs to no feature")
        elif len(winners) > 1:
            errors.append(f"{label} belongs to more than one feature: {', '.join(winners)}")
        else:
            owned[winners[0]].append(item)
    return owned


# ---- the check -----------------------------------------------------------------------------------------------------------------


def status_of(value: Status) -> tuple[str, str]:
    """('true' | 'false' | 'partial' | 'bad', note of a partial)."""
    if value is True:
        return "true", ""
    if value is False:
        return "false", ""
    if isinstance(value, list) and len(value) == 2 and value[0] == "partial" and isinstance(value[1], str):  # pyright: ignore[reportUnknownArgumentType]
        return "partial", value[1]  # pyright: ignore[reportUnknownVariableType]
    return "bad", ""


def note_of(feature: dict[str, Any], twin: str) -> str:
    note = feature.get("note", "")
    if isinstance(note, dict):
        return str(note.get(twin, ""))  # pyright: ignore[reportUnknownArgumentType, reportUnknownMemberType]
    return str(note)


def check_note(label: str, text: str, errors: list[str]) -> None:
    if not text.strip():
        errors.append(f"{label}: the note is empty")
        return
    found = SOURCE.findall(text)
    if not found:
        errors.append(f"{label}: the note names no file of the repository (docs/..., src/..., tests/...)")
    for path in found:
        if not (ROOT / path).exists():
            errors.append(f"{label}: the note names {path}, which is not in the repository")


def compute(doc: dict[str, Any], errors: list[str]) -> dict[str, Any]:
    """Run the checks of the structure and the functions; return the computed matrix (module -> feature -> twin -> counts)."""
    p2, p1 = load_signatures()
    cnames = c_functions()
    cwords = c_words()
    computed: dict[str, Any] = {}
    if doc.get("format") != FORMAT:
        errors.append(f"format must be {FORMAT!r}")
    if not isinstance(doc.get("version"), int):
        errors.append("version must be a whole number")
    if not isinstance(doc.get("$comment"), str) or not doc.get("$comment"):
        errors.append("$comment is missing")
    if doc.get("wasi") != "0.2.12" or p2[0]["module"].rsplit("@", 1)[-1] != doc.get("wasi"):
        errors.append("wasi must be the version of the tables of signatures (0.2.12)")
    modules: dict[str, Any] = doc.get("modules", {})
    seen_packages = {f["package"] for f in p2}
    for package in sorted(seen_packages):
        if package not in modules:
            errors.append(f"the module {package} of the tables of signatures is not in the file")
    for name in ("wasi_snapshot_preview1", "wasi_unstable"):
        if name not in modules:
            errors.append(f"the module {name} is not in the file")
    total_p2 = 0
    for mname, module in modules.items():
        computed[mname] = {}
        imports = module_imports(mname, p2, p1)
        if mname.startswith("wasi:") and mname not in seen_packages:
            errors.append(f"{mname}: not a package of the tables of signatures")
        if module.get("imports", 0) != len(imports):
            errors.append(f"{mname}: imports is {module.get('imports')}, the tables have {len(imports)}")
        if mname.startswith("wasi:"):
            total_p2 += len(imports)
        for twin in TWINS:
            files = module.get("files", {}).get(twin)
            for rel in [files] if isinstance(files, str) else files or []:
                base = C_ROOT if twin == "c" else PACKAGE
                if not (base / rel).is_file():
                    errors.append(f"{mname}: the {twin} file {rel} is not there")
            if not files:
                errors.append(f"{mname}: no file for {twin}")
        features: dict[str, Any] = module.get("features", {})
        owned = assign(mname, features, imports, errors)
        for fname, feature in features.items():
            label = f"{mname}/{fname}"
            if not re.fullmatch(r"[a-z][A-Za-z0-9]*", fname):
                errors.append(f"{label}: the name is not camelCase")
            items = owned[fname]
            if feature.get("imports", 0) != len(items):
                errors.append(f"{label}: imports is {feature.get('imports', 0)}, the claim has {len(items)}")
            symbols: dict[str, list[str]] = feature.get("symbols", {})
            row: dict[str, Any] = {"imports": len(items)}
            for twin in TWINS:
                if twin not in feature:
                    errors.append(f"{label}: no status for {twin}")
                    continue
                if twin == "c":
                    present = [i["name"] for i in items if i["c"] in cnames]
                    absent = [i["name"] for i in items if i["c"] not in cnames]
                    sym_absent = [s for s in symbols.get("c", []) if s not in cwords]
                    sym_all = symbols.get("c", [])
                else:
                    present = [i["name"] for i in items if python_function(*i["python"])]
                    absent = [i["name"] for i in items if not python_function(*i["python"])]
                    sym_absent = [s for s in symbols.get("python", []) if not python_symbol(s)]
                    sym_all = symbols.get("python", [])
                absent = absent + sym_absent
                total = len(items) + len(sym_all)
                row[twin] = {"present": len(present) + len(sym_all) - len(sym_absent), "of": total, "missing": absent}
                kind, note = status_of(feature[twin])
                where = f"{label} ({twin})"
                text = note or note_of(feature, twin)
                if kind == "bad":
                    errors.append(f"{where}: the status must be true, false or [\"partial\", note], not {feature[twin]!r}")
                    continue
                if total == 0:
                    if kind == "true":
                        errors.append(f"{where}: true, but there is nothing to check (no import, no symbol); say partial or false")
                elif kind == "true" and absent:
                    errors.append(f"{where}: true, but these are not in the {twin} twin: {', '.join(absent)}")
                elif kind == "false" and total and len(absent) < total:
                    errors.append(f"{where}: false, but these are there: {total - len(absent)} of {total}; say partial")
                elif kind == "partial" and total and len(absent) == total:
                    errors.append(f"{where}: partial, but nothing of it is in the {twin} twin; say false")
                elif kind == "partial" and absent and not MISSING_WORD.search(note):
                    errors.append(f"{where}: partial with missing functions ({', '.join(absent)}); the note must say 'missing'")
                if kind in ("partial", "false"):
                    check_note(where, text, errors)
                elif text:
                    for path in SOURCE.findall(text):
                        if not (ROOT / path).exists():
                            errors.append(f"{where}: the note names {path}, which is not in the repository")
            computed[mname][fname] = row
    if total_p2 != len(p2):
        errors.append(f"the modules of 0.2 have {total_p2} imports, the tables have {len(p2)}")
    unlisted = unlisted_functions(p2, cnames)
    if unlisted:
        errors.append(f"functions of the twins that no table lists: {', '.join(unlisted)}")
    return computed


def unlisted_functions(p2: list[dict[str, Any]], cnames: set[str]) -> list[str]:
    """Functions of 0.2 in a twin that the table does not list (the check of test_known_limits, for both twins)."""
    listed_c = {f["c_name"] for f in p2 if f["c_name"]}
    out = sorted(n for n in cnames if n.startswith("fusedwasi_wasi_") and n not in listed_c)
    listed_py = {(f["python_module"], f["python_name"]) for f in p2 if f["python_module"]}
    for package in sorted({f["package"] for f in p2}):
        modname = f"fusedwasi.p2.wasi_p2_{package.split(':')[1]}"
        mod = importlib.import_module(modname)
        for k, v in vars(mod).items():
            if k.startswith("wasi_") and inspect.isfunction(v) and v.__module__ == modname and (modname, k) not in listed_py:
                out.append(f"{modname}.{k}")
    return out


# ---- output --------------------------------------------------------------------------------------------------------------------


def show(value: Status) -> str:
    kind, _ = status_of(value)
    return {"true": "yes", "false": "no", "partial": "partial", "bad": "?"}[kind]


def table(doc: dict[str, Any]) -> str:
    rows = [("module", "feature", "imports", "c", "python")]
    for mname, module in doc.get("modules", {}).items():
        for fname, feature in module.get("features", {}).items():
            rows.append((mname, fname, str(feature.get("imports", 0)), show(feature.get("c")), show(feature.get("python"))))
    widths = [max(len(r[i]) for r in rows) for i in range(5)]
    return "\n".join("  ".join(c.ljust(w) for c, w in zip(r, widths)).rstrip() for r in rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0] if __doc__ else None)
    parser.add_argument("--doc", type=Path, default=DEFAULT_DOC, help="the file to check (default docs/features.json)")
    parser.add_argument("--json", action="store_true", help="print the computed matrix as JSON")
    parser.add_argument("--list", action="store_true", help="print the modules and the features as a table")
    args = parser.parse_args(argv)
    sys.path.insert(0, str(ROOT / "src"))
    try:
        doc = json.loads(args.doc.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"{args.doc}: {e}", file=sys.stderr)
        return 1
    if not isinstance(doc, dict):
        print(f"{args.doc}: the top level must be an object", file=sys.stderr)
        return 1
    errors: list[str] = []
    computed = compute(doc, errors)  # pyright: ignore[reportUnknownArgumentType]
    if args.list:
        print(table(doc))  # pyright: ignore[reportUnknownArgumentType]
    if args.json:
        print(json.dumps(computed, indent=2))
    for e in errors:
        print(f"MISMATCH {e}", file=sys.stderr)
    if not errors and not args.json and not args.list:
        n = sum(len(m.get("features", {})) for m in doc["modules"].values())
        print(f"ok: {len(doc['modules'])} modules, {n} features, both twins")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
