# The public names

Written by `tools/public_api.py doc` from `src/fusedwasi/public_api.json`; do not edit. The rule (`design_model.md`, section 5): a name is public only if it is listed here (or is a function of the tables of signatures); every other name of the library is private and carries a mark that a user cannot miss, so that a change of a private name breaks nobody.

* **C**: a private name has a double underscore right after the prefix of the library (`fusedwasi_p2__table_get`, `FUSEDWASI_P2__INDEX_BITS`); a private field of a public struct ends with two underscores (`state.slots__`). No public name has `__` in it.
* **Python**: a private name has a leading underscore (`_check`, `State._slots`). A module lists no other name than the ones below and the functions of its imports.
* **unstable**: public in the alpha, with no promise: wasi:http and wasi:sockets.

`tests/test_public_api.py` fails when a name of the headers is neither here nor marked, when a name here does not exist, when a neutral user (`tests/neutral/`) uses a name that is not here, and when a module of the Python twin has a name with no underscore that is not here. Counts at the last run: C 327 public names besides the 247 functions of the imports, 544 private names and 10 private fields.

## C

### The world and the caller

| Name | Purpose |
| --- | --- |
| `fusedwasi_p2_state` | the world: configuration, tables of handles, policies, clocks; one for everything that shares files and sockets (WASI 0.1 and 0.2 alike) |
| `fusedwasi_p2_guest` | the caller: memories, trap, realloc, wait, exit, signal, the end of a call; one for each instance, over one state |
| `fusedwasi_p2_preopen` | a directory of the host that the guest sees under a name: {path, name, writable} (the same struct for both families) |
| `fusedwasi_p2_init` | makes a state: all zero, with the system's clocks, random source and sleep |
| `fusedwasi_p2_free` | frees what the state holds and closes it; the guests over it must not be used afterwards, except to see what a closed state answers |
| `fusedwasi_p2_state_closed` | whether the state is closed (`fusedwasi_p2_free` was called): no handle is alive, none is made, every descriptor of 0.1 is BADF |
| `fusedwasi_p2_guest_init` | makes a guest over a state: all zero, with the default trap (aborts), wait and exit |
| `fusedwasi_p2_bind_memory` | binds memory number `comp` of a guest by pointer (base and size read at each access, so a memory that moves is followed) |
| `fusedwasi_p2_set_memory` | binds memory number `comp` by value (the guest keeps a copy; set it again when the memory grows) |
| `FUSEDWASI_P2_MAX_MEMORIES` | the number of memories a guest can bind (`bind_memory` answers -1 beyond it) |

### Policies and numbers the host passes

| Name | Purpose |
| --- | --- |
| `FUSEDWASI_P2_USE_TCP_BIND` | `use` that `socket_addr_check` is asked about (0) |
| `FUSEDWASI_P2_USE_TCP_CONNECT` | `use` of `socket_addr_check` (1) |
| `FUSEDWASI_P2_USE_UDP_BIND` | `use` of `socket_addr_check` (2) |
| `FUSEDWASI_P2_USE_UDP_CONNECT` | `use` of `socket_addr_check` (3) |
| `FUSEDWASI_P2_USE_UDP_OUTGOING` | `use` of `socket_addr_check` (4) |
| `fusedwasi_p2_use_name` | the name of a `use` ("tcp-bind", ..., "unknown") |
| `FUSEDWASI_P2_TERMINAL_STDIN` | bit of `state.terminals`: the standard input is a terminal |
| `FUSEDWASI_P2_TERMINAL_STDOUT` | bit of `state.terminals`: the standard output is a terminal |
| `FUSEDWASI_P2_TERMINAL_STDERR` | bit of `state.terminals`: the standard error is a terminal |

### Markers of the headers

| Name | Purpose |
| --- | --- |
| `FUSEDWASI_P1_FN` | marks a function of WASI 0.1 in a header (`static inline`); the generator of the table of signatures finds the functions by it |
| `FUSEDWASI_P2_FN` | marks a function of WASI 0.2 in a header (`static inline`); same use |

### The table of handles and the kinds

| Name | Purpose |
| --- | --- |
| `fusedwasi_p2_table_add` | adds an object of the host to the table of a state (a resource of the world); the handle, or 0 |
| `FUSEDWASI_P2_KIND_*` | the kind of a resource in the table (pollable, error, streams, descriptor, sockets, http, ...), the argument `kind` of `table_add` |

### Streams, pollables and the constructors of the host

| Name | Purpose |
| --- | --- |
| `fusedwasi_p2_stream` | a stream of the host: the callbacks read / check_write / write / flush / ready / destroy and the way to say why it failed |
| `FUSEDWASI_P2_STREAM_OK` | status of a callback of a stream: done |
| `FUSEDWASI_P2_STREAM_CLOSED` | status of a callback of a stream: the end |
| `FUSEDWASI_P2_STREAM_FAILED` | status of a callback of a stream: failed, `failure` says why |
| `fusedwasi_p2_input_stream_add` | puts a stream of the host in the table as an input stream; the handle, or 0 |
| `fusedwasi_p2_output_stream_add` | puts a stream of the host in the table as an output stream; the handle, or 0 |
| `fusedwasi_p2_error_add` | makes an `error` resource (a message, optional codes) in the table; the handle, or 0 |
| `fusedwasi_p2_std_get` | the standard stream i of a state (0 stdin, 1 stdout, 2 stderr): the one the host gave, or the process's own |
| `fusedwasi_p2_pollable` | something a guest can wait for: `ready(state, p)` answers 1, 0, or -1 with `failure` |
| `fusedwasi_p2_deadline_pollable_add` | a pollable ready at an instant of the monotonic clock; the handle, or 0 |
| `fusedwasi_p2_stream_pollable_add` | a pollable that is ready when a stream is; the handle, or 0 |
| `fusedwasi_p2_memory_input` | a stream over bytes the host owns (the struct; `base` is the stream) |
| `fusedwasi_p2_memory_input_new` | an input stream over bytes, in the table; the handle, or 0 |
| `fusedwasi_p2_buffer_output` | a stream that collects what is written (the struct; read `buf` and `len`) |
| `fusedwasi_p2_buffer_output_new` | an output stream that collects up to `limit` bytes, in the table; the stream, or NULL |
| `fusedwasi_p2_buffer_output_release` | frees a buffer output after its handle is dropped or the state freed |
| `fusedwasi_p2_fd_stream` | a stream over a descriptor of the process (the struct; `base` is the stream) |
| `fusedwasi_p2_fd_input_new` | a stream that reads a descriptor of the process (not in the table; give it to `state.std[0]`) |
| `fusedwasi_p2_fd_output_new` | a stream that writes a descriptor of the process (not in the table; give it to `state.std[1]` or `[2]`) |
| `FUSEDWASI_P2_E_*` | the error-codes of wasi:filesystem (`failure_code` of a stream, `fusedwasi_p2_error_add`) |
| `FUSEDWASI_P2_N_*` | the error-codes of wasi:sockets (`failure_network_code` of a stream) |

### WASI 0.1

| Name | Purpose |
| --- | --- |
| `fusedwasi_p1_open_descriptors` | makes the descriptors of 0.1 now and tells a preopen that cannot be opened (otherwise the first call of a function of 0.1 does it) |
| `fusedwasi_p1_errno` | the errno of WASI for an errno of the C library |
| `FUSEDWASI_P1_E_*` | the errno values of WASI 0.1 (`FUSEDWASI_P1_E_SUCCESS`, `E_NOTSUP`, ...), the result of a function and of the `signal` hook |
| `FUSEDWASI_P1_FT_*` | file types of WASI 0.1 |
| `FUSEDWASI_P1_FD_*` | flags of a descriptor of WASI 0.1 (append, dsync, nonblock, rsync, sync) |
| `FUSEDWASI_P1_O_*` | open flags of `path_open` (creat, directory, excl, trunc) |
| `FUSEDWASI_P1_R_*` | rights of a descriptor of WASI 0.1, `R_ALL` and `R_MUTATING` included |
| `FUSEDWASI_P1_RIGHTS_STDIN` | the rights of the standard input |
| `FUSEDWASI_P1_RIGHTS_STDOUT` | the rights of the standard output and error |
| `FUSEDWASI_P1_T_*` | flags of the times of `fd_filestat_set_times` (atim, atim_now, mtim, mtim_now) |
| `FUSEDWASI_P1_LOOKUP_FOLLOW` | the flag `symlink_follow` of the lookup of `path_open` and `path_filestat_get` |
| `fusedwasi_p1_unstable_*` | the five functions of `wasi_unstable` that differ from `wasi_snapshot_preview1` (the others of that snapshot are the `fusedwasi_p1_<name>` ones) |

### The functions of the imports

| Name | Purpose |
| --- | --- |
| `<c_name of the tables of signatures>` | the 46 functions `fusedwasi_p1_<name>` and the 201 functions `fusedwasi_wasi_<package>_<interface>_<name>`: the names the tables bind by; read from `signatures/wasi_p1.json` and `wasi_p2.json` (`c_name`), not copied here |

### The transport and the server side of wasi:http (unstable in the alpha)

_wasi:http is unstable in the alpha: these names may change before they are used by a real program_

| Name | Purpose |
| --- | --- |
| `fusedwasi_p2_http_request` | what `state.http_send` is given: method, scheme, authority, path, headers, body, timeouts |
| `fusedwasi_p2_http_response` | what `state.http_send` gives back: status, headers, body, trailers |
| `fusedwasi_p2_http_error` | an error-code of wasi:http (a tag from `FUSEDWASI_P2_HTTP_*` and the leaves of its payload) |
| `fusedwasi_p2_http_fields` | a set of fields (headers, trailers) the transport reads or builds |
| `fusedwasi_p2_http_blob` | the bytes of a body the transport gives back |
| `fusedwasi_p2_http_error_init` | starts an error-code with a tag |
| `fusedwasi_p2_http_error_free` | frees the strings of an error-code |
| `fusedwasi_p2_http_error_set_str` | sets a string leaf of an error-code |
| `fusedwasi_p2_http_error_set_num` | sets a number leaf of an error-code |
| `fusedwasi_p2_http_fail` | fills an error-code with a tag and answers 1 (what a transport returns) |
| `fusedwasi_p2_http_fail_str` | the same with a string leaf |
| `fusedwasi_p2_http_fail_num` | the same with a number leaf |
| `fusedwasi_p2_http_fields_new` | a new set of fields (one owner; `immutable` for the fields of a request that was sent) |
| `fusedwasi_p2_http_fields_add` | appends a field |
| `fusedwasi_p2_http_fields_unref` | drops a reference to a set of fields |
| `fusedwasi_p2_http_blob_new` | a body of bytes (copied), with one reference |
| `fusedwasi_p2_http_blob_unref` | drops a reference to a body |
| `fusedwasi_p2_http_default_send` | the transport of the library (HTTP/1.1 over a socket, `http` only), the default of `state.http_send` |
| `fusedwasi_p2_http_fields_register` | puts a set of fields in the table; the handle, or 0 |
| `fusedwasi_p2_http_incoming_request_new` | server side: an incoming request in the table, to be given to an exported handler; the handle, or 0 |
| `fusedwasi_p2_http_outparam_new` | server side: the `response-outparam` for the handler; the host reads the response from it |
| `fusedwasi_p2_http_outparam` | server side: the struct behind the outparam handle |
| `FUSEDWASI_P2_HTTP_*` | the error-code tags of wasi:http in the order of the WIT, and the limits `FUSEDWASI_P2_HTTP_MAX_BODY` and `FUSEDWASI_P2_HTTP_MAX_HEAD` |

Not public although they match a pattern above: `FUSEDWASI_P2_KIND_P1_FDS`, `FUSEDWASI_P2_E_ESCAPE`, `FUSEDWASI_P2_E_NO_CLOCK`, `FUSEDWASI_P2_HTTP_H`, `FUSEDWASI_P2_HTTP_CASES`, `FUSEDWASI_P2_HTTP_DEFAULT_TIMEOUT_NS`, `FUSEDWASI_P2_HTTP_OOM`.

### Fields of the public structs

`fusedwasi_p2_state`:

| Field | Purpose |
| --- | --- |
| `args` | the arguments of the program (owned by the caller, must outlive the state) |
| `arg_count` | their number |
| `env_names` | the names of the environment |
| `env_values` | their values |
| `env_count` | their number |
| `initial_cwd` | the initial working directory, or NULL |
| `preopens` | the preopened directories |
| `preopen_count` | their number |
| `socket_addr_check` | policy hook: may this bind or connect (`use` is `FUSEDWASI_P2_USE_*`) go through; NULL refuses everything |
| `allow_ip_name_lookup` | policy: name resolution allowed |
| `http_check` | policy hook: may this outgoing request go through; NULL refuses everything |
| `http_send` | hook: the transport of http (NULL: the library's) |
| `std` | the standard streams (NULL: the descriptors of the process) |
| `terminals` | mask of `FUSEDWASI_P2_TERMINAL_*` |
| `random_bytes` | hook: the secure source |
| `insecure_random_bytes` | hook: the insecure source |
| `clock` | hook: the clocks 0 to 3, in nanoseconds |
| `resolution_ns` | their resolutions |
| `sleep` | hook: how a call of 0.1 waits |
| `user` | the pointer of whoever made the state, given back to its hooks; never touched by the library |
| `slots__`, `slot_count__`, `slot_capacity__`, `std_default__`, `p1_table__`, `closed__` | private: not to be read or written by a host |

`fusedwasi_p2_guest`:

| Field | Purpose |
| --- | --- |
| `state` | the world this guest calls into |
| `trap` | hook: a trap does not return (a longjmp, or what the engine has); the default aborts |
| `realloc` | hook: the `cabi_realloc` of a component |
| `wait` | hook: what a blocking call does between two looks at a pollable |
| `exit` | hook: told when the guest calls `wasi:cli/exit` or `proc_exit`; the default records the code and traps |
| `signal` | hook: `proc_raise` of 0.1 asks it; NULL answers NOTSUP |
| `exited` | the end of the last call: the guest exited (the host clears it before the next call) |
| `exit_code` | valid when `exited` |
| `user` | the pointer of whoever made the guest, given back to its hooks; never touched by the library |
| `memory__`, `memory_cell__`, `memories__` | private: not to be read or written by a host |

`fusedwasi_p2_stream`:

| Field | Purpose |
| --- | --- |
| `read` | callback: reads from the stream |
| `check_write` | callback: how much can be written |
| `write` | callback: writes |
| `flush` | callback: flushes |
| `ready` | callback: is the stream ready |
| `destroy` | callback: when the handle is dropped or the state freed |
| `failure` | the message of a FAILED |
| `failure_has_code` | a wasi:filesystem error-code is given |
| `failure_code` | that error-code |
| `failure_network_code` | the wasi:sockets error-code, when failure_has_network |
| `failure_has_network` | a wasi:sockets error-code is given |
| `failure_errno` | the errno of the system for a FAILED (WASI 0.1 gives it to the program) |
| `ctx` | the pointer of the host for its callbacks |
| `dead__` | private: not to be read or written by a host |

`fusedwasi_p2_pollable`:

| Field | Purpose |
| --- | --- |
| `ready` | callback: 1, 0, or -1 with `failure` |
| `ctx` | the pointer of the host |
| `deadline` | for the clocks |
| `failure` | why `ready` answered -1 |

`fusedwasi_p2_preopen`:

| Field | Purpose |
| --- | --- |
| `path` | the directory on the host |
| `name` | the name the guest sees |
| `writable` | 0: read only |

`fusedwasi_p2_memory_input`:

| Field | Purpose |
| --- | --- |
| `base` | the stream |
| `data` | the bytes |
| `len` | their number |
| `pos` | the position |

`fusedwasi_p2_buffer_output`:

| Field | Purpose |
| --- | --- |
| `base` | the stream |
| `buf` | what was written |
| `len` | how much |
| `capacity` | the allocated size |
| `limit` | the most it takes |

`fusedwasi_p2_fd_stream`:

| Field | Purpose |
| --- | --- |
| `base` | the stream |
| `fd` | the descriptor |

## Python

### `fusedwasi`

the top level: what belongs to no family.

| Name | Purpose |
| --- | --- |
| `__version__` | the version of the distribution |
| `get_include` | the directory with the C headers |
| `get_signatures_path` | the directory with the tables of signatures |

### `fusedwasi.signatures`

the tables of the signatures of every import (data, and the lookup that binds an import).

| Name | Purpose |
| --- | --- |
| `Snapshot` | the type of the argument of `load` ("p1" or "p2") |
| `Param` | a flat parameter or result: name, type, role, source_type |
| `Function` | a row of the table (module, name, params, results, c_name, python_module, python_name, takes_comp; `python_function()`) |
| `Signatures` | a table (`get`, `functions`, `unstable`: the rows of `wasi_unstable`) |
| `load` | loads the table of a snapshot |
| `find_import` | the row for an import of a core module (0.1, or any `0.2.N`), or None |
| `get_signatures_path` | the directory with the tables |

### `fusedwasi.p1`

the package of WASI 0.1: the classes, the helpers of the host and the constants of the specification.

| Name | Purpose |
| --- | --- |
| `State` | the world (the same class as `fusedwasi.p2.State`) |
| `Guest` | the caller (the same class as `fusedwasi.p2.Guest`) |
| `Exit` | raised when the program ends (the same class as `fusedwasi.p2.Exit`) |
| `WasiError` | an error of WASI with its errno (exported; nothing in the library raises it) |
| `PreopenError` | a preopen that cannot be opened (raised by `open_descriptors`) |
| `errno_of` | the errno of WASI for an `OSError` |
| `open_descriptors` | makes the descriptors of 0.1 now and raises `PreopenError` for a preopen that cannot be opened |
| `E_*` | the errno values of WASI 0.1 |
| `FT_*` | file types |
| `FD_*` | flags of a descriptor |
| `O_*` | open flags of `path_open` |
| `R_*` | rights of a descriptor (`R_ALL`, `R_MUTATING` included) |
| `RIGHTS_*` | the rights of the standard streams |
| `T_*` | flags of the times |
| `LOOKUP_FOLLOW` | the flag `symlink_follow` |

### `fusedwasi.p2`

the package of WASI 0.2: the classes the host uses and the constants of its policy.

| Name | Purpose |
| --- | --- |
| `State` | the world |
| `Guest` | the caller |
| `Exit` | raised when the program ends |
| `Trap` | raised for a trap |
| `Pollable` | something a guest can wait for (`ready(state)`) |
| `flat_view` | any writable buffer as flat bytes |
| `use_name` | the name of a `use` of `socket_addr_check` |
| `USE_*` | the `use` that `socket_addr_check` is asked about |
| `TERMINAL_*` | bits of `State.terminals` |
| `KIND_*` | the kind of a resource in the table (`table_add`) |
| `BufferOutput` | an output stream that collects what is written |
| `MemoryInput` | an input stream over bytes |
| `InputStream` | base class of an input stream of the host |
| `OutputStream` | base class of an output stream of the host |
| `StreamClosed` | raised by a stream: the end |
| `StreamFailed` | raised by a stream: failure (`errno=` for 0.1) |

### `fusedwasi.p1.wasi_p1`

the 46 functions of `wasi_snapshot_preview1`.  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

### `fusedwasi.p1.wasi_p1_unstable`

the five functions of `wasi_unstable` that differ from the snapshot.

| Name | Purpose |
| --- | --- |
| `fd_seek` | `fd_seek` with the order of whence of that snapshot |
| `fd_fdstat_get` | `fd_fdstat_get` with 29 bits of rights |
| `fd_filestat_get` | `fd_filestat_get` with a 32-bit link count |
| `path_filestat_get` | `path_filestat_get` with a 32-bit link count |
| `poll_oneoff` | `poll_oneoff` with the subscriptions of that snapshot |

### `fusedwasi.p1.wasi_p1_core`

the constants and helpers of 0.1 that `fusedwasi.p1` exports (the module itself is private).

### `fusedwasi.p2.wasi_p2_core`

the state, the guest and the table; `fusedwasi.p2` exports most of it.

| Name | Purpose |
| --- | --- |
| `State` | the world |
| `Guest` | the caller |
| `Exit` | raised when the program ends |
| `Trap` | raised for a trap |
| `Pollable` | something a guest can wait for |
| `flat_view` | any writable buffer as flat bytes |
| `use_name` | the name of a `use` |
| `USE_*` | see `fusedwasi.p2` |
| `TERMINAL_*` | see `fusedwasi.p2` |
| `KIND_*` | see `fusedwasi.p2` |
| `table_add` | adds an object of the host to the table of a state (a resource of the world); the handle, or 0 |

### `fusedwasi.p2.wasi_p2_io`

wasi:io: the streams of the host and their constructors.  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

| Name | Purpose |
| --- | --- |
| `InputStream` | base class of an input stream of the host |
| `OutputStream` | base class of an output stream of the host |
| `MemoryInput` | an input stream over bytes |
| `BufferOutput` | an output stream that collects what is written |
| `StreamClosed` | raised by a stream: the end |
| `StreamFailed` | raised by a stream: failure |
| `StreamPollable` | a pollable that is ready when a stream is (`table_add(state, KIND_POLLABLE, StreamPollable(stream))`) |
| `input_stream_add` | puts an input stream of the host in the table; the handle, or 0 |
| `output_stream_add` | puts an output stream of the host in the table; the handle, or 0 |
| `error_add` | makes an `error` resource in the table; the handle, or 0 |

### `fusedwasi.p2.wasi_p2_clocks`

wasi:clocks.  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

| Name | Purpose |
| --- | --- |
| `DeadlinePollable` | a pollable ready at an instant of the monotonic clock |

### `fusedwasi.p2.wasi_p2_cli`

wasi:cli.  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

| Name | Purpose |
| --- | --- |
| `std` | the standard stream `name` of a state (the one the host gave, or the process's own) |

### `fusedwasi.p2.wasi_p2_random`

wasi:random.  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

### `fusedwasi.p2.wasi_p2_filesystem`

wasi:filesystem, and the files behind both families (Python only: `State(filesystem=...)`).  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

| Name | Purpose |
| --- | --- |
| `Filesystem` | the operations on files as an object; a host with other files (in memory, say) subclasses it |
| `SystemFilesystem` | the files of the system (the default) |
| `FsError` | what an operation raises (an error-code of wasi:filesystem by name) |
| `Descriptor` | an open file or directory as the operations see it |
| `Times` | the times to set: the type of the argument of `set_times` |
| `READ` | descriptor flag: may read (the flags a `Filesystem` receives) |
| `WRITE` | descriptor flag: may write (the flags a `Filesystem` receives) |
| `FILE_SYNC` | descriptor flag: file integrity sync (the flags a `Filesystem` receives) |
| `DATA_SYNC` | descriptor flag: data integrity sync (the flags a `Filesystem` receives) |
| `REQUESTED_SYNC` | descriptor flag: requested write sync (the flags a `Filesystem` receives) |
| `MUTATE` | descriptor flag: may mutate the directory (the flags a `Filesystem` receives) |
| `SYMLINK_FOLLOW` | path flag: follow a symbolic link (the flags a `Filesystem` receives) |
| `CREATE` | open flag: create (the flags a `Filesystem` receives) |
| `DIRECTORY` | open flag: must be a directory (the flags a `Filesystem` receives) |
| `EXCLUSIVE` | open flag: fail if it exists (the flags a `Filesystem` receives) |
| `TRUNCATE` | open flag: truncate (the flags a `Filesystem` receives) |

### `fusedwasi.p2.wasi_p2_sockets` (unstable in the alpha)

wasi:sockets (unstable in the alpha).  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

### `fusedwasi.p2.wasi_p2_http` (unstable in the alpha)

wasi:http (unstable in the alpha).  Its functions are the ones of the table `signatures/wasi_*.json` (`python_module`, `python_name`).

| Name | Purpose |
| --- | --- |
| `ErrorCode` | raised by a transport for an error-code |
| `HttpRequest` | what `State.http_send` is given |
| `HttpResponse` | what `State.http_send` gives back |
| `Fields` | a set of fields |
| `fields_new` | a new set of fields |
| `default_send` | the transport of the library |
| `incoming_request_new` | server side: an incoming request in the table |
| `response_outparam_new` | server side: the `response-outparam` for the handler |
| `MAX_BODY` | the most a body can have |
| `MAX_HEAD` | the most a head can have |
| `OutParam` | server side: what `response_outparam_new` returns (the handler sets the response on it) |

### Members of the classes

`fusedwasi.p2.State`:

| Member | Purpose |
| --- | --- |
| `args` | the arguments |
| `env` | the environment (pairs) |
| `initial_cwd` | the initial working directory |
| `std` | the standard streams the host gave, by name |
| `preopens` | the preopens (path, name, writable) |
| `filesystem` | the files behind both families |
| `socket_addr_check` | policy hook (ip, port, use) |
| `allow_ip_name_lookup` | policy |
| `http_check` | policy hook (scheme, authority) |
| `http_send` | hook: the transport of http |
| `terminals` | mask of `TERMINAL_*` |
| `user` | the pointer of whoever made the state; never touched by the library |
| `random_bytes` | hook |
| `insecure_random_bytes` | hook |
| `clock` | hook |
| `resolution_ns` | the resolutions |
| `sleep` | hook |
| `close_all` | closes what the state holds open, and closes the state (`closed`) |
| `closed` | read only: whether `close_all()` was called; a closed state has no live handle, makes none, and every descriptor of 0.1 is BADF |
| `_slots`, `_p1` | private |

`fusedwasi.p2.Guest`:

| Member | Purpose |
| --- | --- |
| `state` | the world |
| `realloc` | hook |
| `wait` | hook |
| `trap` | hook |
| `exit` | hook |
| `signal` | hook |
| `exited` | the end of the last call |
| `exit_code` | valid when `exited` |
| `user` | the pointer of whoever made the guest |
| `_memories` | private |
