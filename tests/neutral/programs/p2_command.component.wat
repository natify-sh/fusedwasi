;; p2_command.wat as a component, made by make_p2_command_component.sh (wasm-tools component embed, then new): the imports are lowered with the memory and the realloc of the module.
(component
  (type $ty-wasi:cli/environment@0.2.6 (;0;)
    (instance
      (type (;0;) (tuple string string))
      (type (;1;) (list 0))
      (type (;2;) (func (result 1)))
      (export (;0;) "get-environment" (func (type 2)))
      (type (;3;) (list string))
      (type (;4;) (func (result 3)))
      (export (;1;) "get-arguments" (func (type 4)))
    )
  )
  (import "wasi:cli/environment@0.2.6" (instance $wasi:cli/environment@0.2.6 (;0;) (type $ty-wasi:cli/environment@0.2.6)))
  (type $ty-wasi:io/error@0.2.6 (;1;)
    (instance
      (export (;0;) "error" (type (sub resource)))
    )
  )
  (import "wasi:io/error@0.2.6" (instance $wasi:io/error@0.2.6 (;1;) (type $ty-wasi:io/error@0.2.6)))
  (alias export $wasi:io/error@0.2.6 "error" (type $error (;2;)))
  (type $ty-wasi:io/streams@0.2.6 (;3;)
    (instance
      (export (;0;) "output-stream" (type (sub resource)))
      (alias outer 1 $error (type (;1;)))
      (export (;2;) "error" (type (eq 1)))
      (type (;3;) (own 2))
      (type (;4;) (variant (case "last-operation-failed" 3) (case "closed")))
      (export (;5;) "stream-error" (type (eq 4)))
      (type (;6;) (borrow 0))
      (type (;7;) (list u8))
      (type (;8;) (result (error 5)))
      (type (;9;) (func (param "self" 6) (param "contents" 7) (result 8)))
      (export (;0;) "[method]output-stream.blocking-write-and-flush" (func (type 9)))
    )
  )
  (import "wasi:io/streams@0.2.6" (instance $wasi:io/streams@0.2.6 (;2;) (type $ty-wasi:io/streams@0.2.6)))
  (alias export $wasi:io/streams@0.2.6 "output-stream" (type $output-stream (;4;)))
  (type $ty-wasi:cli/stdout@0.2.6 (;5;)
    (instance
      (alias outer 1 $output-stream (type (;0;)))
      (export (;1;) "output-stream" (type (eq 0)))
      (type (;2;) (own 1))
      (type (;3;) (func (result 2)))
      (export (;0;) "get-stdout" (func (type 3)))
    )
  )
  (import "wasi:cli/stdout@0.2.6" (instance $wasi:cli/stdout@0.2.6 (;3;) (type $ty-wasi:cli/stdout@0.2.6)))
  (type $ty-wasi:cli/exit@0.2.6 (;6;)
    (instance
      (type (;0;) (result))
      (type (;1;) (func (param "status" 0)))
      (export (;0;) "exit" (func (type 1)))
    )
  )
  (import "wasi:cli/exit@0.2.6" (instance $wasi:cli/exit@0.2.6 (;4;) (type $ty-wasi:cli/exit@0.2.6)))
  (type $ty-wasi:clocks/monotonic-clock@0.2.6 (;7;)
    (instance
      (type (;0;) u64)
      (export (;1;) "instant" (type (eq 0)))
      (type (;2;) (func (result 1)))
      (export (;0;) "now" (func (type 2)))
    )
  )
  (import "wasi:clocks/monotonic-clock@0.2.6" (instance $wasi:clocks/monotonic-clock@0.2.6 (;5;) (type $ty-wasi:clocks/monotonic-clock@0.2.6)))
  (core module $main (;0;)
    (type (;0;) (func (param i32)))
    (type (;1;) (func (result i32)))
    (type (;2;) (func (param i32 i32 i32 i32)))
    (type (;3;) (func (result i64)))
    (type (;4;) (func (param i32 i32 i32 i32) (result i32)))
    (type (;5;) (func (param i32 i32 i32)))
    (type (;6;) (func))
    (import "wasi:cli/environment@0.2.6" "get-arguments" (func $get_arguments (;0;) (type 0) (param i32)))
    (import "wasi:cli/environment@0.2.6" "get-environment" (func $get_environment (;1;) (type 0) (param i32)))
    (import "wasi:cli/stdout@0.2.6" "get-stdout" (func $get_stdout (;2;) (type 1) (result i32)))
    (import "wasi:io/streams@0.2.6" "[method]output-stream.blocking-write-and-flush" (func $write_and_flush (;3;) (type 2) (param i32 i32 i32 i32)))
    (import "wasi:io/streams@0.2.6" "[resource-drop]output-stream" (func $drop_output (;4;) (type 0) (param i32)))
    (import "wasi:clocks/monotonic-clock@0.2.6" "now" (func $now (;5;) (type 3) (result i64)))
    (import "wasi:cli/exit@0.2.6" "exit" (func $exit (;6;) (type 0) (param i32)))
    (memory (;0;) 1)
    (global $heap (;0;) (mut i32) i32.const 8192)
    (export "memory" (memory 0))
    (export "cabi_realloc" (func 7))
    (export "run" (func 10))
    (export "wasi:cli/run@0.2.6#run" (func 11))
    (func (;7;) (type 4) (param $old_ptr i32) (param $old_size i32) (param $align i32) (param $new_size i32) (result i32)
      (local $ptr i32)
      local.get $new_size
      i32.eqz
      if ;; label = @1
        local.get $align
        return
      end
      global.get $heap
      local.get $align
      i32.const 1
      i32.sub
      i32.add
      local.get $align
      i32.const 1
      i32.sub
      i32.const -1
      i32.xor
      i32.and
      local.set $ptr
      local.get $ptr
      local.get $new_size
      i32.add
      global.set $heap
      local.get $ptr
    )
    (func $write (;8;) (type 5) (param $out i32) (param $ptr i32) (param $len i32)
      local.get $out
      local.get $ptr
      local.get $len
      i32.const 96
      call $write_and_flush
    )
    (func $main (;9;) (type 6)
      (local $out i32) (local $list i32) (local $count i32) (local $i i32) (local $entry i32)
      call $get_stdout
      local.set $out
      i32.const 64
      call $get_arguments
      i32.const 64
      i32.load
      local.set $list
      i32.const 68
      i32.load
      local.set $count
      i32.const 0
      local.set $i
      block $done
        loop $each
          local.get $i
          local.get $count
          i32.ge_u
          br_if $done
          local.get $list
          local.get $i
          i32.const 3
          i32.shl
          i32.add
          local.set $entry
          local.get $out
          local.get $entry
          i32.load
          local.get $entry
          i32.load offset=4
          call $write
          local.get $out
          i32.const 0
          i32.const 1
          call $write
          local.get $i
          i32.const 1
          i32.add
          local.set $i
          br $each
        end
      end
      i32.const 64
      call $get_environment
      i32.const 64
      i32.load
      local.set $list
      i32.const 68
      i32.load
      local.set $count
      i32.const 0
      local.set $i
      block $done
        loop $each
          local.get $i
          local.get $count
          i32.ge_u
          br_if $done
          local.get $list
          local.get $i
          i32.const 4
          i32.shl
          i32.add
          local.set $entry
          local.get $out
          local.get $entry
          i32.load
          local.get $entry
          i32.load offset=4
          call $write
          local.get $out
          i32.const 16
          i32.const 1
          call $write
          local.get $out
          local.get $entry
          i32.load offset=8
          local.get $entry
          i32.load offset=12
          call $write
          local.get $out
          i32.const 0
          i32.const 1
          call $write
          local.get $i
          i32.const 1
          i32.add
          local.set $i
          br $each
        end
      end
      call $now
      i64.const 0
      i64.ne
      if ;; label = @1
        local.get $out
        i32.const 48
        i32.const 2
        call $write
      end
      local.get $out
      i32.const 32
      i32.const 4
      call $write
      local.get $out
      call $drop_output
      i32.const 1
      call $exit
    )
    (func (;10;) (type 6)
      call $main
    )
    (func (;11;) (type 1) (result i32)
      call $main
      i32.const 0
    )
    (data (;0;) (i32.const 0) "\0a")
    (data (;1;) (i32.const 16) "=")
    (data (;2;) (i32.const 32) "bye\0a")
    (data (;3;) (i32.const 48) "t\0a")
    (@producers
      (processed-by "wit-component" "0.262.0")
    )
  )
  (core module $wit-component-shim-module (;1;)
    (type (;0;) (func (param i32)))
    (type (;1;) (func (param i32 i32 i32 i32)))
    (table (;0;) 3 3 funcref)
    (export "0" (func $indirect-wasi:cli/environment@0.2.6-get-arguments))
    (export "1" (func $indirect-wasi:cli/environment@0.2.6-get-environment))
    (export "2" (func $"indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush"))
    (export "$imports" (table 0))
    (func $indirect-wasi:cli/environment@0.2.6-get-arguments (;0;) (type 0) (param i32)
      local.get 0
      i32.const 0
      call_indirect (type 0)
    )
    (func $indirect-wasi:cli/environment@0.2.6-get-environment (;1;) (type 0) (param i32)
      local.get 0
      i32.const 1
      call_indirect (type 0)
    )
    (func $"indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush" (;2;) (type 1) (param i32 i32 i32 i32)
      local.get 0
      local.get 1
      local.get 2
      local.get 3
      i32.const 2
      call_indirect (type 1)
    )
    (@producers
      (processed-by "wit-component" "0.262.0")
    )
  )
  (core instance $wit-component-shim-instance (;0;) (instantiate $wit-component-shim-module))
  (alias core export $wit-component-shim-instance "0" (core func $indirect-wasi:cli/environment@0.2.6-get-arguments (;0;)))
  (alias core export $wit-component-shim-instance "1" (core func $indirect-wasi:cli/environment@0.2.6-get-environment (;1;)))
  (core instance $wasi:cli/environment@0.2.6 (;1;)
    (export "get-arguments" (func $indirect-wasi:cli/environment@0.2.6-get-arguments))
    (export "get-environment" (func $indirect-wasi:cli/environment@0.2.6-get-environment))
  )
  (alias export $wasi:cli/stdout@0.2.6 "get-stdout" (func $get-stdout (;0;)))
  (core func $get-stdout (;2;) (canon lower (func $get-stdout)))
  (core instance $wasi:cli/stdout@0.2.6 (;2;)
    (export "get-stdout" (func $get-stdout))
  )
  (alias core export $wit-component-shim-instance "2" (core func $"indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush" (;3;)))
  (alias export $wasi:io/streams@0.2.6 "output-stream" (type $"#type8 output-stream" (@name "output-stream") (;8;)))
  (core func $resource.drop (;4;) (canon resource.drop $"#type8 output-stream"))
  (core instance $wasi:io/streams@0.2.6 (;3;)
    (export "[method]output-stream.blocking-write-and-flush" (func $"indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush"))
    (export "[resource-drop]output-stream" (func $resource.drop))
  )
  (alias export $wasi:clocks/monotonic-clock@0.2.6 "now" (func $now (;1;)))
  (core func $now (;5;) (canon lower (func $now)))
  (core instance $wasi:clocks/monotonic-clock@0.2.6 (;4;)
    (export "now" (func $now))
  )
  (alias export $wasi:cli/exit@0.2.6 "exit" (func $exit (;2;)))
  (core func $exit (;6;) (canon lower (func $exit)))
  (core instance $wasi:cli/exit@0.2.6 (;5;)
    (export "exit" (func $exit))
  )
  (core instance $main (;6;) (instantiate $main
      (with "wasi:cli/environment@0.2.6" (instance $wasi:cli/environment@0.2.6))
      (with "wasi:cli/stdout@0.2.6" (instance $wasi:cli/stdout@0.2.6))
      (with "wasi:io/streams@0.2.6" (instance $wasi:io/streams@0.2.6))
      (with "wasi:clocks/monotonic-clock@0.2.6" (instance $wasi:clocks/monotonic-clock@0.2.6))
      (with "wasi:cli/exit@0.2.6" (instance $wasi:cli/exit@0.2.6))
    )
  )
  (alias core export $main "memory" (core memory $memory (;0;)))
  (core module $wit-component-fixup (;2;)
    (type (;0;) (func (param i32)))
    (type (;1;) (func (param i32 i32 i32 i32)))
    (import "actual" "0" (func $0 (;0;) (type 0) (param i32)))
    (import "actual" "1" (func $1 (;1;) (type 0) (param i32)))
    (import "actual" "2" (func $2 (;2;) (type 1) (param i32 i32 i32 i32)))
    (import "shim" "$imports" (table (;0;) 3 3 funcref))
    (elem (;0;) (i32.const 0) func $0 $1 $2)
    (@producers
      (processed-by "wit-component" "0.262.0")
    )
  )
  (alias export $wasi:cli/environment@0.2.6 "get-arguments" (func $get-arguments (;3;)))
  (alias core export $main "cabi_realloc" (core func $cabi_realloc (;7;)))
  (core func $"#core-func8 indirect-wasi:cli/environment@0.2.6-get-arguments" (@name "indirect-wasi:cli/environment@0.2.6-get-arguments") (;8;) (canon lower (func $get-arguments) (memory $memory) (realloc $cabi_realloc) string-encoding=utf8))
  (alias export $wasi:cli/environment@0.2.6 "get-environment" (func $get-environment (;4;)))
  (core func $"#core-func9 indirect-wasi:cli/environment@0.2.6-get-environment" (@name "indirect-wasi:cli/environment@0.2.6-get-environment") (;9;) (canon lower (func $get-environment) (memory $memory) (realloc $cabi_realloc) string-encoding=utf8))
  (alias export $wasi:io/streams@0.2.6 "[method]output-stream.blocking-write-and-flush" (func $"[method]output-stream.blocking-write-and-flush" (;5;)))
  (core func $"#core-func10 indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush" (@name "indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush") (;10;) (canon lower (func $"[method]output-stream.blocking-write-and-flush") (memory $memory)))
  (core instance $actual (;7;)
    (export "0" (func $"#core-func8 indirect-wasi:cli/environment@0.2.6-get-arguments"))
    (export "1" (func $"#core-func9 indirect-wasi:cli/environment@0.2.6-get-environment"))
    (export "2" (func $"#core-func10 indirect-wasi:io/streams@0.2.6-[method]output-stream.blocking-write-and-flush"))
  )
  (core instance $fixup (;8;) (instantiate $wit-component-fixup
      (with "actual" (instance $actual))
      (with "shim" (instance $wit-component-shim-instance))
    )
  )
  (type (;9;) (result))
  (type (;10;) (func (result 9)))
  (alias core export $main "wasi:cli/run@0.2.6#run" (core func $wasi:cli/run@0.2.6#run (;11;)))
  (func $run (;6;) (type 10) (canon lift (core func $wasi:cli/run@0.2.6#run)))
  (component $wasi:cli/run@0.2.6-shim-component (;0;)
    (type (;0;) (result))
    (type (;1;) (func (result 0)))
    (import "import-func-run" (func (;0;) (type 1)))
    (type (;2;) (result))
    (type (;3;) (func (result 2)))
    (export (;1;) "run" (func 0) (func (type 3)))
  )
  (instance $wasi:cli/run@0.2.6-shim-instance (;6;) (instantiate $wasi:cli/run@0.2.6-shim-component
      (with "import-func-run" (func $run))
    )
  )
  (export $wasi:cli/run@0.2.6 (;7;) "wasi:cli/run@0.2.6" (instance $wasi:cli/run@0.2.6-shim-instance))
  (@producers
    (processed-by "wit-component" "0.262.0")
  )
)
