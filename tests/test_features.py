"""The feature matrix `docs/features.json` against both twins, run through `tools/check_features.py` (a subprocess, as a user would), and the rejection of files broken on purpose."""

import copy
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).parent.parent
SCRIPT = ROOT / "tools" / "check_features.py"
DOC = ROOT / "docs" / "features.json"


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args], check=False, capture_output=True, text=True, cwd=ROOT, timeout=120
    )


def load() -> dict[str, Any]:
    return json.loads(DOC.read_text(encoding="utf-8"))


def rejected(tmp_path: Path, doc: dict[str, Any]) -> str:
    path = tmp_path / "broken.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    result = run("--doc", str(path))
    assert result.returncode == 1, result.stdout + result.stderr
    return result.stderr


def test_the_document_is_true_for_both_twins() -> None:
    result = run()
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("ok: ")


def test_the_json_output_is_the_computed_matrix() -> None:
    result = run("--json")
    assert result.returncode == 0, result.stderr
    computed = json.loads(result.stdout)
    types = computed["wasi:filesystem"]["types"]
    assert types["imports"] == 31
    assert types["c"] == {"present": 31, "of": 31, "missing": []}
    assert types["python"] == {"present": 31, "of": 31, "missing": []}
    assert computed["wasi:clocks"]["timezone"]["c"]["present"] == 0


def test_the_list_prints_every_module_and_feature() -> None:
    result = run("--list")
    assert result.returncode == 0, result.stderr
    doc = load()
    for module, body in doc["modules"].items():
        for feature in body["features"]:
            assert any(module in line and feature in line for line in result.stdout.splitlines()), (module, feature)


def test_every_import_is_in_one_feature_and_the_count_is_the_tables() -> None:
    doc = load()
    p2 = sum(m["imports"] for k, m in doc["modules"].items() if k.startswith("wasi:"))
    tables = json.loads((ROOT / "src/fusedwasi/signatures/wasi_p2.json").read_text(encoding="utf-8"))
    assert p2 == tables["count"] == 204
    for module in doc["modules"].values():
        assert module["imports"] == sum(f.get("imports", 0) for f in module["features"].values())


def test_a_true_for_a_function_that_is_not_there_is_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:clocks"]["features"]["timezone"]["c"] = True
    err = rejected(tmp_path, doc)
    assert "wasi:clocks/timezone (c): true, but these are not in the c twin" in err
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:clocks"]["features"]["timezone"]["python"] = True
    assert "wasi:clocks/timezone (python): true, but" in rejected(tmp_path, doc)


def test_a_false_for_functions_that_are_there_is_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:random"]["features"]["random"]["python"] = False
    assert "wasi:random/random (python): false, but these are there" in rejected(tmp_path, doc)


def test_an_unknown_feature_and_an_unclaimed_import_are_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:random"]["features"]["madeUp"] = {
        "covers": ["nothing"],
        "imports": 1,
        "c": False,
        "python": False,
    }
    err = rejected(tmp_path, doc)
    assert "wasi:random/madeUp: imports is 1, the claim has 0" in err
    assert "the note is empty" in err  # a false with nothing to check needs a note that names a file
    doc = copy.deepcopy(load())
    del doc["modules"]["wasi:random"]["features"]["insecureSeed"]
    err = rejected(tmp_path, doc)
    assert "belongs to no feature" in err


def test_an_import_in_two_features_is_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:random"]["features"]["insecureSeed"]["covers"].append("insecure")
    assert "belongs to more than one feature" in rejected(tmp_path, doc)


def test_a_partial_without_a_note_or_a_source_is_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:sockets"]["features"]["ipv6"]["c"] = ["partial", ""]
    assert "the note is empty" in rejected(tmp_path, doc)
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:sockets"]["features"]["ipv6"]["c"] = ["partial", "it is limited"]
    assert "names no file of the repository" in rejected(tmp_path, doc)
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:sockets"]["features"]["ipv6"]["c"] = ["partial", "see docs/nothing_here.md"]
    assert "which is not in the repository" in rejected(tmp_path, doc)


def test_a_null_and_a_missing_twin_are_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:random"]["features"]["random"]["c"] = None
    assert "the status must be true, false or" in rejected(tmp_path, doc)
    doc = copy.deepcopy(load())
    del doc["modules"]["wasi:random"]["features"]["random"]["python"]
    assert "no status for python" in rejected(tmp_path, doc)


def test_a_wrong_count_a_missing_module_and_a_wrong_file_are_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:random"]["imports"] = 6
    assert "wasi:random: imports is 6" in rejected(tmp_path, doc)
    doc = copy.deepcopy(load())
    del doc["modules"]["wasi:io"]
    assert "the module wasi:io of the tables of signatures is not in the file" in rejected(tmp_path, doc)
    doc = copy.deepcopy(load())
    doc["modules"]["wasi:io"]["files"]["c"] = "p2/no_such_header.h"
    assert "the c file p2/no_such_header.h is not there" in rejected(tmp_path, doc)


def test_a_symbol_that_is_not_there_is_rejected(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["modules"]["host"]["features"]["closedState"]["python"] = True
    doc["modules"]["host"]["features"]["closedState"]["symbols"]["python"].append("fusedwasi.p2:State.no_such")
    assert "fusedwasi.p2:State.no_such" in rejected(tmp_path, doc)


def test_the_format_id_and_the_version_are_checked(tmp_path: Path) -> None:
    doc = copy.deepcopy(load())
    doc["format"] = "other"
    doc["version"] = "1"
    err = rejected(tmp_path, doc)
    assert "format must be" in err and "version must be a whole number" in err
