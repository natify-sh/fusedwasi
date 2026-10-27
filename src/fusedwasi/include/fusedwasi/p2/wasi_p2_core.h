/* The state of the world of WASI 0.2, the guest that calls into it, and the helpers of the canonical ABI that every package uses (the twin of p2/wasi_p2_core.py).
 *
 * Header-only, every function static (FUSEDWASI_P2_FN). No mutable global and no thread-local. Everything that changes is in a `fusedwasi_p2_state` (the world: the configuration,
 * the tables of handles, the policies, the clocks) or in a `fusedwasi_p2_guest` (the caller: its memories, its trap and `cabi_realloc`, the end of its call). The first argument of
 * every function of the packages is the guest, `g`; its state is `g->state`. Many guests may point to one state. A memory is a base and a size; a module fused from components has
 * one for each component, and a function takes the number of its own.
 * Alpha: the interface is the two structs and one function per import. Memory is written byte by byte, little-endian.
 * Integers are unsigned (`uint32_t`, `uint64_t`) in parameters and results, as in the Python twin. An engine whose values are signed converts them at the call (a cast to the unsigned type of the width).
 * POSIX clocks need `clock_gettime` (a GNU or POSIX mode of the compiler). */
#ifndef FUSEDWASI_P2_CORE_H
#define FUSEDWASI_P2_CORE_H

#include <stdint.h>

#ifdef _WIN32
typedef intptr_t fusedwasi_p2_ssize; /* a signed size, the same on every system (MSVC has no ssize_t) */
#else
#include <sys/types.h>
typedef ssize_t fusedwasi_p2_ssize;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN /* without winsock.h, which winsock2.h (wasi_p2_net.h) does not go with */
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt") /* elsewhere: -lbcrypt */
#endif
#include <bcrypt.h>
#else
#include <time.h>
#include <unistd.h>
#if defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define FUSEDWASI_P2_HAVE_GETENTROPY 1
#endif
#endif
#endif
#ifndef FUSEDWASI_P2_HAVE_GETENTROPY
#define FUSEDWASI_P2_HAVE_GETENTROPY 0
#endif

struct fusedwasi_p2_state;
struct fusedwasi_p2_guest;
struct fusedwasi_p2_stream;
struct fusedwasi_p2_http_request;
struct fusedwasi_p2_http_response;
struct fusedwasi_p2_http_error;

/* the `use` that `socket_addr_check` is asked about; the numbers are part of the interface (the UDP ones come last), and the Python twin has the same ones (`USE_TCP_BIND`, ...) */
enum { FUSEDWASI_P2_USE_TCP_BIND, FUSEDWASI_P2_USE_TCP_CONNECT, FUSEDWASI_P2_USE_UDP_BIND, FUSEDWASI_P2_USE_UDP_CONNECT, FUSEDWASI_P2_USE_UDP_OUTGOING };

/* "tcp-bind", "tcp-connect", "udp-bind", "udp-connect", "udp-outgoing"; "unknown" for any other number (`use_name` of the Python twin) */
static inline const char *fusedwasi_p2_use_name(int use) {
    static const char *const names[] = {"tcp-bind", "tcp-connect", "udp-bind", "udp-connect", "udp-outgoing"};
    return use >= 0 && use < 5 ? names[use] : "unknown";
}

/* a directory of the host that the guest sees under `name`; `writable` 0: the guest can only read it. The same struct is the preopen of the state of 0.1 (`fusedwasi_p1_preopen`), and the
   tuple `(path, name, writable)` in Python */
struct fusedwasi_p2_preopen {
    const char *path, *name;
    int writable;
};

/* every function is static, and `inline` so that the ones a module does not use do not warn */
#define FUSEDWASI_P2_FN static inline

#define FUSEDWASI_P2_MAX_MEMORIES 8
#define FUSEDWASI_P2_INDEX_BITS 20 /* a handle is (generation << INDEX_BITS) | index; index 0 is never given out */
#define FUSEDWASI_P2_INDEX_MASK ((1u << FUSEDWASI_P2_INDEX_BITS) - 1)
#define FUSEDWASI_P2_GENERATION_MASK ((1u << (32 - FUSEDWASI_P2_INDEX_BITS)) - 1)

enum { FUSEDWASI_P2_KIND_POLLABLE = 1, FUSEDWASI_P2_KIND_ERROR = 2, FUSEDWASI_P2_KIND_INPUT_STREAM = 3, FUSEDWASI_P2_KIND_OUTPUT_STREAM = 4,
       FUSEDWASI_P2_KIND_TERMINAL_INPUT = 5, FUSEDWASI_P2_KIND_TERMINAL_OUTPUT = 6,
       FUSEDWASI_P2_KIND_DESCRIPTOR = 7, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM = 8,
       FUSEDWASI_P2_KIND_NETWORK = 9, FUSEDWASI_P2_KIND_TCP_SOCKET = 10, FUSEDWASI_P2_KIND_UDP_SOCKET = 11, FUSEDWASI_P2_KIND_INCOMING_DATAGRAM_STREAM = 12,
       FUSEDWASI_P2_KIND_OUTGOING_DATAGRAM_STREAM = 13, FUSEDWASI_P2_KIND_RESOLVE_STREAM = 14,
       FUSEDWASI_P2_KIND_FIELDS = 15, FUSEDWASI_P2_KIND_INCOMING_REQUEST = 16, FUSEDWASI_P2_KIND_OUTGOING_REQUEST = 17, FUSEDWASI_P2_KIND_REQUEST_OPTIONS = 18,
       FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM = 19, FUSEDWASI_P2_KIND_INCOMING_RESPONSE = 20, FUSEDWASI_P2_KIND_INCOMING_BODY = 21, FUSEDWASI_P2_KIND_FUTURE_TRAILERS = 22,
       FUSEDWASI_P2_KIND_OUTGOING_RESPONSE = 23, FUSEDWASI_P2_KIND_OUTGOING_BODY = 24, FUSEDWASI_P2_KIND_FUTURE_INCOMING_RESPONSE = 25,
       FUSEDWASI_P2_KIND_P1_FDS = 26 /* the descriptors of WASI 0.1, private (p1/wasi_p1_core.h): a slot so that the state frees them with the rest */ };

enum { FUSEDWASI_P2_TERMINAL_STDIN = 1, FUSEDWASI_P2_TERMINAL_STDOUT = 2, FUSEDWASI_P2_TERMINAL_STDERR = 4 };

/* A memory is read through two pointers the consumer owns (the base moves when the module grows its memory; the library keeps no copy of either).
   `fusedwasi_p2_memory_cell` is the storage that `fusedwasi_p2_set_memory` writes to; `fusedwasi_p2_bind_memory` points elsewhere. */
typedef struct {
    uint8_t **data;
    uint64_t *size;
} fusedwasi_p2_memory;

typedef struct {
    uint8_t *base;
    uint64_t size;
} fusedwasi_p2_memory_cell;

/* Something a guest can wait for; wasi:clocks and wasi:io make the kinds. `ctx` is the owner's, `deadline` is for the clocks. An object of the world: `ready` takes the state, never traps;
   it returns 1 (ready), 0 (not yet), or -1 when it cannot tell, with the reason in `failure` (a C string that outlives the call): the WASI function that asked has the guest and traps with it. */
typedef struct fusedwasi_p2_pollable {
    int (*ready)(struct fusedwasi_p2_state *state, struct fusedwasi_p2_pollable *p);
    void *ctx;
    uint64_t deadline;
    const char *failure;
} fusedwasi_p2_pollable;

typedef struct {
    uint32_t generation;
    uint32_t kind;
    int live;
    void *obj;
    void (*destroy)(struct fusedwasi_p2_state *state, void *obj); /* called when the handle is dropped, or the state is freed (no guest then); may be NULL */
} fusedwasi_p2_slot;

/* The STATE is the world: what the guests share (a Wasmtime `WasiCtx` and its table). One for all the guests that share files and sockets; made once and freed once by whoever owns it.
   The GUEST (below) is the caller: its memory, its trap, its `cabi_realloc`, the end of its call. Many guests may point to one state; nothing is rebound per call.
   The hooks of the state (the sources of time and random bytes, the policies of the network, the transport of http) take the state; the hooks of the guest take the guest. */
typedef struct fusedwasi_p2_state {
    fusedwasi_p2_slot *slots; /* slot 0 is never used */
    uint32_t slot_count, slot_capacity;
    /* wasi:cli/environment: owned by the caller, they must outlive the state */
    const char *const *args;
    uint32_t arg_count;
    const char *const *env_names, *const *env_values;
    uint32_t env_count;
    const char *initial_cwd; /* NULL: none */
    /* wasi:filesystem/preopens: owned by the caller */
    const struct fusedwasi_p2_preopen *preopens;
    uint32_t preopen_count;
    /* wasi:sockets: nothing is allowed unless the caller says so, as in Wasmtime. `socket_addr_check` decides for each bind or connect (`use` is a FUSEDWASI_P2_USE_*),
       `allow_ip_name_lookup` for resolve-addresses. `ip` is the address as text. NULL: everything is refused. */
    int (*socket_addr_check)(struct fusedwasi_p2_state *state, const char *ip, uint16_t port, int use);
    int allow_ip_name_lookup;
    /* wasi:http: an outgoing request is refused (`HTTP-request-denied`) unless `http_check` says yes (`scheme` is "http" or "https"; NULL: everything is refused).
       `http_send` replaces the transport of wasi_p2_http.h: returns 0 with `out` filled, or nonzero with `err` filled. */
    int (*http_check)(struct fusedwasi_p2_state *state, const char *scheme, const char *authority);
    int (*http_send)(struct fusedwasi_p2_state *state, const struct fusedwasi_p2_http_request *req, struct fusedwasi_p2_http_response *out, struct fusedwasi_p2_http_error *err);
    /* the standard streams (wasi_p2_io.h): [0] stdin, [1] stdout, [2] stderr; NULL means the descriptors of the process, made when first asked for (`std_default`: private,
       freed with the state). The streams of the caller are not closed when a handle to them is dropped. */
    struct fusedwasi_p2_stream *std[3];
    void *std_default[3]; /* private */
    unsigned terminals; /* FUSEDWASI_P2_TERMINAL_*: which of the three are terminals */
    /* random bytes: returns 0, or nonzero if the source failed (the guest traps). `insecure_random_bytes` is the same source unless replaced. */
    int (*random_bytes)(struct fusedwasi_p2_state *state, uint8_t *buf, size_t n);
    int (*insecure_random_bytes)(struct fusedwasi_p2_state *state, uint8_t *buf, size_t n);
    /* the clocks of WASI 0.1 and 0.2, in nanoseconds: `id` is 0 real time, 1 monotonic, 2 CPU time of the process, 3 CPU time of the thread. Returns 0, or nonzero if the clock
       cannot be read (0.2 traps, 0.1 answers `notsup`). Replaced in tests and by hosts. `resolution_ns` is the resolution of each. */
    int (*clock)(struct fusedwasi_p2_state *state, uint32_t id, uint64_t *ns);
    uint64_t resolution_ns[4];
    /* how a call of 0.1 that sleeps (`poll_oneoff` on a clock) waits `ns` nanoseconds; returns 0, or nonzero if interrupted (the guest traps) */
    int (*sleep)(struct fusedwasi_p2_state *state, uint64_t ns);
    void *user; /* the pointer of whoever made the state, given back to its hooks: the library never reads, writes or frees it (`fusedwasi_p2_init` makes it NULL); `State.user` in Python */
    uint32_t p1_table; /* private: the handle of the descriptors of WASI 0.1, made by the first function of 0.1 (p1/wasi_p1_core.h); 0 until then */
} fusedwasi_p2_state;

/* The caller: one for each instance, made once (`fusedwasi_p2_guest_init`), never rewritten per call. */
typedef struct fusedwasi_p2_guest {
    fusedwasi_p2_state *state; /* the world this guest calls into */
    fusedwasi_p2_memory memory[FUSEDWASI_P2_MAX_MEMORIES]; /* NULL pointers: never bound; `fusedwasi_p2_bind_memory`, `fusedwasi_p2_set_memory` */
    fusedwasi_p2_memory_cell memory_cell[FUSEDWASI_P2_MAX_MEMORIES]; /* the cells of `fusedwasi_p2_set_memory`, owned by the guest */
    uint32_t memories;
    /* a trap does not return; a host jumps out of the call (a longjmp, or what its engine has); the default aborts */
    void (*trap)(struct fusedwasi_p2_guest *g, const char *message);
    /* the `cabi_realloc` of component `comp`: returns the pointer, or 0 for out of memory */
    uint32_t (*realloc)(struct fusedwasi_p2_guest *g, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size);
    /* what a blocking call does between two looks at a pollable; it can trap: it is where the caller may be interrupted */
    void (*wait)(struct fusedwasi_p2_guest *g);
    /* The end of a call is what one call leaves, so it is the guest's. The guest called wasi:cli/exit: `exit` is told, and by default records the code and traps; the code is in
       `exit_code` when `exited`. (`proc_exit` of 0.1 is this same exit.) The host clears `exited` before the next call. */
    void (*exit)(struct fusedwasi_p2_guest *g, uint32_t code);
    /* `proc_raise` of WASI 0.1 asks it, with the signal number (1 to 30). It returns the errno of WASI for the call, or ends the program by its own means (a trap, a longjmp). NULL (the default): NOTSUP, the program goes on. */
    uint32_t (*signal)(struct fusedwasi_p2_guest *g, uint32_t number);
    int exited;
    uint32_t exit_code;
    void *user; /* the pointer of whoever made the guest, given back to its hooks: the library never reads, writes or frees it (`fusedwasi_p2_guest_init` makes it NULL) */
} fusedwasi_p2_guest;

FUSEDWASI_P2_FN void fusedwasi_p2_default_trap(fusedwasi_p2_guest *g, const char *message) {
    (void)g;
    fprintf(stderr, "fusedwasi p2 trap: %s\n", message);
    abort();
}

#ifdef _WIN32
FUSEDWASI_P2_FN uint64_t fusedwasi_p2_filetime_ns(FILETIME t) { return (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime) * 100u; }

FUSEDWASI_P2_FN int fusedwasi_p2_default_clock(fusedwasi_p2_state *state, uint32_t id, uint64_t *ns) {
    FILETIME created, exited, kernel, user, now;
    LARGE_INTEGER counter, frequency;
    (void)state;
    switch (id) {
    case 0:
        GetSystemTimePreciseAsFileTime(&now);
        *ns = fusedwasi_p2_filetime_ns(now) - 11644473600ull * 1000000000ull; /* from 1601 to 1970 */
        return 0;
    case 1:
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) return -1;
        *ns = (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000ull + (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000000ull / (uint64_t)frequency.QuadPart;
        return 0;
    case 2:
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
        *ns = fusedwasi_p2_filetime_ns(kernel) + fusedwasi_p2_filetime_ns(user);
        return 0;
    case 3:
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
        *ns = fusedwasi_p2_filetime_ns(kernel) + fusedwasi_p2_filetime_ns(user);
        return 0;
    default: return -1;
    }
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_default_resolution_ns(uint32_t id) {
    LARGE_INTEGER frequency;
    if (id == 1 && QueryPerformanceFrequency(&frequency)) return (1000000000ull + (uint64_t)frequency.QuadPart - 1) / (uint64_t)frequency.QuadPart;
    return 100; /* the unit of a FILETIME */
}
#else
FUSEDWASI_P2_FN int fusedwasi_p2_default_clock(fusedwasi_p2_state *state, uint32_t id, uint64_t *ns) {
    static const clockid_t ids[4] = {CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID};
    struct timespec ts;
    (void)state;
    if (id > 3 || clock_gettime(ids[id], &ts) != 0) return -1;
    *ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return 0;
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_default_resolution_ns(uint32_t id) {
    static const clockid_t ids[4] = {CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID};
    struct timespec ts;
    if (id > 3 || clock_getres(ids[id], &ts) != 0) return 1;
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return ns ? ns : 1;
}
#endif

/* the time of clock `id`: a clock of the host that cannot be read is a trap (0.2 has no error for it) */
FUSEDWASI_P2_FN uint64_t fusedwasi_p2_clock_ns(fusedwasi_p2_guest *g, uint32_t id) {
    uint64_t ns = 0;
    if (g->state->clock(g->state, id, &ns) != 0) g->trap(g, "the clock cannot be read");
    return ns;
}

/* waits `ns` nanoseconds on the monotonic clock; in slices, so that a host sees an interrupt between them */
FUSEDWASI_P2_FN int fusedwasi_p2_default_sleep(fusedwasi_p2_state *state, uint64_t ns) {
    uint64_t start, now;
    if (state->clock(state, 1, &start) != 0) return -1;
    for (;;) {
        if (state->clock(state, 1, &now) != 0 || now - start >= ns) return 0;
        uint64_t left = ns - (now - start), slice = left < 100000000ull ? left : 100000000ull;
#ifdef _WIN32
        Sleep((DWORD)((slice + 999999u) / 1000000u));
#else
        struct timespec t = {(time_t)(slice / 1000000000ull), (long)(slice % 1000000000ull)};
        nanosleep(&t, NULL);
#endif
    }
}

FUSEDWASI_P2_FN int fusedwasi_p2_default_random_bytes(fusedwasi_p2_state *state, uint8_t *buf, size_t n) {
    (void)state;
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0;
#elif FUSEDWASI_P2_HAVE_GETENTROPY
    /* one system call for each 256 bytes, with no file to open: the source is called for each `get-random-bytes` of a guest */
    while (n > 0) {
        size_t chunk = n < 256 ? n : 256;
        if (getentropy(buf, chunk) != 0) return 1;
        buf += chunk;
        n -= chunk;
    }
    return 0;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return 1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got != n;
#endif
}

FUSEDWASI_P2_FN void fusedwasi_p2_default_exit(fusedwasi_p2_guest *g, uint32_t code) {
    g->exited = 1;
    g->exit_code = code;
    g->trap(g, "exit");
}

FUSEDWASI_P2_FN void fusedwasi_p2_default_wait(fusedwasi_p2_guest *g) {
    (void)g;
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec ts = {0, 1000000};
    nanosleep(&ts, NULL);
#endif
}

/* the state: all zero, with the system's own clocks, random source and sleep; the standard streams are the descriptors of the process until the caller gives others */
FUSEDWASI_P2_FN void fusedwasi_p2_init(fusedwasi_p2_state *state) {
    memset(state, 0, sizeof *state);
    state->random_bytes = state->insecure_random_bytes = fusedwasi_p2_default_random_bytes;
    state->clock = fusedwasi_p2_default_clock;
    state->sleep = fusedwasi_p2_default_sleep;
    for (uint32_t i = 0; i < 4; i++) state->resolution_ns[i] = fusedwasi_p2_default_resolution_ns(i);
}

/* a guest over `state`: all zero, with the default trap (aborts), wait and exit. It owns nothing, so it needs no free; it can be made on the stack for one call, or once for an instance. */
FUSEDWASI_P2_FN void fusedwasi_p2_guest_init(fusedwasi_p2_guest *g, fusedwasi_p2_state *state) {
    memset(g, 0, sizeof *g);
    g->state = state;
    g->trap = fusedwasi_p2_default_trap;
    g->wait = fusedwasi_p2_default_wait;
    g->exit = fusedwasi_p2_default_exit;
}

/* frees what the state holds: every live handle is destroyed (there is no guest then), and the table. The guests that point to it must not be used afterwards. */
FUSEDWASI_P2_FN void fusedwasi_p2_free(fusedwasi_p2_state *state) {
    for (uint32_t i = 1; i < state->slot_count; i++)
        if (state->slots[i].live) { /* not live while it is destroyed: a destroy that drops other handles (the descriptors of 0.1) finds the ones already gone */
            state->slots[i].live = 0;
            if (state->slots[i].destroy) state->slots[i].destroy(state, state->slots[i].obj);
        }
    for (int i = 0; i < 3; i++) free(state->std_default[i]), state->std_default[i] = NULL;
    free(state->slots);
    state->slots = NULL;
    state->slot_count = state->slot_capacity = 0;
}

/* Binds memory number `comp` by pointer: `*data` is the base and `*size` the size in bytes, both owned by the caller and read at each access, so a base that moves
   (memory.grow) is followed with no second call. The pointers must stay valid while the state is used. Returns 0, or -1 if the number is too big. */
FUSEDWASI_P2_FN int fusedwasi_p2_bind_memory(fusedwasi_p2_guest *g, uint32_t comp, uint8_t **data, uint64_t *size) {
    if (comp >= FUSEDWASI_P2_MAX_MEMORIES) return -1;
    g->memory[comp].data = data;
    g->memory[comp].size = size;
    if (comp >= g->memories) g->memories = comp + 1;
    return 0;
}

/* The convenience: stores `base` and `size` in cells of the state and binds the memory to them (a base that moves: call it again, or use `fusedwasi_p2_bind_memory`).
   Returns 0, or -1 if the number is too big. */
FUSEDWASI_P2_FN int fusedwasi_p2_set_memory(fusedwasi_p2_guest *g, uint32_t comp, uint8_t *base, uint64_t size) {
    if (comp >= FUSEDWASI_P2_MAX_MEMORIES) return -1;
    g->memory_cell[comp].base = base;
    g->memory_cell[comp].size = size;
    return fusedwasi_p2_bind_memory(g, comp, &g->memory_cell[comp].base, &g->memory_cell[comp].size);
}

/* the table of handles (own and borrow share it; a handle is an i32 for the guest) */

/* Adds an object to the table of the state (no guest needed: the streams of a state are made while it is configured). Returns the handle, or 0 when the table is full or there is no memory;
   nothing is changed then. */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_table_add(fusedwasi_p2_state *state, uint32_t kind, void *obj, void (*destroy)(fusedwasi_p2_state *, void *)) {
    uint32_t index = 0;
    for (uint32_t i = 1; i < state->slot_count; i++)
        if (!state->slots[i].live) {
            index = i;
            break;
        }
    if (!index) {
        uint32_t used = state->slot_count ? state->slot_count : 1; /* slot 0 */
        if (used > FUSEDWASI_P2_INDEX_MASK) return 0;
        if (used >= state->slot_capacity) {
            uint32_t cap = state->slot_capacity ? state->slot_capacity * 2 : 16;
            fusedwasi_p2_slot *grown = (fusedwasi_p2_slot *)realloc(state->slots, cap * sizeof *grown);
            if (!grown) return 0;
            memset(grown + state->slot_capacity, 0, (cap - state->slot_capacity) * sizeof *grown);
            state->slots = grown;
            state->slot_capacity = cap;
        }
        state->slot_count = used + 1;
        index = used;
    }
    fusedwasi_p2_slot *slot = &state->slots[index];
    slot->generation = (slot->generation + 1) & FUSEDWASI_P2_GENERATION_MASK;
    slot->kind = kind;
    slot->live = 1;
    slot->obj = obj;
    slot->destroy = destroy;
    return (slot->generation << FUSEDWASI_P2_INDEX_BITS) | index;
}

/* The same for a function that has the guest: a full table or no memory is a trap. */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_table_new(fusedwasi_p2_guest *g, uint32_t kind, void *obj, void (*destroy)(fusedwasi_p2_state *, void *)) {
    uint32_t handle = fusedwasi_p2_table_add(g->state, kind, obj, destroy);
    if (!handle) {
        if (g->state->slot_count > FUSEDWASI_P2_INDEX_MASK) g->trap(g, "table of handles is full");
        g->trap(g, "out of memory");
    }
    return handle;
}

/* For a function that has the guest, after a constructor of the state that gave 0 for a full table or no memory: the trap. */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_handle_or_trap(fusedwasi_p2_guest *g, uint32_t handle) {
    if (!handle) g->trap(g, g->state->slot_count > FUSEDWASI_P2_INDEX_MASK ? "table of handles is full" : "out of memory");
    return handle;
}

/* The slot of a live handle of the given kind, or NULL for anything else. For whoever holds a handle itself (the adapter of WASI 0.1) and answers an error code, not a trap. */
FUSEDWASI_P2_FN fusedwasi_p2_slot *fusedwasi_p2_table_find(fusedwasi_p2_state *state, uint32_t handle, uint32_t kind) {
    uint32_t index = handle & FUSEDWASI_P2_INDEX_MASK;
    if (index == 0 || index >= state->slot_count) return NULL;
    fusedwasi_p2_slot *slot = &state->slots[index];
    if (!slot->live || slot->generation != (handle >> FUSEDWASI_P2_INDEX_BITS) || slot->kind != kind) return NULL;
    return slot;
}

/* The slot of a live handle of the given kind; anything else is a trap. */
FUSEDWASI_P2_FN fusedwasi_p2_slot *fusedwasi_p2_table_get(fusedwasi_p2_guest *g, uint32_t handle, uint32_t kind) {
    fusedwasi_p2_slot *slot = fusedwasi_p2_table_find(g->state, handle, kind);
    if (!slot) g->trap(g, "bad handle");
    return slot;
}

/* The object of a live handle of the given kind; the handle is spent and `destroy` is not called (the caller owns the object now). */
FUSEDWASI_P2_FN void *fusedwasi_p2_table_take(fusedwasi_p2_guest *g, uint32_t handle, uint32_t kind) {
    fusedwasi_p2_slot *slot = fusedwasi_p2_table_get(g, handle, kind);
    void *obj = slot->obj;
    slot->live = 0;
    slot->obj = NULL;
    return obj;
}

FUSEDWASI_P2_FN int fusedwasi_p2_table_alive(fusedwasi_p2_guest *g, uint32_t handle, uint32_t kind) {
    uint32_t index = handle & FUSEDWASI_P2_INDEX_MASK;
    if (index == 0 || index >= g->state->slot_count) return 0;
    fusedwasi_p2_slot *slot = &g->state->slots[index];
    return slot->live && slot->generation == (handle >> FUSEDWASI_P2_INDEX_BITS) && slot->kind == kind;
}

FUSEDWASI_P2_FN void fusedwasi_p2_table_drop(fusedwasi_p2_guest *g, uint32_t handle, uint32_t kind) {
    fusedwasi_p2_slot *slot = fusedwasi_p2_table_get(g, handle, kind);
    slot->live = 0;
    if (slot->destroy) slot->destroy(g->state, slot->obj);
    slot->obj = NULL;
}

/* memory: little-endian, checked */

FUSEDWASI_P2_FN uint8_t *fusedwasi_p2_mem(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint32_t size) {
    if (comp >= g->memories || !g->memory[comp].data || !g->memory[comp].size) g->trap(g, "no such memory");
    if ((uint64_t)ptr + size > *g->memory[comp].size) g->trap(g, "pointer outside the memory");
    return *g->memory[comp].data + ptr;
}

FUSEDWASI_P2_FN void fusedwasi_p2_store_u32(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint32_t v) {
    uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, 4);
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

FUSEDWASI_P2_FN void fusedwasi_p2_store_u64(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint64_t v) {
    uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, 8);
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

FUSEDWASI_P2_FN void fusedwasi_p2_store_u8(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint8_t v) { *fusedwasi_p2_mem(g, comp, ptr, 1) = v; }

FUSEDWASI_P2_FN void fusedwasi_p2_store_u16(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint16_t v) {
    uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, 2);
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8);
}

FUSEDWASI_P2_FN uint8_t fusedwasi_p2_load_u8(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr) { return *fusedwasi_p2_mem(g, comp, ptr, 1); }

FUSEDWASI_P2_FN uint16_t fusedwasi_p2_load_u16(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr) {
    const uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, 2);
    return (uint16_t)(p[0] | p[1] << 8);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_load_u32(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr) {
    const uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, 4);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Memory from the component for a list or a string that the host returns (`cabi_realloc`). */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_alloc(fusedwasi_p2_guest *g, uint32_t comp, uint32_t align, uint32_t size) {
    /* The canonical ABI calls `realloc(0, 0, align, size)` also for an empty list or string (a component that counts its allocations, such as the adapter of WASI 0.1 of
       Wasmtime, relies on it); a host that has no `realloc` can still answer an empty result, as it could before. A size of 0 may come back as 0 (a component may give no
       address for nothing): the alignment stands for it. */
    if (!g->realloc) {
        if (size == 0) return align;
        g->trap(g, "the component has no cabi_realloc");
    }
    uint32_t ptr = g->realloc(g, comp, 0, 0, align, size);
    if (!ptr && size) g->trap(g, "cabi_realloc failed");
    return ptr ? ptr : align;
}

FUSEDWASI_P2_FN void fusedwasi_p2_string(fusedwasi_p2_guest *g, uint32_t comp, const char *text, uint32_t at) {
    uint32_t n = (uint32_t)strlen(text);
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 1, n);
    if (n) memcpy(fusedwasi_p2_mem(g, comp, ptr, n), text, n);
    fusedwasi_p2_store_u32(g, comp, at, ptr);
    fusedwasi_p2_store_u32(g, comp, at + 4, n);
}

#endif /* FUSEDWASI_P2_CORE_H */
