"""The README says what to import; these tests keep it true.

Every `import` line of a `python` block of the README is run (the lines that name the package: `from fusedwasi ... import ...`), and the claim about the top level is checked: `State`,
`Guest` and `Exit` are one class each for WASI 0.1 and 0.2 and are re-exported at the top, and what belongs to one family is not.
"""

import importlib
import importlib.util
import re
from pathlib import Path

import fusedwasi
from fusedwasi import p1, p2

README = (Path(__file__).parent.parent / "README.md").read_text(encoding="utf-8")


def python_blocks() -> list[str]:
    return re.findall(r"```python\n(.*?)```", README, flags=re.DOTALL)


def test_the_imports_of_the_python_examples_of_the_readme_work() -> None:
    lines = [
        line
        for block in python_blocks()
        for line in block.splitlines()
        if re.match(r"(from fusedwasi[\w.]* import |import fusedwasi)", line)
    ]
    assert lines, "the README has no python example that imports fusedwasi"
    for line in lines:
        code = line.split("#", 1)[0].strip()
        match = re.match(r"from (\S+) import (.+?)(?: as \w+)?$", code)
        if match:
            module = importlib.import_module(match.group(1))
            for name in (n.strip() for n in match.group(2).split(",")):
                assert hasattr(module, name.split(" as ")[0]) or importlib.util.find_spec(
                    f"{match.group(1)}.{name.split(' as ')[0]}"
                ), line
        else:
            importlib.import_module(code.removeprefix("import ").split(" as ")[0])


def test_the_readme_shows_the_top_level_import_for_the_shared_classes() -> None:
    assert "from fusedwasi import Guest, State" in README
    assert "fusedwasi.State is fusedwasi.p1.State is fusedwasi.p2.State" in README


def test_the_shared_classes_are_one_class_and_are_at_the_top() -> None:
    for name in ("State", "Guest", "Exit"):
        assert getattr(fusedwasi, name) is getattr(p1, name) is getattr(p2, name), name
        assert name in fusedwasi.__all__


def test_what_belongs_to_one_family_is_not_at_the_top() -> None:
    """The README names these as staying in their package."""
    assert hasattr(p2, "Trap") and not hasattr(fusedwasi, "Trap")
    assert hasattr(p1, "WasiError") and not hasattr(fusedwasi, "WasiError")
    assert hasattr(p1, "PreopenError") and not hasattr(fusedwasi, "PreopenError")
