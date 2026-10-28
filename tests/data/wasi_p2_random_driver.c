/* The scenario of tests/test_wasi_p2_random.py, in C: sources that count bytes (secure from 0, insecure from 128), a fake memory. */
#include <inttypes.h>
#include "wasi_p2_random.h"

static void trap(fusedwasi_p2_guest *s, const char *m) { (void)s; printf("TRAP %s\n", m); exit(0); }

typedef struct {
    uint8_t secure, insecure; /* the counters of the two sources */
    uint32_t next;            /* the allocator */
    uint32_t calls;           /* how many times it was called */
} counters;

static int counting(fusedwasi_p2_state *s, uint8_t *buf, size_t n) {
    counters *c = (counters *)s->user;
    for (size_t i = 0; i < n; i++) buf[i] = c->secure++;
    return 0;
}
static int counting_insecure(fusedwasi_p2_state *s, uint8_t *buf, size_t n) {
    counters *c = (counters *)s->user;
    for (size_t i = 0; i < n; i++) buf[i] = c->insecure++;
    return 0;
}
static int failing(fusedwasi_p2_state *s, uint8_t *buf, size_t n) { (void)s, (void)buf, (void)n; return 1; }

static uint32_t bump(fusedwasi_p2_guest *s, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size) {
    counters *c = (counters *)s->user;
    (void)comp, (void)old_ptr, (void)old_size;
    c->calls++;
    uint32_t ptr = (c->next + align - 1) / align * align;
    c->next = ptr + new_size;
    return ptr;
}

static uint8_t memory[1024];

static void dump(const char *what) {
    printf("%s ", what);
    for (int i = 0; i < 128; i++) printf("%02x", memory[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "main";
    counters state = {0, 128, 64, 0};
    fusedwasi_p2_state world;
    fusedwasi_p2_guest s;
    fusedwasi_p2_init(&world);
    fusedwasi_p2_guest_init(&s, &world);
    s.trap = trap;
    s.user = &state;
    world.user = &state;
    s.realloc = bump;
    world.random_bytes = counting;
    world.insecure_random_bytes = counting_insecure;
    fusedwasi_p2_set_memory(&s, 0, memory, sizeof memory);
    if (!strcmp(scenario, "main")) {
        fusedwasi_wasi_random_random_get_random_bytes(&s, 0, 5, 0), dump("bytes");
        printf("u64 %" PRIu64 "\n", fusedwasi_wasi_random_random_get_random_u64(&s));
        fusedwasi_wasi_random_insecure_get_insecure_random_bytes(&s, 0, 3, 8), dump("insecure bytes");
        printf("insecure u64 %" PRIu64 "\n", fusedwasi_wasi_random_insecure_get_insecure_random_u64(&s));
        fusedwasi_wasi_random_insecure_seed_insecure_seed(&s, 0, 16), dump("seed");
        fusedwasi_wasi_random_random_get_random_bytes(&s, 0, 0, 32), dump("empty");
        printf("realloc calls %u\n", (unsigned)state.calls); /* also the empty list asked for memory */
    } else if (!strcmp(scenario, "default_source")) {
        /* the source of the system: no override; two calls give different bytes, and each is a system call and no file */
        world.random_bytes = fusedwasi_p2__default_random_bytes;
        uint64_t a = fusedwasi_wasi_random_random_get_random_u64(&s), b = fusedwasi_wasi_random_random_get_random_u64(&s);
        printf("distinct %d\n", a != b);
        fusedwasi_wasi_random_random_get_random_bytes(&s, 0, 100, 0);
        printf("bytes %u\n", (unsigned)fusedwasi_p2__load_u32(&s, 0, 4));
        fusedwasi_wasi_random_random_get_random_bytes(&s, 0, 600, 0); /* more than 256, which is one call of the system */
        printf("bytes %u\n", (unsigned)fusedwasi_p2__load_u32(&s, 0, 4));
    } else if (!strcmp(scenario, "too_many")) {
        fusedwasi_wasi_random_random_get_random_bytes(&s, 0, (uint64_t)1 << 40, 0);
    } else if (!strcmp(scenario, "failing")) {
        world.random_bytes = failing;
        fusedwasi_wasi_random_random_get_random_u64(&s);
    }
    fusedwasi_p2_free(&world);
    return 0;
}
