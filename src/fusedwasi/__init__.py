"""fusedwasi: a host of the WASI Component Model for core modules fused from components (alpha).

The top level holds only what belongs to no family: `get_include()` is the directory to put on the include path of a C compiler (`-I$(python -m fusedwasi)`, then
`#include <fusedwasi/p2/wasi_p2_cli.h>` works), `get_signatures_path()` is the directory with the tables of the signatures of every WASI import (see `fusedwasi.signatures`, whose
`find_import` gives the function for an import) and `__version__`.

The Python twin has one package for each family, with the same names in each: `fusedwasi.p1` (WASI 0.1: `State`, `Guest`, `Exit`, ...) and `fusedwasi.p2` (WASI 0.2: `State`, `Guest`,
`Exit`, `Trap`, ...). Flat functions on a `Guest` over a `State`; the functions are in `fusedwasi.p1.wasi_p1` and in one file for each package of WASI 0.2 in `fusedwasi.p2`
(`wasi_p2_cli`, `wasi_p2_clocks`, `wasi_p2_filesystem`, `wasi_p2_http`, `wasi_p2_io`, `wasi_p2_random`, `wasi_p2_sockets`).
"""

from importlib import metadata
from pathlib import Path

__all__ = ["__version__", "get_include", "get_signatures_path"]

try:
    __version__ = metadata.version("fusedwasi")
except metadata.PackageNotFoundError:  # run from a checkout that is not installed
    __version__ = "0.0.0"


def get_include() -> str:
    """The directory with the C headers (`fusedwasi.h`, `fusedwasi/p2/*.h`)."""
    return str(Path(__file__).resolve().parent / "include")


def get_signatures_path() -> str:
    """The directory with the tables of the signatures of the WASI imports (`wasi_p1.json`, `wasi_p2.json`, `*_protocols.pyi`)."""
    return str(Path(__file__).resolve().parent / "signatures")
