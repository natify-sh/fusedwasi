"""fusedwasi: a host of the WASI Component Model for core modules fused from components (alpha).

Python twin of the C headers, which this package carries: `get_include()` is the directory to put on the include path of a C compiler
(`-I$(python -m fusedwasi)`), and `#include <fusedwasi/p2/wasi_p2_cli.h>` works from there. Flat functions on a `Host`, one file for each
package of WASI 0.2 in `fusedwasi.p2` (`wasi_p2_cli`, `wasi_p2_clocks`, `wasi_p2_filesystem`, `wasi_p2_http`, `wasi_p2_io`, `wasi_p2_random`,
`wasi_p2_sockets`). `get_signatures_path()` is the directory with the tables of the signatures of every WASI import (see `fusedwasi.signatures`).
"""

from pathlib import Path

from .p2.wasi_p2_core import Exit, Host, Trap

__all__ = ["Exit", "Host", "Trap", "get_include", "get_signatures_path"]


def get_include() -> str:
    """The directory with the C headers (`fusedwasi.h`, `fusedwasi/p2/*.h`)."""
    return str(Path(__file__).resolve().parent / "include")


def get_signatures_path() -> str:
    """The directory with the tables of the signatures of the WASI imports (`wasi_p1.json`, `wasi_p2.json`, `*_protocols.pyi`)."""
    return str(Path(__file__).resolve().parent / "signatures")
