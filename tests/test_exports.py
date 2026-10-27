"""What the packages of the Python twin export: each family its own names with `__all__`, the top level only what belongs to no family."""

import importlib
import inspect
import re
from types import ModuleType

import pytest

import fusedwasi
from fusedwasi import p1, p2

FAMILY_CLASSES = ("State", "Guest", "Exit")


@pytest.mark.parametrize("package", [p1, p2], ids=["p1", "p2"])
def test_all_is_complete_importable_and_without_duplicates(package: ModuleType) -> None:
    names: list[str] = package.__all__
    assert names and len(names) == len(set(names))
    assert all(hasattr(package, n) for n in names)
    namespace: dict[str, object] = {}
    exec(f"from {package.__name__} import *", namespace)  # noqa: S102
    assert set(names) <= set(namespace)
    # everything public that the package holds is in __all__ (a submodule is not a name of the family)
    public = {n for n in vars(package) if not n.startswith("_") and not inspect.ismodule(vars(package)[n])}
    assert public == set(names)


def test_the_top_level_has_no_name_of_a_family() -> None:
    assert sorted(fusedwasi.__all__) == ["__version__", "get_include", "get_signatures_path"]
    for name in ("State", "Guest", "Exit", "Trap"):
        assert not hasattr(fusedwasi, name)
    assert isinstance(fusedwasi.__version__, str)


def test_a_name_in_both_families_is_two_classes() -> None:
    both = set(p1.__all__) & set(p2.__all__)
    assert both == set(FAMILY_CLASSES)  # the only names the families share, and they are not one thing
    for name in both:
        assert getattr(p1, name) is not getattr(p2, name)
    assert p1.State.__module__ == "fusedwasi.p1.wasi_p1_core" and p2.State.__module__ == "fusedwasi.p2.wasi_p2_core"


def test_the_names_a_user_needs_are_there() -> None:
    assert {
        "State",
        "Guest",
        "Exit",
        "Trap",
        "Pollable",
        "flat_view",
        "StreamClosed",
        "StreamFailed",
        "InputStream",
        "OutputStream",
        "USE_TCP_BIND",
        "TERMINAL_STDIN",
    } <= set(p2.__all__)
    assert {
        "State",
        "Guest",
        "Exit",
        "WasiError",
        "PreopenError",
        "errno_of",
        "E_SUCCESS",
        "E_NOTSUP",
        "FT_DIR",
        "O_CREAT",
        "R_ALL",
    } <= set(p1.__all__)
    assert not any(
        re.match(r"^K_", n) for n in p1.__all__
    )  # the kinds of the entries of the table of descriptors are private


def test_the_families_work_together() -> None:
    out = p2.BufferOutput()
    state = p1.State(core=p2.State(args=["a"], stdout=out))
    guest = p1.Guest(state, bytearray(64))
    from fusedwasi.p1 import wasi_p1

    assert wasi_p1.args_sizes_get(guest, 0, 4) == p1.E_SUCCESS
    assert importlib.import_module("fusedwasi.p2.wasi_p2_cli")
