"""The package carries the C headers, so that another project can depend on it and compile against them."""

import subprocess
import sys
from pathlib import Path

import fusedwasi


def test_the_headers_are_in_the_package() -> None:
    include = Path(fusedwasi.get_include())
    assert (include / "fusedwasi.h").is_file()
    for name in ("cli", "clocks", "core", "filesystem", "http", "io", "net", "random", "sockets"):
        assert (include / "fusedwasi" / "p2" / f"wasi_p2_{name}.h").is_file(), name


def test_python_m_fusedwasi_prints_the_include_directory() -> None:
    out = subprocess.run([sys.executable, "-m", "fusedwasi"], capture_output=True, text=True, check=True).stdout
    assert out.strip() == fusedwasi.get_include()
