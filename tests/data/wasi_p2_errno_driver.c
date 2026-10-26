/* prints, for each errno from 0 to 199, the error-code of wasi:filesystem and the error-code of wasi:sockets that the C host gives it (the table of the Python twin is compared in
   tests/test_wasi_p2_filesystem.py) */
#include <stdio.h>
#include "wasi_p2_filesystem.h"
#include "wasi_p2_sockets.h"

int main(void) {
    for (int e = 0; e < 200; e++) printf("%d %d %d\n", e, fusedwasi_p2_errno_code(e), fusedwasi_p2_net_errno(e));
    return 0;
}
