;; A WASI 0.1 command that writes what it sees to standard output, as raw bytes, then ends with proc_exit(3).
;; Written by hand for the tests of tests/neutral; it knows nothing of fusedwasi or of the engine that runs it.
;;
;; Output, in order: "hello p1\n"; (argc, size of the arguments) and the arguments; (count, size of the environment) and the environment;
;; fd_prestat_get(3): the record, then its errno; path_open of "f.txt" to create it: errno and fd; the file is written, closed, opened again and read back;
;; the errnos of clock_time_get, random_get, fd_write to a bad descriptor and fd_close of the same.
(module
  (import "wasi_snapshot_preview1" "args_sizes_get" (func $args_sizes_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_get" (func $args_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_sizes_get" (func $environ_sizes_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_get" (func $environ_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $fd_close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_get" (func $fd_prestat_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_open" (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get" (func $clock_time_get (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "random_get" (func $random_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "f.txt")
  (data (i32.const 16) "hello p1\n")
  (data (i32.const 32) "data!")

  ;; write `len` bytes at `ptr` to standard output
  (func $out (param $ptr i32) (param $len i32)
    (i32.store (i32.const 0) (local.get $ptr))
    (i32.store (i32.const 4) (local.get $len))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 600))))
  ;; write a 32-bit number
  (func $num (param $value i32)
    (i32.store (i32.const 604) (local.get $value))
    (call $out (i32.const 604) (i32.const 4)))

  (func (export "_start")
    (local $fd i32)
    (call $out (i32.const 16) (i32.const 9))
    ;; the arguments
    (call $num (call $args_sizes_get (i32.const 512) (i32.const 516)))
    (call $out (i32.const 512) (i32.const 8))
    (call $num (call $args_get (i32.const 1024) (i32.const 2048)))
    (call $out (i32.const 2048) (i32.load (i32.const 516)))
    ;; the environment
    (call $num (call $environ_sizes_get (i32.const 520) (i32.const 524)))
    (call $out (i32.const 520) (i32.const 8))
    (call $num (call $environ_get (i32.const 1100) (i32.const 3072)))
    (call $out (i32.const 3072) (i32.load (i32.const 524)))
    ;; the preopened directory
    (call $num (call $fd_prestat_get (i32.const 3) (i32.const 528)))
    (call $out (i32.const 528) (i32.const 8))
    ;; create a file, write to it, close it
    (call $num (call $path_open (i32.const 3) (i32.const 0) (i32.const 8) (i32.const 5) (i32.const 1)
      (i64.const 0x1fffffff) (i64.const 0x1fffffff) (i32.const 0) (i32.const 540)))
    (call $num (i32.load (i32.const 540)))
    (local.set $fd (i32.load (i32.const 540)))
    (i32.store (i32.const 0) (i32.const 32))
    (i32.store (i32.const 4) (i32.const 5))
    (call $num (call $fd_write (local.get $fd) (i32.const 0) (i32.const 1) (i32.const 548)))
    (call $num (i32.load (i32.const 548)))
    (call $num (call $fd_close (local.get $fd)))
    ;; open it again and read it back
    (call $num (call $path_open (i32.const 3) (i32.const 0) (i32.const 8) (i32.const 5) (i32.const 0)
      (i64.const 0x1fffffff) (i64.const 0x1fffffff) (i32.const 0) (i32.const 540)))
    (local.set $fd (i32.load (i32.const 540)))
    (i32.store (i32.const 0) (i32.const 4096))
    (i32.store (i32.const 4) (i32.const 64))
    (call $num (call $fd_read (local.get $fd) (i32.const 0) (i32.const 1) (i32.const 552)))
    (call $out (i32.const 4096) (i32.load (i32.const 552)))
    (drop (call $fd_close (local.get $fd)))
    ;; calls whose result is only an errno
    (call $num (call $clock_time_get (i32.const 1) (i64.const 1) (i32.const 560)))
    (call $num (call $random_get (i32.const 576) (i32.const 16)))
    (call $num (call $fd_write (i32.const 77) (i32.const 0) (i32.const 1) (i32.const 548)))
    (call $num (call $fd_close (i32.const 77)))
    (call $proc_exit (i32.const 3))))
