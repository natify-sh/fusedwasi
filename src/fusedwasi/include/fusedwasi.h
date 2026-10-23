/* fusedwasi: a header-only C host for the WASI Component Model. One include for everything; the parts are in fusedwasi/.
   The version is in <fusedwasi/version.h>, which CMake generates from version.h.in.
   The packages of WASI 0.2 are in fusedwasi/p2/ and are included one by one (a module links only what it imports): wasi_p2_cli.h, wasi_p2_clocks.h,
   wasi_p2_filesystem.h, wasi_p2_http.h, wasi_p2_io.h, wasi_p2_random.h, wasi_p2_sockets.h (the others, core and net, are included by them). */
#ifndef FUSEDWASI_H
#define FUSEDWASI_H

#endif /* FUSEDWASI_H */
