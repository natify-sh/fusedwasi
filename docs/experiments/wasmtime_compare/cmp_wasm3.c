/* The C host of fusedwasi for harness.py: the poke / peek / run protocol over a pipe.

   It is the neutral user of tests/neutral/c_wasm3/neutral_wasm3.c (wasm3 runs the module; every import is a function of the public headers of fusedwasi, linked through
   the glue that gen_wasm3_glue.py writes from `fusedwasi.signatures`), with a main of its own:

       cmp_wasm3 --world p1|p2 [--arg A]... [--env NAME=VALUE]... [--dir HOST::GUEST]... module.wasm

   then it reads requests on the standard input and answers on the standard output (all integers little-endian u32); the output of the guest is NOT the standard output, it is kept
   and given back by 'O':

       'P' addr len bytes   -> 0                      write bytes into the memory of the guest
       'K' addr len         -> 0 bytes                read bytes from it
       'R' op               -> 0 | 1 code | 2 len text   call export `run(op)`: ended normally | the guest exited with code | trapped
       'O'                  -> 0 len bytes            what the guest wrote to its standard output
       a request that cannot be served (an address outside the memory) is answered 9. */

#define main neutral_wasm3_main
#include "neutral_wasm3.c"
#undef main

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

typedef struct {
    fusedwasi_p2_stream base;
    uint8_t *buf;
    uint64_t len, capacity;
} cmp_out;

static uint64_t cmp_out_check(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, int *status) {
    cmp_out *o = (cmp_out *)st;
    (void)state;
    *status = FUSEDWASI_P2_STREAM_OK;
    return (1u << 20) - o->len; /* the same limit as BufferOutput(1 << 20) of the Python side */
}
static int cmp_out_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    cmp_out *o = (cmp_out *)st;
    (void)state;
    if (o->len + len > o->capacity) {
        uint64_t cap = o->capacity ? o->capacity : 64;
        while (cap < o->len + len) cap *= 2;
        o->buf = (uint8_t *)realloc(o->buf, (size_t)cap);
        o->capacity = cap;
    }
    if (len) memcpy(o->buf + o->len, data, (size_t)len);
    o->len += len;
    return FUSEDWASI_P2_STREAM_OK;
}
static int cmp_out_flush(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state, (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}
static int cmp_ready(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state, (void)st;
    return 1;
}
static int cmp_in_read(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    (void)state, (void)st, (void)buf;
    *got = 0;
    return len ? FUSEDWASI_P2_STREAM_CLOSED : FUSEDWASI_P2_STREAM_OK; /* an empty input, as MemoryInput(b"") */
}

static int get(void *p, size_t n) { return fread(p, 1, n, stdin) == n; }
static void put(const void *p, size_t n) { fwrite(p, 1, n, stdout); }
static void reply(uint8_t status) { put(&status, 1); }

int main(int argc, char **argv) {
    const char *world = "p1";
    const char *args[64], *env_names[64], *env_values[64];
    struct fusedwasi_p2_preopen preopens[8];
    uint32_t arg_count = 0, env_count = 0, preopen_count = 0;
    const char *module_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--world") && i + 1 < argc) world = argv[++i];
        else if (!strcmp(argv[i], "--arg") && i + 1 < argc) args[arg_count++] = argv[++i];
        else if (!strcmp(argv[i], "--env") && i + 1 < argc) {
            char *pair = strdup(argv[++i]);
            char *eq = strchr(pair, '=');
            *eq = 0;
            env_names[env_count] = pair, env_values[env_count++] = eq + 1;
        } else if (!strcmp(argv[i], "--dir") && i + 1 < argc) {
            char *pair = strdup(argv[++i]);
            char *sep = strstr(pair, "::");
            *sep = 0;
            preopens[preopen_count].path = pair, preopens[preopen_count].name = sep + 2, preopens[preopen_count++].writable = 1;
        } else module_path = argv[i];
    }
    if (!module_path) return 2;
#ifdef _WIN32
    _setmode(0, _O_BINARY), _setmode(1, _O_BINARY);
#endif
    int p1 = !strcmp(world, "p1");

    cmp_out out;
    memset(&out, 0, sizeof out);
    out.base.check_write = cmp_out_check, out.base.write = cmp_out_write, out.base.flush = cmp_out_flush, out.base.ready = cmp_ready;
    fusedwasi_p2_stream in_stream;
    memset(&in_stream, 0, sizeof in_stream);
    in_stream.read = cmp_in_read, in_stream.ready = cmp_ready;

    fusedwasi_p2_state state;
    fusedwasi_p2_init(&state);
    state.args = args, state.arg_count = arg_count;
    state.env_names = env_names, state.env_values = env_values, state.env_count = env_count;
    state.preopens = preopens, state.preopen_count = preopen_count;
    state.std[0] = &in_stream, state.std[1] = &out.base;
    if (p1) {
        int err = 0;
        size_t bad = 0;
        if (fusedwasi_p1_open_descriptors(&state, &err, &bad)) return 2;
    }

    neutral_inst *inst = (neutral_inst *)calloc(1, sizeof *inst);
    inst->wasm = read_file(module_path, &inst->wasm_size);
    if (!inst->wasm) return 2;
    IM3Environment environment = m3_NewEnvironment();
    inst->rt = m3_NewRuntime(environment, 64 * 1024, inst);
    IM3Module module;
    M3Result e = m3_ParseModule(environment, &module, inst->wasm, (uint32_t)inst->wasm_size);
    if (!e) e = m3_LoadModule(inst->rt, module);
    if (e) {
        fprintf(stderr, "%s: %s\n", module_path, e);
        return 2;
    }
    if (neutral_link(module, inst->wasm, inst->wasm_size, inst)) return 2;
    fusedwasi_p2_guest_init(&inst->g, &state);
    inst->g.user = inst;
    inst->g.trap = neutral_trap;
    inst->g.realloc = neutral_realloc;
    fusedwasi_p2_bind_memory(&inst->g, 0, &inst->base, &inst->size);
    if (m3_FindFunction(&inst->realloc_function, inst->rt, "cabi_realloc")) inst->realloc_function = NULL;
    IM3Function run;
    if (m3_FindFunction(&run, inst->rt, "run")) return 2;

    for (;;) {
        uint8_t tag;
        if (!get(&tag, 1)) break;
        neutral_sync(inst, inst->rt);
        if (tag == 'P' || tag == 'K') {
            uint32_t addr, len;
            if (!get(&addr, 4) || !get(&len, 4)) break;
            uint8_t *data = (uint8_t *)malloc(len ? len : 1);
            if (tag == 'P' && !get(data, len)) break;
            if ((uint64_t)addr + len > inst->size) {
                reply(9);
            } else if (tag == 'P') {
                memcpy(inst->base + addr, data, len);
                reply(0);
            } else {
                reply(0);
                put(inst->base + addr, len);
            }
            free(data);
        } else if (tag == 'R') {
            uint32_t op;
            if (!get(&op, 4)) break;
            inst->g.exited = 0, inst->g.exit_code = 0;
            inst->trap_text[0] = 0;
            const void *arguments[1] = {&op};
            e = m3_Call(run, 1, arguments);
            if (inst->g.exited) {
                reply(1);
                put(&inst->g.exit_code, 4);
            } else if (e) {
                uint32_t n = (uint32_t)strlen(e);
                reply(2);
                put(&n, 4);
                put(e, n);
            } else {
                reply(0);
            }
        } else if (tag == 'O') {
            uint32_t n = (uint32_t)out.len;
            reply(0);
            put(&n, 4);
            put(out.buf, n);
        } else {
            reply(9);
        }
        fflush(stdout);
    }
    return 0;
}
