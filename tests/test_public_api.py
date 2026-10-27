"""The public names, and the mark that keeps the private ones apart (docs/design_model.md, section 5; the data is src/fusedwasi/public_api.json).

The check itself is `tools/public_api.py`; these tests run it on the tree, and run it on trees that break the rule to show that it notices:

* every `fusedwasi_*` / `FUSEDWASI_*` name of the headers is in the list of public names or carries the mark (a double underscore); every public name of the list exists;
* the fields of the public structs are listed, or end with the mark;
* the three neutral users (C over wasm3 with its generated glue, Python over wasmtime-py and over pywasm3) use public names only;
* in Python, a module has no name without an underscore that is not in the list, and `__all__` of the packages is the list.
"""

import copy
import importlib.util
import json
import shutil
import sys
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

REPO = Path(__file__).resolve().parent.parent
TOOL = REPO / "tools" / "public_api.py"


def _load_tool() -> ModuleType:
    spec = importlib.util.spec_from_file_location("public_api", TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules["public_api"] = module
    spec.loader.exec_module(module)
    return module


tool: Any = _load_tool()


@pytest.fixture
def api() -> dict[str, Any]:
    return tool.load()


def test_the_names_of_the_headers_are_public_or_marked(api: dict[str, Any]) -> None:
    assert tool.check_c(api) == []


def test_the_neutral_c_user_and_its_glue_use_public_names_only(api: dict[str, Any]) -> None:
    assert tool.check_neutral_c(api) == []
    sources = tool.neutral_c_sources()
    assert any("generated glue" in where for where in sources)
    assert "fusedwasi_p2_bind_memory" in "".join(sources.values())  # the check looked at the user, not at nothing


def test_the_python_modules_have_no_unlisted_name(api: dict[str, Any]) -> None:
    assert tool.check_python(api) == []


def test_the_neutral_python_users_use_public_names_only(api: dict[str, Any]) -> None:
    assert tool.check_neutral_python(api) == []


def test_the_document_is_the_one_the_data_gives() -> None:
    assert tool.check_doc(tool.load()) == [], "run `python tools/public_api.py doc`"


def test_the_list_of_c_has_the_tables_of_signatures_in_it(api: dict[str, Any]) -> None:
    names = tool.CNames(api)
    assert len(names.signatures) == 247  # 46 of WASI 0.1 and 201 of WASI 0.2
    assert all(names.is_public(n) for n in names.signatures)
    assert not any(
        names.is_public(n)
        for n in (
            "fusedwasi_p2__table_get",
            "fusedwasi_p2_table_get",
            "FUSEDWASI_P2__KIND_P1_FDS",
            "FUSEDWASI_P1__K_FILE",
        )
    )


def test_a_public_name_has_no_mark_and_the_mark_is_after_the_prefix(api: dict[str, Any]) -> None:
    names = tool.CNames(api)
    assert names.marked("fusedwasi_p2_table_get") == "fusedwasi_p2__table_get"
    assert names.marked("FUSEDWASI_P1_K_FILE") == "FUSEDWASI_P1__K_FILE"
    assert names.marked("FUSEDWASI_H") == "FUSEDWASI__H"
    assert names.unmarked("fusedwasi_p2__table_get") == "fusedwasi_p2_table_get"
    for group in api["c"]["groups"]:
        assert not any("__" in name for name in group["names"])


# ---- the check notices a tree that breaks the rule ------------------------------------------------------------------------------


@pytest.fixture
def copied_headers(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    include = tmp_path / "include"
    shutil.copytree(tool.INCLUDE, include)
    monkeypatch.setattr(tool, "INCLUDE", include)
    return include


def test_a_new_unmarked_name_fails_until_it_is_listed(api: dict[str, Any], copied_headers: Path) -> None:
    core = copied_headers / "fusedwasi" / "p2" / "wasi_p2_core.h"
    core.write_text(
        core.read_text(encoding="utf-8") + "\nFUSEDWASI_P2_FN int fusedwasi_p2_brand_new(void) { return 1; }\n",
        encoding="utf-8",
    )
    problems = tool.check_c(api)
    assert any("fusedwasi_p2_brand_new" in p and "neither listed" in p for p in problems)
    # listing it makes it public
    listed = copy.deepcopy(api)
    listed["c"]["groups"][0]["names"]["fusedwasi_p2_brand_new"] = "a test"
    assert tool.check_c(listed) == []
    # marking it makes it private, and marking twice changes nothing more
    names = tool.CNames(api)
    renames = tool.mark_c(names, sorted(copied_headers.rglob("*.h")), tool.universe())
    assert renames == {"fusedwasi_p2_brand_new": "fusedwasi_p2__brand_new"}
    assert tool.check_c(api) == []
    assert tool.mark_c(names, sorted(copied_headers.rglob("*.h")), tool.universe()) == {}


def test_a_listed_name_that_does_not_exist_fails(api: dict[str, Any]) -> None:
    broken = copy.deepcopy(api)
    broken["c"]["groups"][0]["names"]["fusedwasi_p2_no_such_thing"] = "x"
    broken["c"]["groups"][0]["names"]["FUSEDWASI_P2_NO_SUCH_*"] = "x"
    problems = tool.check_c(broken)
    assert any("`fusedwasi_p2_no_such_thing` is not in the headers" in p for p in problems)
    assert any("`FUSEDWASI_P2_NO_SUCH_*` is not in the headers" in p for p in problems)


def test_a_new_field_of_a_public_struct_fails_until_it_is_listed_or_marked(
    api: dict[str, Any], copied_headers: Path
) -> None:
    core = copied_headers / "fusedwasi" / "p2" / "wasi_p2_core.h"
    text = core.read_text(encoding="utf-8")
    assert "    void *user; /* the pointer of whoever made the guest" in text
    core.write_text(
        text.replace(
            "    void *user; /* the pointer of whoever made the guest",
            "    int brand_new;\n    void *user; /* the pointer of whoever made the guest",
        ),
        encoding="utf-8",
    )
    assert any("fusedwasi_p2_guest.brand_new" in p for p in tool.check_c(api))
    private = copy.deepcopy(api)
    private["c"]["fields"]["fusedwasi_p2_guest"]["private"].append("brand_new")
    names = tool.CNames(private)
    tool.mark_c(names, sorted(copied_headers.rglob("*.h")), tool.universe())
    assert "int brand_new__;" in core.read_text(encoding="utf-8")
    assert tool.check_c(private) == []


def test_a_neutral_user_of_a_private_name_fails(api: dict[str, Any], monkeypatch: pytest.MonkeyPatch) -> None:
    sources = {
        "user.c": "int main(void) { fusedwasi_p2_state s; fusedwasi_p2_init(&s); fusedwasi_p2__table_get(0, 0, 0); s.slots__ = 0; g.memories__ = 1; return 0; }",
    }
    monkeypatch.setattr(tool, "neutral_c_sources", lambda: sources)
    problems = tool.check_neutral_c(api)
    assert any("fusedwasi_p2__table_get" in p for p in problems)
    assert any("`slots__` carries the mark" in p or "private field `slots__`" in p for p in problems)
    assert any("private field `memories`" in p for p in problems)
    assert not any("fusedwasi_p2_init`" in p for p in problems)


def test_a_python_name_without_an_underscore_fails_until_it_is_listed(
    api: dict[str, Any], monkeypatch: pytest.MonkeyPatch
) -> None:
    broken = copy.deepcopy(api)
    del broken["python"]["modules"]["fusedwasi.p2.wasi_p2_core"]["names"]["flat_view"]
    problems = tool.check_python(broken)
    assert any("fusedwasi.p2.wasi_p2_core: `flat_view`" in p for p in problems)
    missing = copy.deepcopy(api)
    missing["python"]["modules"]["fusedwasi.p2.wasi_p2_core"]["names"]["no_such_function"] = "x"
    assert any("`no_such_function` does not exist" in p for p in tool.check_python(missing))


def test_a_member_of_state_without_an_underscore_fails_until_it_is_listed(api: dict[str, Any]) -> None:
    broken = copy.deepcopy(api)
    del broken["python"]["members"]["fusedwasi.p2.State"]["public"]["clock"]
    assert any("fusedwasi.p2.State.clock" in p for p in tool.check_members(broken))


def test_the_data_is_valid_json_with_a_purpose_for_each_name() -> None:
    data = json.loads(tool.API.read_text(encoding="utf-8"))
    for group in data["c"]["groups"]:
        assert group["stability"] in ("stable", "unstable")
        assert all(isinstance(p, str) and p for p in group["names"].values()), group["id"]
    for module in data["python"]["modules"].values():
        assert module["purpose"]
        assert all(isinstance(p, str) and p for p in module["names"].values())


def test_every_header_says_which_names_are_public(api: dict[str, Any]) -> None:
    assert tool.check_headers(api) == [], "run `python tools/public_api.py headers`"
    core = (tool.INCLUDE / "fusedwasi" / "p2" / "wasi_p2_core.h").read_text(encoding="utf-8")
    top = core[: core.index("*/")]
    assert "Host API" in top and "fusedwasi_p2_bind_memory" in top
