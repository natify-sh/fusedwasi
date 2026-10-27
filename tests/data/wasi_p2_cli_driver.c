/* The scenario of tests/test_wasi_p2_cli.py, in C: a fake memory, a bump allocator for cabi_realloc, pipes for the descriptor streams. */
#include <inttypes.h>
#include "wasi_p2_cli.h"

static void trap(fusedwasi_p2_guest *s, const char *m) {
    if (s->exited) printf("EXIT %u\n", s->exit_code);
    else printf("TRAP %s\n", m);
    exit(0);
}

static uint32_t bump(fusedwasi_p2_guest *s, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size) {
    uint32_t *next = (uint32_t *)s->user;
    (void)comp, (void)old_ptr, (void)old_size;
    uint32_t ptr = (*next + align - 1) / align * align;
    *next = ptr + new_size;
    return ptr;
}

static uint8_t memory[256];

static void dump(const char *what) {
    printf("%s ", what);
    for (int i = 0; i < 256; i++) printf("%02x", memory[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "main";
    uint32_t next = 160;
    static const char *const args[] = {"prog", "-x"}, *const names[] = {"A", "LONG"}, *const values[] = {"1", "value"};
    fusedwasi_p2_state world;
    fusedwasi_p2_guest s;
    fusedwasi_p2_init(&world);
    fusedwasi_p2_guest_init(&s, &world);
    s.trap = trap;
    s.user = &next;
    s.realloc = bump;
    world.args = args, world.arg_count = 2;
    world.env_names = names, world.env_values = values, world.env_count = 2;
    world.initial_cwd = "/work";
    fusedwasi_p2_set_memory(&s, 0, memory, sizeof memory);
    memcpy(memory + 120, "hello", 5);
    fusedwasi_p2_fd_stream *fin = NULL, *fout = NULL;
    int out_pipe[2] = {-1, -1};
    (void)out_pipe; /* used by the POSIX-only scenario fd */
    if (!strcmp(scenario, "main")) {
        world.terminals = FUSEDWASI_P2_TERMINAL_STDOUT;
        uint32_t hi = fusedwasi_p2_memory_input_new(&world, (const uint8_t *)"in!", 3), ho;
        fusedwasi_p2_buffer_output *bo = fusedwasi_p2_buffer_output_new(&world, 100, &ho);
        world.std[0] = fusedwasi_p2__table_get(&s, hi, FUSEDWASI_P2_KIND_INPUT_STREAM)->obj;
        world.std[1] = &bo->base;
        printf("handles %u %u\n", hi, ho);
        fusedwasi_wasi_cli_environment_get_arguments(&s, 0, 0), dump("arguments");
        fusedwasi_wasi_cli_environment_get_environment(&s, 0, 16), dump("environment");
        fusedwasi_wasi_cli_environment_initial_cwd(&s, 0, 40), dump("cwd");
        fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(&s, 0, 56), dump("terminal stdin");
        fusedwasi_wasi_cli_terminal_stdout_get_terminal_stdout(&s, 0, 64), dump("terminal stdout");
        fusedwasi_wasi_cli_terminal_output_terminal_output_drop(&s, fusedwasi_p2__load_u32(&s, 0, 68));
        uint32_t a = fusedwasi_wasi_cli_stdin_get_stdin(&s), b = fusedwasi_wasi_cli_stdout_get_stdout(&s), c = fusedwasi_wasi_cli_stdout_get_stdout(&s);
        printf("std handles %u %u %u\n", a, b, c);
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, b, 120, 5, 80), dump("write");
        fusedwasi_wasi_io_streams_output_stream_drop(&s, b);
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, c, 120, 5, 88), dump("write after the other handle was dropped");
        fusedwasi_wasi_io_streams_output_stream_drop(&s, c);
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, a, 10, 96), dump("read");
        fusedwasi_wasi_io_streams_input_stream_drop(&s, a);
        printf("stderr %u\n", fusedwasi_wasi_cli_stderr_get_stderr(&s));
        printf("stdout ");
        for (uint64_t i = 0; i < bo->len; i++) printf("%02x", bo->buf[i]);
        printf("\n");
        fusedwasi_p2_free(&world);
        fusedwasi_p2_buffer_output_release(bo);
        return 0;
#ifdef _WIN32
    } else if (!strcmp(scenario, "fd")) {
        puts("the scenario fd needs pipe()"); /* POSIX only: its test is skipped on Windows */
        return 2;
#else
    } else if (!strcmp(scenario, "fd")) {
        int in_pipe[2];
        if (pipe(in_pipe) || pipe(out_pipe)) return 2;
        if (write(in_pipe[1], "pipe!", 5) != 5) return 2;
        close(in_pipe[1]);
        fin = fusedwasi_p2_fd_input_new(&world, in_pipe[0]);
        fout = fusedwasi_p2_fd_output_new(&world, out_pipe[1]);
        world.std[0] = &fin->base, world.std[1] = &fout->base;
        uint32_t a = fusedwasi_wasi_cli_stdin_get_stdin(&s), b = fusedwasi_wasi_cli_stdout_get_stdout(&s);
        printf("std handles %u %u\n", a, b);
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, a, 16, 0), dump("read");
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, a, 16, 16), dump("read at the end");
        fusedwasi_wasi_io_streams_output_stream_blocking_write_and_flush(&s, 0, b, 120, 5, 32), dump("write");
        close(out_pipe[1]);
        uint8_t got[16];
        ssize_t n = read(out_pipe[0], got, sizeof got);
        printf("pipe ");
        for (ssize_t i = 0; i < n; i++) printf("%02x", got[i]);
        printf("\n");
        close(in_pipe[0]);
        close(out_pipe[0]);
#endif
    } else if (!strcmp(scenario, "exit_ok")) {
        fusedwasi_wasi_cli_exit_exit(&s, 0);
    } else if (!strcmp(scenario, "exit_err")) {
        fusedwasi_wasi_cli_exit_exit(&s, 1);
    } else if (!strcmp(scenario, "exit_code")) {
        fusedwasi_wasi_cli_exit_exit_with_code(&s, 0x1ff);
    }
    fusedwasi_p2_free(&world);
    free(fin);
    free(fout);
    return 0;
}
