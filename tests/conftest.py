"""`FUSEDWASI_ALLOWED_SKIPS` (a regular expression) makes a run fail when a test is skipped for a reason that does not match it.

The job that builds the C drivers with MSVC uses it: there a skip is allowed only for what Windows does not have (pipe(), a FIFO, select.poll), and a skip for "no C compiler"
would be a run that tested nothing.
"""

import errno
import os
import re
import sys
import tempfile

import pytest

_unexpected: list[str] = []

# PyPy before 8.0 (7.3.x, the last one with Python 3.11 only) refuses `os.link(..., src_dir_fd=, dst_dir_fd=, follow_symlinks=False)` with EINVAL; PyPy 8 (Python 3.12) does not (docs/specs/S0009.md). The tests that
# make a hard link by that call are an expected failure there, with this message, and not a red run: the advice is PyPy 8.0 or newer.
_PYPY_LINK_BUG = (
    "PyPy before 8.0 (7.3.x) refuses os.link(follow_symlinks=False) with EINVAL, so a hard link cannot be made there; "
    "use PyPy 8.0 or newer (it has Python 3.11 and 3.12), where it works (docs/specs/S0009.md)"
)
_NEEDS_HARD_LINK = {
    "test_c_and_python_agree[files]",
    "test_what_the_second_stage_gives",
    "test_c_and_python_agree[main]",
    "test_values",
}


def _pypy_refuses_the_link_flag() -> bool:
    if sys.implementation.name != "pypy" or not hasattr(os, "link"):
        return False
    with tempfile.TemporaryDirectory() as directory:
        fd = os.open(directory, os.O_RDONLY)
        try:
            with open(os.path.join(directory, "a"), "w"):
                pass
            os.link("a", "b", src_dir_fd=fd, dst_dir_fd=fd, follow_symlinks=False)
        except OSError as error:
            return error.errno == errno.EINVAL
        finally:
            os.close(fd)
    return False


def pytest_report_header(config: pytest.Config) -> str | None:
    return f"WARNING: {_PYPY_LINK_BUG}" if _pypy_refuses_the_link_flag() else None


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    if not _pypy_refuses_the_link_flag():
        return
    marker = pytest.mark.xfail(reason=_PYPY_LINK_BUG, strict=False)
    for item in items:
        if item.name in _NEEDS_HARD_LINK and item.nodeid.split("::")[0] in {
            "tests/test_wasi_p1_core.py",
            "tests/test_wasi_p2_filesystem.py",
        }:
            item.add_marker(marker)


def pytest_runtest_logreport(report: pytest.TestReport) -> None:
    allowed = os.environ.get("FUSEDWASI_ALLOWED_SKIPS")
    if not allowed or not report.skipped or hasattr(report, "wasxfail"):  # an expected failure (`xfail`) is not a skip
        return
    longrepr = report.longrepr
    reason = str(longrepr[2]) if isinstance(longrepr, tuple) else str(longrepr)  # "Skipped: why"
    if not re.search(allowed, reason):
        _unexpected.append(f"{report.nodeid}: {reason}")


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    if _unexpected:
        print("\nskipped for a reason that FUSEDWASI_ALLOWED_SKIPS does not allow:\n  " + "\n  ".join(_unexpected))
        session.exitstatus = 1
