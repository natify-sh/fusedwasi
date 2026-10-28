"""The package carries the C headers, so that another project can depend on it and compile against them."""

import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

import fusedwasi


def test_the_headers_are_in_the_package() -> None:
    include = Path(fusedwasi.get_include())
    assert (include / "fusedwasi.h").is_file()
    for name in ("cli", "clocks", "core", "filesystem", "http", "io", "net", "random", "sockets"):
        assert (include / "fusedwasi" / "p2" / f"wasi_p2_{name}.h").is_file(), name


def test_python_m_fusedwasi_prints_the_include_directory() -> None:
    out = subprocess.run([sys.executable, "-m", "fusedwasi"], capture_output=True, text=True, check=True).stdout
    assert out.strip() == fusedwasi.get_include()


ROOT = Path(__file__).parent.parent


@pytest.mark.skipif(shutil.which("cmake") is None or shutil.which("cc") is None, reason="no cmake or no C compiler")
def test_the_cmake_install_works_and_a_project_finds_the_package(tmp_path: Path) -> None:
    """`cmake --install` of a source tree, then `find_package(fusedwasi)` and the target `fusedwasi::headers` in another project."""
    prefix = tmp_path / "prefix"
    subprocess.run(
        ["cmake", "-S", str(ROOT), "-B", str(tmp_path / "build"), f"-DCMAKE_INSTALL_PREFIX={prefix}"],
        check=True,
        capture_output=True,
    )
    subprocess.run(["cmake", "--install", str(tmp_path / "build")], check=True, capture_output=True)
    assert (prefix / "include" / "fusedwasi" / "p2" / "wasi_p2_core.h").is_file()
    assert (prefix / "include" / "fusedwasi" / "version.h").is_file()
    assert (prefix / "share" / "fusedwasi" / "signatures" / "wasi_p2.json").is_file()
    assert not list((prefix / "share").rglob("__pycache__"))
    user = tmp_path / "user"
    user.mkdir()
    (user / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.13)\nproject(user LANGUAGES C)\nfind_package(fusedwasi REQUIRED)\n"
        "add_executable(user main.c)\ntarget_link_libraries(user PRIVATE fusedwasi::headers)\n",
        encoding="utf-8",
    )
    (user / "main.c").write_text(
        "#include <fusedwasi/p2/wasi_p2_core.h>\nint main(void) { fusedwasi_p2_state s; fusedwasi_p2_init(&s); int c = fusedwasi_p2_state_closed(&s); fusedwasi_p2_free(&s); return c; }\n",
        encoding="utf-8",
    )
    subprocess.run(
        ["cmake", "-S", str(user), "-B", str(user / "b"), f"-DCMAKE_PREFIX_PATH={prefix}"],
        check=True,
        capture_output=True,
    )
    subprocess.run(["cmake", "--build", str(user / "b")], check=True, capture_output=True)
    # a multi-configuration generator (Visual Studio) puts the program in a directory of the configuration
    exe = next((user / "b").rglob("user.exe" if sys.platform == "win32" else "user"))
    assert subprocess.run([str(exe)], check=False).returncode == 0


@pytest.mark.skipif(shutil.which("uv") is None, reason="no uv")
def test_a_wheel_built_without_git_has_the_headers_and_the_tables(tmp_path: Path) -> None:
    """A checkout built by another tool (uv of a git dependency on Windows) or a plain copy has no git for setuptools-scm to list the files with: the wheel names them itself."""
    copy = tmp_path / "copy"
    shutil.copytree(
        ROOT,
        copy,
        ignore=shutil.ignore_patterns(
            ".git", ".venv", "build", "dist", "__pycache__", "*.egg-info", ".pytest_cache", ".ruff_cache"
        ),
    )
    env = {**os.environ, "SETUPTOOLS_SCM_PRETEND_VERSION": "0.0.1"}
    subprocess.run(
        ["uv", "build", "--wheel", "-o", str(tmp_path / "dist"), str(copy)], check=True, capture_output=True, env=env
    )
    names = zipfile.ZipFile(next((tmp_path / "dist").glob("*.whl"))).namelist()
    for needed in (
        "fusedwasi/signatures/wasi_p1.json",
        "fusedwasi/signatures/wasi_p2.json",
        "fusedwasi/public_api.json",
        "fusedwasi/py.typed",
        "fusedwasi/include/fusedwasi.h",
        "fusedwasi/include/fusedwasi/p1/wasi_p1_core.h",
        "fusedwasi/include/fusedwasi/p2/wasi_p2_sockets.h",
    ):
        assert needed in names, needed
