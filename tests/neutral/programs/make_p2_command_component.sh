#!/bin/sh
# Makes p2_command.component.wat from p2_command.wat: the same core module as a component, which Wasmtime can run with its own WASI.
# Needs wasm-tools (written with 1.262.0). The WIT it reads is the copy of the specification in tools/spec_sources, with the version 0.2.6 that the module imports and without
# the gates of later versions. Run from anywhere; the result is printed into this directory. Not run by the tests: the comparison with Wasmtime is the check that both are one program.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
spec="$here/../../../tools/spec_sources/wasi_p2"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/wit/deps"
for package in cli clocks filesystem io random sockets; do
  mkdir -p "$work/wit/deps/$package"
  cp "$spec/$package"/*.wit "$work/wit/deps/$package/"
done
rm -f "$work/wit/deps/cli/command.wit"
sed -i -E 's/@0\.2\.12/@0.2.6/g; s/@(since|unstable)\(version = 0\.2\.(1[0-9]|[7-9])[^)]*\)//; s/@unstable\(feature = [^)]*\)//' "$work"/wit/deps/*/*.wit
cat > "$work/wit/world.wit" <<'WIT'
package neutral:cmd;
world cmd {
  import wasi:cli/environment@0.2.6;
  import wasi:cli/stdout@0.2.6;
  import wasi:cli/exit@0.2.6;
  import wasi:clocks/monotonic-clock@0.2.6;
  import wasi:io/streams@0.2.6;
  export wasi:cli/run@0.2.6;
}
WIT
wasm-tools component embed "$work/wit" --world cmd "$here/p2_command.wat" -o "$work/embedded.wasm"
wasm-tools component new "$work/embedded.wasm" -o "$work/command.wasm"
{
  echo ";; p2_command.wat as a component, made by make_p2_command_component.sh (wasm-tools component embed, then new): the imports are lowered with the memory and the realloc of the module."
  wasm-tools print "$work/command.wasm"
} > "$here/p2_command.component.wat"
