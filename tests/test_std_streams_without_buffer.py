"""The default standard streams of the host of WASI 0.2 when `sys.stdin`/`sys.stdout` have no `.buffer` (the console of Pythonista, the shell StaSh, `io.StringIO`)."""

import io
import sys

import pytest

from fusedwasi import p2
from fusedwasi.p2 import wasi_p2_cli as cli
from fusedwasi.p2.wasi_p2_io import StreamClosed


class OnlyWrite:
    """What an app puts in place of sys.stdout: a `write` and nothing else."""

    def __init__(self) -> None:
        self.text = ""

    def write(self, text: str) -> int:
        self.text += text
        return len(text)


def test_a_text_stdin_is_read_as_utf_8_and_its_end_is_the_end_of_the_stream(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(sys, "stdin", io.StringIO("ab€"))
    stdin = cli.std(p2.State(), "stdin")
    chunks = [stdin.read(2), stdin.read(2), stdin.read(2)]  # type: ignore[attr-defined]
    assert chunks == [b"ab", b"\xe2\x82", b"\xac"]  # never more than asked for
    with pytest.raises(StreamClosed):
        stdin.read(2)  # type: ignore[attr-defined]


def test_a_stdin_that_is_none_is_a_stream_that_is_over(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(sys, "stdin", None)
    with pytest.raises(StreamClosed):
        cli.std(p2.State(), "stdin").read(4)  # type: ignore[attr-defined]


def test_a_stdin_with_only_readline_is_read_by_lines(monkeypatch: pytest.MonkeyPatch) -> None:
    class Lines:
        def __init__(self) -> None:
            self.lines = ["x\n", "yz\n"]

        def readline(self) -> str:
            return self.lines.pop(0) if self.lines else ""

    monkeypatch.setattr(sys, "stdin", Lines())
    stdin = cli.std(p2.State(), "stdin")
    assert [stdin.read(9), stdin.read(9)] == [b"x\n", b"yz\n"]  # type: ignore[attr-defined]
    with pytest.raises(StreamClosed):
        stdin.read(9)  # type: ignore[attr-defined]


def test_a_stdout_with_no_flush_and_no_buffer_gets_the_text(monkeypatch: pytest.MonkeyPatch) -> None:
    out = OnlyWrite()
    monkeypatch.setattr(sys, "stdout", out)
    stdout = cli.std(p2.State(), "stdout")
    stdout.write("hello €".encode())  # type: ignore[attr-defined]
    stdout.flush()  # type: ignore[attr-defined]
    assert out.text == "hello €"
