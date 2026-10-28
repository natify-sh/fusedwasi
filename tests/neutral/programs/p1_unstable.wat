;; A command of the first snapshot of WASI 0.1 (`wasi_unstable`, "preview0") that creates a file, writes "abc12" to it, seeks, reads and ends with proc_exit.
;; Written by hand for the tests of tests/neutral. The snapshot orders `whence` as cur, end, set (the later one: set, cur, end), so the same bytes under `wasi_snapshot_preview1` would seek elsewhere.
;;
;; Output, in order (raw bytes): the errno of fd_seek(fd, 0, 2) which is "set" here, the position it gave (8 bytes), what fd_read gets (5 bytes), the errno of fd_seek(fd, -2, 1) which is "end" here, and
;; the position it gave (8 bytes); the exit code is that position (3).
(module
  (import "wasi_unstable" "path_open" (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_unstable" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_unstable" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_unstable" "fd_seek" (func $fd_seek (param i32 i64 i32 i32) (result i32)))
  (import "wasi_unstable" "proc_exit" (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "u.txt")
  (data (i32.const 32) "abc12")

  (func $out (param $ptr i32) (param $len i32)
    (i32.store (i32.const 0) (local.get $ptr))
    (i32.store (i32.const 4) (local.get $len))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 600))))
  (func $num (param $value i32)
    (i32.store (i32.const 604) (local.get $value))
    (call $out (i32.const 604) (i32.const 4)))

  (func (export "_start")
    (local $fd i32)
    (drop (call $path_open (i32.const 3) (i32.const 0) (i32.const 8) (i32.const 5) (i32.const 1)
      (i64.const 0x1fffffff) (i64.const 0x1fffffff) (i32.const 0) (i32.const 540)))
    (local.set $fd (i32.load (i32.const 540)))
    (i32.store (i32.const 0) (i32.const 32))
    (i32.store (i32.const 4) (i32.const 5))
    (drop (call $fd_write (local.get $fd) (i32.const 0) (i32.const 1) (i32.const 548)))
    ;; whence 2 is "set" in this snapshot
    (call $num (call $fd_seek (local.get $fd) (i64.const 0) (i32.const 2) (i32.const 640)))
    (call $out (i32.const 640) (i32.const 8))
    (i32.store (i32.const 0) (i32.const 4096))
    (i32.store (i32.const 4) (i32.const 5))
    (drop (call $fd_read (local.get $fd) (i32.const 0) (i32.const 1) (i32.const 552)))
    (call $out (i32.const 4096) (i32.load (i32.const 552)))
    ;; whence 1 is "end" in this snapshot: 5 - 2
    (call $num (call $fd_seek (local.get $fd) (i64.const -2) (i32.const 1) (i32.const 640)))
    (call $out (i32.const 640) (i32.const 8))
    (call $proc_exit (i32.load (i32.const 640)))))
