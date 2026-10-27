;; A WASI 0.2 core module that traps: it writes to a stream handle that does not exist.
(module
  (import "wasi:io/streams@0.2.6" "[method]output-stream.blocking-write-and-flush" (func $write_and_flush (param i32 i32 i32 i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "x")
  (func (export "cabi_realloc") (param i32 i32 i32 i32) (result i32) (local.get 2))
  (func (export "run")
    (call $write_and_flush (i32.const 999) (i32.const 0) (i32.const 1) (i32.const 64))))
