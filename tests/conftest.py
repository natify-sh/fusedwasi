"""`FUSEDWASI_ALLOWED_SKIPS` (a regular expression) makes a run fail when a test is skipped for a reason that does not match it.

The job that builds the C drivers with MSVC uses it: there a skip is allowed only for what Windows does not have (pipe(), a FIFO, select.poll), and a skip for "no C compiler"
would be a run that tested nothing.
"""

import os
import re

import pytest

_unexpected: list[str] = []


def pytest_runtest_logreport(report: pytest.TestReport) -> None:
    allowed = os.environ.get("FUSEDWASI_ALLOWED_SKIPS")
    if not allowed or not report.skipped:
        return
    longrepr = report.longrepr
    reason = str(longrepr[2]) if isinstance(longrepr, tuple) else str(longrepr)  # "Skipped: why"
    if not re.search(allowed, reason):
        _unexpected.append(f"{report.nodeid}: {reason}")


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    if _unexpected:
        print("\nskipped for a reason that FUSEDWASI_ALLOWED_SKIPS does not allow:\n  " + "\n  ".join(_unexpected))
        session.exitstatus = 1
