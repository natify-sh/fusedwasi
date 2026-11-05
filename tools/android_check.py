"""What fusedwasi does inside an Android app process (CI job `android` of test.yml, docs/TESTING note in docs/host_scope.md).

Run by CPython's testbed app (`android.py test ... -- -m tools.android_check`) with the working directory holding `tests/` and `tools/`, and `fusedwasi` and `pytest` in the site-packages of
the app. It prints the platform, then runs the tests of the Python twin that need nothing an app does not have. An app has **no C compiler, no cmake and no interpreter of its own to start**
(`sys.executable` is the app), so the tests that build the C drivers skip with "no C compiler" and the tests that start `python` are not in the list: the C twin is not run here.
Exit 0 when the tests pass (a skip for "no C compiler" is expected; a skip for another reason is printed and fails the run)."""

import faulthandler
import os
import platform
import sys

import pytest

# the Python twin on the real system calls of Android (files, sockets, pipes, clocks, random), and the pure checks
FILES = [
    "tests/test_wasi_p2_clocks.py",
    "tests/test_wasi_p2_io.py",
    "tests/test_wasi_p2_random.py",
    "tests/test_wasi_p2_cli.py",
    "tests/test_wasi_p2_filesystem.py",
    "tests/test_wasi_p2_sockets.py",
    "tests/test_wasi_p2_http.py",
    "tests/test_wasi_p2_address_checks.py",
    "tests/test_wasi_p1_errno.py",
    "tests/test_wasi_p1_over_p2.py",
    "tests/test_closed_state.py",
    "tests/test_memory_forms.py",
    "tests/test_memory_remote.py",
    "tests/test_std_streams_without_buffer.py",
]


def main() -> int:
    faulthandler.enable()
    print("platform:", sys.version.split()[0], sys.platform, platform.machine(), platform.release(), flush=True)
    for path in ("/proc/self/attr/current", "/sys/fs/selinux/enforce"):
        try:
            with open(path) as f:
                print(path, f.read().strip())
        except OSError as e:
            print(path, e)
    # a skip is read from the report: the allowed reasons are what an app or this system lacks (a compiler: the C twin is run by the other jobs; the ctypes route of Windows; an IPv6 loopback)
    os.environ.setdefault("FUSEDWASI_ALLOWED_SKIPS", "no C compiler|no cmake|the ctypes route is the Windows branch|no IPv6 loopback")
    rc = pytest.main(["-q", "-rs", "-p", "no:cacheprovider", *FILES])
    print("pytest exit code:", int(rc), flush=True)
    return int(rc)


if __name__ == "__main__":
    sys.exit(main())
