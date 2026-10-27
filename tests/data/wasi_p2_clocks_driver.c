/* The scenario of tests/test_wasi_p2_clocks.py, in C: fixed clocks, a fake memory, the output is compared with the one of the Python host. */
#include <inttypes.h>
#include "wasi_p2_clocks.h"

static int clock_of(fusedwasi_p2_state *s, uint32_t id, uint64_t *ns) {
    (void)s;
    *ns = id == 0 ? 1700000000123456789ULL : 5000;
    return 0;
}
static void trap(fusedwasi_p2_guest *s, const char *m) { (void)s; printf("TRAP %s\n", m); exit(0); }

static void dump(const uint8_t *m, int n) {
    for (int i = 0; i < n; i++) printf("%02x", m[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "main";
    uint8_t memory[64] = {0};
    fusedwasi_p2_state world;
    fusedwasi_p2_guest s;
    fusedwasi_p2_init(&world);
    fusedwasi_p2_guest_init(&s, &world);
    s.trap = trap;
    world.clock = clock_of;
    world.resolution_ns[0] = 1000;
    world.resolution_ns[1] = 7;
    fusedwasi_p2_set_memory(&s, 0, memory, sizeof memory);
    if (!strcmp(scenario, "main")) {
        fusedwasi_wasi_clocks_wall_clock_now(&s, 0, 0);
        fusedwasi_wasi_clocks_wall_clock_resolution(&s, 0, 16);
        dump(memory, 32);
        printf("%" PRIu64 " %" PRIu64 "\n", fusedwasi_wasi_clocks_monotonic_clock_now(&s), fusedwasi_wasi_clocks_monotonic_clock_resolution(&s));
        uint32_t a = fusedwasi_wasi_clocks_monotonic_clock_subscribe_duration(&s, 100);
        uint32_t b = fusedwasi_wasi_clocks_monotonic_clock_subscribe_instant(&s, 9000);
        uint32_t c = fusedwasi_wasi_clocks_monotonic_clock_subscribe_duration(&s, UINT64_MAX);
        printf("%u %u %u\n", a, b, c);
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n", ((fusedwasi_p2_pollable *)fusedwasi_p2_table_get(&s, a, FUSEDWASI_P2_KIND_POLLABLE)->obj)->deadline,
               ((fusedwasi_p2_pollable *)fusedwasi_p2_table_get(&s, b, FUSEDWASI_P2_KIND_POLLABLE)->obj)->deadline, ((fusedwasi_p2_pollable *)fusedwasi_p2_table_get(&s, c, FUSEDWASI_P2_KIND_POLLABLE)->obj)->deadline);
        fusedwasi_p2_table_drop(&s, a, FUSEDWASI_P2_KIND_POLLABLE);
        printf("%u\n", fusedwasi_wasi_clocks_monotonic_clock_subscribe_instant(&s, 1)); /* the slot of `a` again, another generation */
    } else if (!strcmp(scenario, "double_drop")) {
        uint32_t a = fusedwasi_wasi_clocks_monotonic_clock_subscribe_instant(&s, 1);
        fusedwasi_p2_table_drop(&s, a, FUSEDWASI_P2_KIND_POLLABLE);
        fusedwasi_p2_table_drop(&s, a, FUSEDWASI_P2_KIND_POLLABLE);
    } else if (!strcmp(scenario, "outside")) {
        fusedwasi_wasi_clocks_wall_clock_now(&s, 0, 60); /* 16 bytes do not fit at 60 of 64 */
    } else if (!strcmp(scenario, "no_memory")) {
        fusedwasi_wasi_clocks_wall_clock_now(&s, 1, 0);
    }
    fusedwasi_p2_free(&world);
    return 0;
}
