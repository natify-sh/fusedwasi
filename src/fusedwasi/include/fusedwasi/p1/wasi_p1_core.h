/* The core of WASI 0.1 that `wasi_snapshot_preview1` and `wasi_unstable` (the first snapshot) share: the state of the world of 0.1, the guest that calls into
   it, and a function `fusedwasi_p1_<name>` for each call of the specification. Header only, included by wasi_p1.h and wasi_p1_unstable.h; the
   functions are `static inline`, so those the module does not import are not in the object.

   There is no Python in this file. The STATE (`fusedwasi_p1_state`) is the world of 0.1, over the state of WASI 0.2 (`fusedwasi_p2_state`, which the host fills: the arguments, the
   environment, the clocks, the random bytes, the sleep, the standard streams): it holds the descriptors with their rights and positions. The GUEST (`fusedwasi_p1_guest`) is the
   caller: it holds a guest of 0.2 (`g->core`: the memory, `trap`, `wait`, `exit`, `exited`, `exit_code`, `user`) and the hook `signal`. Many guests may point to one state; nothing is
   rebound per call. The end of the program is not a hook and not a state of the world: it is what the call that ended it leaves, as the error `I32Exit` of Wasmtime is the
   result of that call, so it is on the guest. `proc_exit` records the code in the guest (`g->core.exited`, `g->core.exit_code`) and reports a trap; the host reads them after the
   call and clears them before the next one. `proc_raise` asks the hook `signal` of the guest (without one it answers NOTSUP and the program goes on). Nothing is closed and no later
   call is refused. The files are those of the system layer of 0.2 (wasi_p2_filesystem.h: the walk of a path confined below a directory and the calls on a name below it, for POSIX
   and for Windows); this file keeps the descriptors with their rights, the positions and the calls of 0.1.

   A WASI call is `uint32_t fusedwasi_p1_<name>(void *g, parameters..., int *trapped)` (proc_exit returns nothing; `g` is a `fusedwasi_p1_guest *`): a trap is reported
   through `trapped`, and the host knows what it was (Python: the exception it set). `fusedwasi_p1_create` makes the state, `fusedwasi_p1_guest_init` a guest.

   The two snapshots differ in four things (the order of `whence`, one right less, a 32-bit link count in `filestat`, an extra
   `identifier` in a clock subscription) and in `sock_accept`, which the first one has not. None of the calls built so far reads or
   writes them, so the same function serves both; the day one does, wasi_p1_unstable.h defines `fusedwasi_p1_unstable_<name>`, which the glue
   then calls for it. */
#ifndef FUSEDWASI_P1_CORE_H
#define FUSEDWASI_P1_CORE_H

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  /* what the headers of Python set, for a host that does not include them */
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#endif
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define FUSEDWASI_P1_READ(fd, buf, n) _read((fd), (buf), (unsigned int)(n))
#define FUSEDWASI_P1_WRITE(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#define FUSEDWASI_P1_SYNC(fd) _commit(fd)
#define FUSEDWASI_P1_LSEEK(fd, off, whence) _lseeki64((fd), (off), (whence))
#define FUSEDWASI_P1_CLOSE(fd) _close(fd)
#else
#include <unistd.h>
#define FUSEDWASI_P1_READ(fd, buf, n) read((fd), (buf), (n))
#define FUSEDWASI_P1_WRITE(fd, buf, n) write((fd), (buf), (n))
#define FUSEDWASI_P1_SYNC(fd) fsync(fd)
#define FUSEDWASI_P1_LSEEK(fd, off, whence) lseek((fd), (off), (whence))
#define FUSEDWASI_P1_CLOSE(fd) close(fd)
#endif
/* Windows has no openat and cannot open a directory: there a directory is a path */
#if defined(_WIN32)
#define FUSEDWASI_P1_DIRS_ARE_PATHS 1
#else
#define FUSEDWASI_P1_DIRS_ARE_PATHS 0
#endif
#include <stdlib.h>
#include <time.h>
#include "../p2/wasi_p2_cli.h"
#include "../p2/wasi_p2_filesystem.h"  /* the state of WASI 0.2 that this one is over; it includes windows.h and bcrypt.h (-lbcrypt where there is no pragma) */
#if !FUSEDWASI_P1_DIRS_ARE_PATHS
#include <dirent.h>
#include <sys/stat.h>
#endif

/* the descriptors are the system's own (also where the files are those of Python: os.open gives them) and can be asked about with poll and fstat: not on Windows */
#ifndef _WIN32
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#define FUSEDWASI_P1_NATIVE_POLL 1
#else
#define FUSEDWASI_P1_NATIVE_POLL 0
#endif

/* the memory of the state: the C library's, so that a host without Python has nothing else to link */
#define FUSEDWASI_P1_MALLOC malloc
#define FUSEDWASI_P1_CALLOC calloc
#define FUSEDWASI_P1_REALLOC realloc
#define FUSEDWASI_P1_FREE free

#define FUSEDWASI_P1_LINK_MAX 4096  /* the most that path_readlink reads of a link */

typedef struct fusedwasi_p1_state fusedwasi_p1_state;
typedef struct fusedwasi_p1_guest fusedwasi_p1_guest;


/* errno of WASI (not the C library's): the whole list of the specification */
enum {
    FUSEDWASI_P1_E_SUCCESS = 0, FUSEDWASI_P1_E_2BIG = 1, FUSEDWASI_P1_E_ACCES = 2, FUSEDWASI_P1_E_ADDRINUSE = 3, FUSEDWASI_P1_E_ADDRNOTAVAIL = 4, FUSEDWASI_P1_E_AFNOSUPPORT = 5, FUSEDWASI_P1_E_AGAIN = 6,
    FUSEDWASI_P1_E_ALREADY = 7, FUSEDWASI_P1_E_BADF = 8, FUSEDWASI_P1_E_BADMSG = 9, FUSEDWASI_P1_E_BUSY = 10, FUSEDWASI_P1_E_CANCELED = 11, FUSEDWASI_P1_E_CHILD = 12, FUSEDWASI_P1_E_CONNABORTED = 13,
    FUSEDWASI_P1_E_CONNREFUSED = 14, FUSEDWASI_P1_E_CONNRESET = 15, FUSEDWASI_P1_E_DEADLK = 16, FUSEDWASI_P1_E_DESTADDRREQ = 17, FUSEDWASI_P1_E_DOM = 18, FUSEDWASI_P1_E_DQUOT = 19,
    FUSEDWASI_P1_E_EXIST = 20, FUSEDWASI_P1_E_FAULT = 21, FUSEDWASI_P1_E_FBIG = 22, FUSEDWASI_P1_E_HOSTUNREACH = 23, FUSEDWASI_P1_E_IDRM = 24, FUSEDWASI_P1_E_ILSEQ = 25, FUSEDWASI_P1_E_INPROGRESS = 26,
    FUSEDWASI_P1_E_INTR = 27, FUSEDWASI_P1_E_INVAL = 28, FUSEDWASI_P1_E_IO = 29, FUSEDWASI_P1_E_ISCONN = 30, FUSEDWASI_P1_E_ISDIR = 31, FUSEDWASI_P1_E_LOOP = 32, FUSEDWASI_P1_E_MFILE = 33,
    FUSEDWASI_P1_E_MLINK = 34, FUSEDWASI_P1_E_MSGSIZE = 35, FUSEDWASI_P1_E_MULTIHOP = 36, FUSEDWASI_P1_E_NAMETOOLONG = 37, FUSEDWASI_P1_E_NETDOWN = 38, FUSEDWASI_P1_E_NETRESET = 39,
    FUSEDWASI_P1_E_NETUNREACH = 40, FUSEDWASI_P1_E_NFILE = 41, FUSEDWASI_P1_E_NOBUFS = 42, FUSEDWASI_P1_E_NODEV = 43, FUSEDWASI_P1_E_NOENT = 44, FUSEDWASI_P1_E_NOEXEC = 45, FUSEDWASI_P1_E_NOLCK = 46,
    FUSEDWASI_P1_E_NOLINK = 47, FUSEDWASI_P1_E_NOMEM = 48, FUSEDWASI_P1_E_NOMSG = 49, FUSEDWASI_P1_E_NOPROTOOPT = 50, FUSEDWASI_P1_E_NOSPC = 51, FUSEDWASI_P1_E_NOSYS = 52, FUSEDWASI_P1_E_NOTCONN = 53,
    FUSEDWASI_P1_E_NOTDIR = 54, FUSEDWASI_P1_E_NOTEMPTY = 55, FUSEDWASI_P1_E_NOTRECOVERABLE = 56, FUSEDWASI_P1_E_NOTSOCK = 57, FUSEDWASI_P1_E_NOTSUP = 58, FUSEDWASI_P1_E_NOTTY = 59,
    FUSEDWASI_P1_E_NXIO = 60, FUSEDWASI_P1_E_OVERFLOW = 61, FUSEDWASI_P1_E_OWNERDEAD = 62, FUSEDWASI_P1_E_PERM = 63, FUSEDWASI_P1_E_PIPE = 64, FUSEDWASI_P1_E_PROTO = 65,
    FUSEDWASI_P1_E_PROTONOSUPPORT = 66, FUSEDWASI_P1_E_PROTOTYPE = 67, FUSEDWASI_P1_E_RANGE = 68, FUSEDWASI_P1_E_ROFS = 69, FUSEDWASI_P1_E_SPIPE = 70, FUSEDWASI_P1_E_SRCH = 71,
    FUSEDWASI_P1_E_STALE = 72, FUSEDWASI_P1_E_TIMEDOUT = 73, FUSEDWASI_P1_E_TXTBSY = 74, FUSEDWASI_P1_E_XDEV = 75, FUSEDWASI_P1_E_NOTCAPABLE = 76
};
enum {
    FUSEDWASI_P1_FT_UNKNOWN = 0, FUSEDWASI_P1_FT_BLOCK = 1, FUSEDWASI_P1_FT_CHAR = 2, FUSEDWASI_P1_FT_DIR = 3, FUSEDWASI_P1_FT_REGULAR = 4, FUSEDWASI_P1_FT_SOCKET_DGRAM = 5, FUSEDWASI_P1_FT_SOCKET_STREAM = 6,
    FUSEDWASI_P1_FT_SYMLINK = 7
};
/* the oflags, fdflags and lookupflags of path_open */
enum { FUSEDWASI_P1_O_CREAT = 1, FUSEDWASI_P1_O_DIRECTORY = 2, FUSEDWASI_P1_O_EXCL = 4, FUSEDWASI_P1_O_TRUNC = 8, FUSEDWASI_P1_LOOKUP_FOLLOW = 1 };
enum { FUSEDWASI_P1_FD_APPEND = 1, FUSEDWASI_P1_FD_DSYNC = 2, FUSEDWASI_P1_FD_NONBLOCK = 4, FUSEDWASI_P1_FD_RSYNC = 8, FUSEDWASI_P1_FD_SYNC = 16 };
/* the flags of the times: which of the two are set, to a value or to now */
enum { FUSEDWASI_P1_T_ATIM = 1, FUSEDWASI_P1_T_ATIM_NOW = 2, FUSEDWASI_P1_T_MTIM = 4, FUSEDWASI_P1_T_MTIM_NOW = 8 };
/* the rights: one bit each, in the order of the specification */
#define FUSEDWASI_P1_R(n) (UINT64_C(1) << (n))
#define FUSEDWASI_P1_R_FD_DATASYNC FUSEDWASI_P1_R(0)
#define FUSEDWASI_P1_R_FD_READ FUSEDWASI_P1_R(1)
#define FUSEDWASI_P1_R_FD_SEEK FUSEDWASI_P1_R(2)
#define FUSEDWASI_P1_R_FD_FDSTAT_SET_FLAGS FUSEDWASI_P1_R(3)
#define FUSEDWASI_P1_R_FD_SYNC FUSEDWASI_P1_R(4)
#define FUSEDWASI_P1_R_FD_TELL FUSEDWASI_P1_R(5)
#define FUSEDWASI_P1_R_FD_WRITE FUSEDWASI_P1_R(6)
#define FUSEDWASI_P1_R_FD_ALLOCATE FUSEDWASI_P1_R(8)
#define FUSEDWASI_P1_R_PATH_CREATE_DIRECTORY FUSEDWASI_P1_R(9)
#define FUSEDWASI_P1_R_PATH_CREATE_FILE FUSEDWASI_P1_R(10)
#define FUSEDWASI_P1_R_PATH_OPEN FUSEDWASI_P1_R(13)
#define FUSEDWASI_P1_R_FD_READDIR FUSEDWASI_P1_R(14)
#define FUSEDWASI_P1_R_PATH_FILESTAT_GET FUSEDWASI_P1_R(18)
#define FUSEDWASI_P1_R_PATH_FILESTAT_SET_SIZE FUSEDWASI_P1_R(19)
#define FUSEDWASI_P1_R_PATH_FILESTAT_SET_TIMES FUSEDWASI_P1_R(20)
#define FUSEDWASI_P1_R_FD_FILESTAT_GET FUSEDWASI_P1_R(21)
#define FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE FUSEDWASI_P1_R(22)
#define FUSEDWASI_P1_R_FD_FILESTAT_SET_TIMES FUSEDWASI_P1_R(23)
#define FUSEDWASI_P1_R_POLL_FD_READWRITE FUSEDWASI_P1_R(27)
#define FUSEDWASI_P1_R_FD_ADVISE FUSEDWASI_P1_R(7)
#define FUSEDWASI_P1_R_PATH_LINK_SOURCE FUSEDWASI_P1_R(11)
#define FUSEDWASI_P1_R_PATH_LINK_TARGET FUSEDWASI_P1_R(12)
#define FUSEDWASI_P1_R_PATH_READLINK FUSEDWASI_P1_R(15)
#define FUSEDWASI_P1_R_PATH_RENAME_SOURCE FUSEDWASI_P1_R(16)
#define FUSEDWASI_P1_R_PATH_RENAME_TARGET FUSEDWASI_P1_R(17)
#define FUSEDWASI_P1_R_PATH_SYMLINK FUSEDWASI_P1_R(24)
#define FUSEDWASI_P1_R_SOCK_SHUTDOWN FUSEDWASI_P1_R(28)
#define FUSEDWASI_P1_R_PATH_REMOVE_DIRECTORY FUSEDWASI_P1_R(25)
#define FUSEDWASI_P1_R_PATH_UNLINK_FILE FUSEDWASI_P1_R(26)
#define FUSEDWASI_P1_R_ALL ((UINT64_C(1) << 30) - 1)
/* the rights only a directory has: a program that asks for one of them along with the right to write (wasi-libc asks for all the
   rights there are, and so does the scratch directory of the test suite) may mean a directory, and then writing is not what is meant */
#define FUSEDWASI_P1_R_OF_DIRECTORY \
    (FUSEDWASI_P1_R(9) | FUSEDWASI_P1_R(10) | FUSEDWASI_P1_R(11) | FUSEDWASI_P1_R(12) | FUSEDWASI_P1_R(13) | FUSEDWASI_P1_R(14) | FUSEDWASI_P1_R(15) | FUSEDWASI_P1_R(16) | FUSEDWASI_P1_R(17) | FUSEDWASI_P1_R(18) | \
     FUSEDWASI_P1_R(19) | FUSEDWASI_P1_R(20) | FUSEDWASI_P1_R(24) | FUSEDWASI_P1_R(25) | FUSEDWASI_P1_R(26))
/* what changes something: taken away from a preopen that is read-only */
#define FUSEDWASI_P1_R_MUTATING \
    (FUSEDWASI_P1_R(0) | FUSEDWASI_P1_R(4) | FUSEDWASI_P1_R(6) | FUSEDWASI_P1_R(8) | FUSEDWASI_P1_R(9) | FUSEDWASI_P1_R(10) | FUSEDWASI_P1_R(11) | FUSEDWASI_P1_R(12) | FUSEDWASI_P1_R(16) | FUSEDWASI_P1_R(17) | \
     FUSEDWASI_P1_R(19) | FUSEDWASI_P1_R(20) | FUSEDWASI_P1_R(22) | FUSEDWASI_P1_R(23) | FUSEDWASI_P1_R(24) | FUSEDWASI_P1_R(25) | FUSEDWASI_P1_R(26))
/* the rights of the standard streams: fd_read, fd_fdstat_set_flags, fd_filestat_get, poll_fd_readwrite / fd_write, fd_sync ... */
#define FUSEDWASI_P1_RIGHTS_STDIN UINT64_C(0x820000a)
#define FUSEDWASI_P1_RIGHTS_STDOUT UINT64_C(0x8200058)

/* the names of a directory, as fd_readdir gives them */
typedef struct {
    uint64_t ino;
    uint32_t namelen;
    uint8_t type;
    char *name;
} fusedwasi_p1_dirent;
typedef struct {
    fusedwasi_p1_dirent *items;
    size_t count, capacity;
} fusedwasi_p1_dirlist;

/* an open descriptor: a standard stream, a file (an operating system descriptor) or a directory (a path: Windows cannot open one) */
enum { FUSEDWASI_P1_K_NONE = 0, FUSEDWASI_P1_K_STD, FUSEDWASI_P1_K_FILE, FUSEDWASI_P1_K_DIR };
typedef struct {
    uint8_t kind;
    uint8_t filetype;
    uint16_t flags;      /* fdflags */
    fusedwasi_p2_descriptor d;  /* a file or a directory of the system layer of 0.2 (d.fd is the descriptor of the system; for a standard stream, which one it is) */
    uint64_t rights, inheriting;
    char *preopen;       /* the name the program sees, for a preopen */
    size_t preopen_len;
    fusedwasi_p1_dirlist *list;  /* fd_readdir: the names of the directory, made at cookie 0 */
} fusedwasi_p1_entry;


/* ---- what the host gives: the configuration ------------------------------------------------------------------------------ */

/* A preopened directory is the same thing in 0.1 and in 0.2: `{path, name, writable}` (the real path of the directory, what the program sees, 0 for read-only). There is no
   name length: the name is a C string (a path has no NUL), as in 0.2. The pointers are the caller's and are copied by `fusedwasi_p1_create`. */
typedef struct fusedwasi_p2_preopen fusedwasi_p1_preopen;

typedef struct {
    const fusedwasi_p1_preopen *preopens;
    size_t npreopens;
    void *user;  /* the host's own: copied to `fusedwasi_p1_state.user`; the library never reads, writes or frees what it points to */
} fusedwasi_p1_config;

/* The world of 0.1: the descriptors (private) over the state of 0.2. Made by `fusedwasi_p1_create`, freed by `fusedwasi_p1_destroy`. */
struct fusedwasi_p1_state {
    fusedwasi_p2_state *core;  /* the state of WASI 0.2 that this one is over: the arguments, the environment, the clocks, the random bytes; owned by the host */
    void *user;           /* the pointer of whoever made the state: the library never reads, writes or frees it (`fusedwasi_p1_config.user` is copied here) */
    fusedwasi_p1_entry *entries;  /* the descriptors, by number */
    uint32_t nentries;
};

/* The caller of 0.1: one for each instance, made once by `fusedwasi_p1_guest_init`, never rewritten per call. It is a guest of 0.2 (`core`: its memory 0 is the memory of the
   instance, bound with `fusedwasi_p1_bind`; `core.trap` is used by what 0.1 does through 0.2, `core.wait` by a blocking read; `core.exited` and `core.exit_code` are where
   `proc_exit` leaves its code; `core.user` is the pointer of whoever made the guest) and the hook `signal`. `core` is the first member, so a hook of the guest of 0.2 can get
   the guest of 0.1 back by a cast. */
struct fusedwasi_p1_guest {
    fusedwasi_p2_guest core;
    fusedwasi_p1_state *state;
    /* `proc_raise` asks it, with the signal number (1 to 30, not one of those that nothing ends: see proc_raise). It returns the errno of WASI for the call, or ends the program by
       its own means (a trap, a longjmp). NULL (the default): NOTSUP, the program goes on. */
    uint32_t (*signal)(fusedwasi_p1_guest *g, uint32_t number);
};

/* the memory of the instance as the calls read it: the engine keeps the base and the size, and may move the first when the memory grows */
#define FUSEDWASI_P1_MEM(g) (*(g)->core.memory[0].data)

/* the errno of the C library (or of an OSError) as an errno of WASI */
static inline uint32_t fusedwasi_p1_errno(int e) {
    switch (e) {
    case 0: return FUSEDWASI_P1_E_SUCCESS;
    case E2BIG: return FUSEDWASI_P1_E_2BIG;
    case EACCES: return FUSEDWASI_P1_E_ACCES;
#ifdef EADDRINUSE
    case EADDRINUSE: return FUSEDWASI_P1_E_ADDRINUSE;
#endif
#ifdef EADDRNOTAVAIL
    case EADDRNOTAVAIL: return FUSEDWASI_P1_E_ADDRNOTAVAIL;
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT: return FUSEDWASI_P1_E_AFNOSUPPORT;
#endif
    case EAGAIN: return FUSEDWASI_P1_E_AGAIN;
#ifdef EALREADY
    case EALREADY: return FUSEDWASI_P1_E_ALREADY;
#endif
    case EBADF: return FUSEDWASI_P1_E_BADF;
#ifdef EBADMSG
    case EBADMSG: return FUSEDWASI_P1_E_BADMSG;
#endif
    case EBUSY: return FUSEDWASI_P1_E_BUSY;
#ifdef ECANCELED
    case ECANCELED: return FUSEDWASI_P1_E_CANCELED;
#endif
    case ECHILD: return FUSEDWASI_P1_E_CHILD;
#ifdef ECONNABORTED
    case ECONNABORTED: return FUSEDWASI_P1_E_CONNABORTED;
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED: return FUSEDWASI_P1_E_CONNREFUSED;
#endif
#ifdef ECONNRESET
    case ECONNRESET: return FUSEDWASI_P1_E_CONNRESET;
#endif
    case EDEADLK: return FUSEDWASI_P1_E_DEADLK;
#ifdef EDESTADDRREQ
    case EDESTADDRREQ: return FUSEDWASI_P1_E_DESTADDRREQ;
#endif
    case EDOM: return FUSEDWASI_P1_E_DOM;
#ifdef EDQUOT
    case EDQUOT: return FUSEDWASI_P1_E_DQUOT;
#endif
    case EEXIST: return FUSEDWASI_P1_E_EXIST;
    case EFAULT: return FUSEDWASI_P1_E_FAULT;
    case EFBIG: return FUSEDWASI_P1_E_FBIG;
#ifdef EHOSTUNREACH
    case EHOSTUNREACH: return FUSEDWASI_P1_E_HOSTUNREACH;
#endif
#ifdef EIDRM
    case EIDRM: return FUSEDWASI_P1_E_IDRM;
#endif
    case EILSEQ: return FUSEDWASI_P1_E_ILSEQ;
#ifdef EINPROGRESS
    case EINPROGRESS: return FUSEDWASI_P1_E_INPROGRESS;
#endif
    case EINTR: return FUSEDWASI_P1_E_INTR;
    case EINVAL: return FUSEDWASI_P1_E_INVAL;
    case EIO: return FUSEDWASI_P1_E_IO;
#ifdef EISCONN
    case EISCONN: return FUSEDWASI_P1_E_ISCONN;
#endif
    case EISDIR: return FUSEDWASI_P1_E_ISDIR;
    case ELOOP: return FUSEDWASI_P1_E_LOOP;
    case EMFILE: return FUSEDWASI_P1_E_MFILE;
    case EMLINK: return FUSEDWASI_P1_E_MLINK;
#ifdef EMSGSIZE
    case EMSGSIZE: return FUSEDWASI_P1_E_MSGSIZE;
#endif
#ifdef EMULTIHOP
    case EMULTIHOP: return FUSEDWASI_P1_E_MULTIHOP;
#endif
    case ENAMETOOLONG: return FUSEDWASI_P1_E_NAMETOOLONG;
#ifdef ENETDOWN
    case ENETDOWN: return FUSEDWASI_P1_E_NETDOWN;
#endif
#ifdef ENETRESET
    case ENETRESET: return FUSEDWASI_P1_E_NETRESET;
#endif
#ifdef ENETUNREACH
    case ENETUNREACH: return FUSEDWASI_P1_E_NETUNREACH;
#endif
    case ENFILE: return FUSEDWASI_P1_E_NFILE;
#ifdef ENOBUFS
    case ENOBUFS: return FUSEDWASI_P1_E_NOBUFS;
#endif
    case ENODEV: return FUSEDWASI_P1_E_NODEV;
    case ENOENT: return FUSEDWASI_P1_E_NOENT;
    case ENOEXEC: return FUSEDWASI_P1_E_NOEXEC;
    case ENOLCK: return FUSEDWASI_P1_E_NOLCK;
#ifdef ENOLINK
    case ENOLINK: return FUSEDWASI_P1_E_NOLINK;
#endif
    case ENOMEM: return FUSEDWASI_P1_E_NOMEM;
#ifdef ENOMSG
    case ENOMSG: return FUSEDWASI_P1_E_NOMSG;
#endif
#ifdef ENOPROTOOPT
    case ENOPROTOOPT: return FUSEDWASI_P1_E_NOPROTOOPT;
#endif
    case ENOSPC: return FUSEDWASI_P1_E_NOSPC;
    case ENOSYS: return FUSEDWASI_P1_E_NOSYS;
#ifdef ENOTCONN
    case ENOTCONN: return FUSEDWASI_P1_E_NOTCONN;
#endif
    case ENOTDIR: return FUSEDWASI_P1_E_NOTDIR;
    case ENOTEMPTY: return FUSEDWASI_P1_E_NOTEMPTY;
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE: return FUSEDWASI_P1_E_NOTRECOVERABLE;
#endif
#ifdef ENOTSOCK
    case ENOTSOCK: return FUSEDWASI_P1_E_NOTSOCK;
#endif
#ifdef ENOTSUP
    case ENOTSUP: return FUSEDWASI_P1_E_NOTSUP;
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP: return FUSEDWASI_P1_E_NOTSUP;
#endif
    case ENOTTY: return FUSEDWASI_P1_E_NOTTY;
    case ENXIO: return FUSEDWASI_P1_E_NXIO;
#ifdef EOVERFLOW
    case EOVERFLOW: return FUSEDWASI_P1_E_OVERFLOW;
#endif
#ifdef EOWNERDEAD
    case EOWNERDEAD: return FUSEDWASI_P1_E_OWNERDEAD;
#endif
    case EPERM: return FUSEDWASI_P1_E_PERM;
    case EPIPE: return FUSEDWASI_P1_E_PIPE;
#ifdef EPROTO
    case EPROTO: return FUSEDWASI_P1_E_PROTO;
#endif
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT: return FUSEDWASI_P1_E_PROTONOSUPPORT;
#endif
#ifdef EPROTOTYPE
    case EPROTOTYPE: return FUSEDWASI_P1_E_PROTOTYPE;
#endif
    case ERANGE: return FUSEDWASI_P1_E_RANGE;
    case EROFS: return FUSEDWASI_P1_E_ROFS;
    case ESPIPE: return FUSEDWASI_P1_E_SPIPE;
    case ESRCH: return FUSEDWASI_P1_E_SRCH;
#ifdef ESTALE
    case ESTALE: return FUSEDWASI_P1_E_STALE;
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT: return FUSEDWASI_P1_E_TIMEDOUT;
#endif
#ifdef ETXTBSY
    case ETXTBSY: return FUSEDWASI_P1_E_TXTBSY;
#endif
    case EXDEV: return FUSEDWASI_P1_E_XDEV;
    default: return FUSEDWASI_P1_E_IO;
    }
}

/* ---- the state ---------------------------------------------------------------------------------------------------- */

static inline char *fusedwasi_p1_copy(const char *text, size_t len) {
    char *out = (char *)FUSEDWASI_P1_MALLOC(len + 1);
    if (out != NULL) {
        memcpy(out, text, len);
        out[len] = '\0';
    }
    return out;
}

static inline void fusedwasi_p1_dirlist_free(fusedwasi_p1_dirlist *list) {
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; i++) FUSEDWASI_P1_FREE(list->items[i].name);
    FUSEDWASI_P1_FREE(list->items);
    FUSEDWASI_P1_FREE(list);
}

/* closes what the descriptor holds; 0, or -1 with errno from the close of a file */
static inline int fusedwasi_p1_release(fusedwasi_p1_entry *e) {
    int rc = 0;
    if (e->kind == FUSEDWASI_P1_K_FILE || e->kind == FUSEDWASI_P1_K_DIR) {
        if (e->d.fd >= 0) rc = FUSEDWASI_P1_CLOSE(e->d.fd);
        e->d.fd = -1;
        FUSEDWASI_P1_FREE(e->d.path);
        e->d.path = NULL;
    }
    FUSEDWASI_P1_FREE(e->preopen);
    fusedwasi_p1_dirlist_free(e->list);
    memset(e, 0, sizeof *e);
    return rc;
}

/* the lowest free number from 3 on; -1 when there is no memory */
static inline int64_t fusedwasi_p1_new_fd(fusedwasi_p1_state *w) {
    for (uint32_t i = 3; i < w->nentries; i++)
        if (w->entries[i].kind == FUSEDWASI_P1_K_NONE) return i;
    uint32_t n = w->nentries * 2;
    fusedwasi_p1_entry *bigger = (fusedwasi_p1_entry *)FUSEDWASI_P1_REALLOC(w->entries, (size_t)n * sizeof(fusedwasi_p1_entry));
    if (bigger == NULL) return -1;
    memset(bigger + w->nentries, 0, (size_t)(n - w->nentries) * sizeof(fusedwasi_p1_entry));
    uint32_t first = w->nentries;
    w->entries = bigger;
    w->nentries = n;
    return first;
}

static inline void fusedwasi_p1_destroy(fusedwasi_p1_state *w);

static inline int fusedwasi_p1_errno_of_code(int code);

/* The state over `core` from a configuration (which it copies: the host may free it at once; `core` must outlive the state). NULL on failure with *err an errno; when it is
   the directory of a preopen that could not be opened, *bad is its number in the configuration, otherwise (size_t)-1. */
static inline fusedwasi_p1_state *fusedwasi_p1_create(const fusedwasi_p1_config *cfg, fusedwasi_p2_state *core, int *err, size_t *bad) {
    *bad = (size_t)-1;
    fusedwasi_p1_state *w = (fusedwasi_p1_state *)FUSEDWASI_P1_CALLOC(1, sizeof(fusedwasi_p1_state));
    if (w == NULL) {
        *err = ENOMEM;
        return NULL;
    }
    w->core = core;
    w->user = cfg->user;
    w->nentries = 8;
    w->entries = (fusedwasi_p1_entry *)FUSEDWASI_P1_CALLOC(w->nentries, sizeof(fusedwasi_p1_entry));
    if (w->entries == NULL) goto nomem;
    for (int i = 0; i < 3; i++) {
        w->entries[i].kind = FUSEDWASI_P1_K_STD;
        w->entries[i].d.fd = i;  /* which stream it is, wherever it is renumbered to */
    }
    for (size_t i = 0; i < cfg->npreopens; i++) {
        const fusedwasi_p1_preopen *p = &cfg->preopens[i];
        int64_t number = fusedwasi_p1_new_fd(w);
        if (number < 0) goto nomem;
        fusedwasi_p1_entry *e = &w->entries[number];
        memset(e, 0, sizeof *e);
        e->kind = FUSEDWASI_P1_K_DIR;
        e->filetype = FUSEDWASI_P1_FT_DIR;
        e->rights = e->inheriting = p->writable ? FUSEDWASI_P1_R_ALL : (FUSEDWASI_P1_R_ALL & ~FUSEDWASI_P1_R_MUTATING);
        e->preopen_len = strlen(p->name);
        e->preopen = fusedwasi_p1_copy(p->name, e->preopen_len);
        if (e->preopen == NULL) {
            fusedwasi_p1_release(e);
            goto nomem;
        }
        int code = fusedwasi_p2_sys_open_directory(p->path, &e->d);
        if (code != FUSEDWASI_P2_OK) {
            *err = fusedwasi_p1_errno_of_code(code);
            *bad = i;
            fusedwasi_p1_release(e);
            fusedwasi_p1_destroy(w);
            return NULL;
        }
    }
    return w;
nomem:
    *err = ENOMEM;
    fusedwasi_p1_destroy(w);
    return NULL;
}

static inline void fusedwasi_p1_destroy(fusedwasi_p1_state *w) {
    if (w == NULL) return;
    for (uint32_t i = 0; i < w->nentries; i++) fusedwasi_p1_release(&w->entries[i]);
    FUSEDWASI_P1_FREE(w->entries);
    FUSEDWASI_P1_FREE(w);
}

/* a guest over `state`: its guest of 0.2 is made over `state->core` (default trap, wait and exit; see `fusedwasi_p2_guest_init`), `signal` is NULL. It owns nothing, so it needs no free. */
static inline void fusedwasi_p1_guest_init(fusedwasi_p1_guest *g, fusedwasi_p1_state *state) {
    memset(g, 0, sizeof *g);
    fusedwasi_p2_guest_init(&g->core, state->core);
    g->state = state;
}

/* binds the memory of the instance by pointer (it moves when the memory grows: the pointers are read at each access; they are the engine's and must outlive the guest) */
static inline void fusedwasi_p1_bind(fusedwasi_p1_guest *g, uint8_t **data, uint64_t *size) {
    fusedwasi_p2_bind_memory(&g->core, 0, data, size);
}

/* ---- the memory of the instance (little-endian, as WebAssembly says) -------------------------------------------------- */

static inline int fusedwasi_p1_in_memory(const fusedwasi_p1_guest *g, uint32_t ptr, uint64_t len) {
    return g->core.memory[0].data != NULL && (uint64_t)ptr + len <= *g->core.memory[0].size;
}

static inline int fusedwasi_p1_put_u8(fusedwasi_p1_guest *g, uint32_t ptr, uint8_t v) {
    if (!fusedwasi_p1_in_memory(g, ptr, 1)) return 0;
    FUSEDWASI_P1_MEM(g)[ptr] = v;
    return 1;
}

static inline int fusedwasi_p1_put_u16(fusedwasi_p1_guest *g, uint32_t ptr, uint16_t v) {
    if (!fusedwasi_p1_in_memory(g, ptr, 2)) return 0;
    FUSEDWASI_P1_MEM(g)[ptr] = (uint8_t)v;
    FUSEDWASI_P1_MEM(g)[ptr + 1] = (uint8_t)(v >> 8);
    return 1;
}

static inline int fusedwasi_p1_put_u32(fusedwasi_p1_guest *g, uint32_t ptr, uint32_t v) {
    if (!fusedwasi_p1_in_memory(g, ptr, 4)) return 0;
    for (int i = 0; i < 4; i++) FUSEDWASI_P1_MEM(g)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1_put_u64(fusedwasi_p1_guest *g, uint32_t ptr, uint64_t v) {
    if (!fusedwasi_p1_in_memory(g, ptr, 8)) return 0;
    for (int i = 0; i < 8; i++) FUSEDWASI_P1_MEM(g)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1_get_u32(const fusedwasi_p1_guest *g, uint32_t ptr, uint32_t *v) {
    if (!fusedwasi_p1_in_memory(g, ptr, 4)) return 0;
    *v = 0;
    for (int i = 0; i < 4; i++) *v |= (uint32_t)FUSEDWASI_P1_MEM(g)[ptr + i] << (8 * i);
    return 1;
}

#define FUSEDWASI_P1_GUEST(vg) fusedwasi_p1_guest *g = (fusedwasi_p1_guest *)(vg)

/* ---- arguments and environment --------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_strings_sizes(fusedwasi_p1_guest *g, const char *const *items, uint32_t count, uint32_t count_ptr, uint32_t size_ptr) {
    size_t size = 0;
    for (uint32_t i = 0; i < count; i++) size += strlen(items[i]) + 1;
    if (!fusedwasi_p1_put_u32(g, count_ptr, count) || !fusedwasi_p1_put_u32(g, size_ptr, (uint32_t)size)) return FUSEDWASI_P1_E_FAULT;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the strings are in memory of the instance as the specification says; the environment is "NAME=value" (`values` is NULL for the arguments) */
static inline uint32_t fusedwasi_p1_strings_get(fusedwasi_p1_guest *g, const char *const *items, const char *const *values, uint32_t count, uint32_t table, uint32_t out) {
    size_t size = 0;
    for (uint32_t i = 0; i < count; i++) size += strlen(items[i]) + (values != NULL ? 1 + strlen(values[i]) : 0) + 1;
    if (!fusedwasi_p1_in_memory(g, out, size) || !fusedwasi_p1_in_memory(g, table, (uint64_t)count * 4)) return FUSEDWASI_P1_E_FAULT;
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        fusedwasi_p1_put_u32(g, table + 4 * i, out + (uint32_t)offset);
        size_t n = strlen(items[i]);
        memcpy(FUSEDWASI_P1_MEM(g) + out + offset, items[i], n);
        offset += n;
        if (values != NULL) {
            FUSEDWASI_P1_MEM(g)[out + offset++] = '=';
            n = strlen(values[i]);
            memcpy(FUSEDWASI_P1_MEM(g) + out + offset, values[i], n);
            offset += n;
        }
        FUSEDWASI_P1_MEM(g)[out + offset++] = 0;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_args_sizes_get(void *vg, uint32_t argc, uint32_t size, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_strings_sizes(g, g->state->core->args, g->state->core->arg_count, argc, size);
}

static inline uint32_t fusedwasi_p1_args_get(void *vg, uint32_t argv, uint32_t buf, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_strings_get(g, g->state->core->args, NULL, g->state->core->arg_count, argv, buf);
}

/* the sizes of the environment count the "=" */
static inline uint32_t fusedwasi_p1_environ_sizes_get(void *vg, uint32_t count, uint32_t size, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    size_t total = 0;
    for (uint32_t i = 0; i < g->state->core->env_count; i++) total += strlen(g->state->core->env_names[i]) + 1 + strlen(g->state->core->env_values[i]) + 1;
    if (!fusedwasi_p1_put_u32(g, count, g->state->core->env_count) || !fusedwasi_p1_put_u32(g, size, (uint32_t)total)) return FUSEDWASI_P1_E_FAULT;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_environ_get(void *vg, uint32_t environ_, uint32_t buf, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_strings_get(g, g->state->core->env_names, g->state->core->env_values, g->state->core->env_count, environ_, buf);
}

/* ---- clocks and random bytes (the clocks, the random source and the sleep are those of the state of WASI 0.2: fusedwasi_p2_state) ---------- */

static inline uint32_t fusedwasi_p1_clock_res_get(void *vg, uint32_t id, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1_put_u64(g, ptr, g->state->core->resolution_ns[id]) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* the time of a clock; a clock that cannot be read is NOTSUP (a host that traps does it in its function) */
static inline uint32_t fusedwasi_p1_clock_ns(fusedwasi_p1_guest *g, uint32_t id, uint64_t *ns) {
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    return g->state->core->clock(g->state->core, id, ns) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_NOTSUP;
}

static inline uint32_t fusedwasi_p1_clock_time_get(void *vg, uint32_t id, uint64_t precision, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)precision;
    (void)trapped;
    uint64_t ns = 0;
    uint32_t rc = fusedwasi_p1_clock_ns(g, id, &ns);
    if (rc != FUSEDWASI_P1_E_SUCCESS) return rc;
    return fusedwasi_p1_put_u64(g, ptr, ns) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_random_get(void *vg, uint32_t buf, uint32_t size, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    if (!fusedwasi_p1_in_memory(g, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (size == 0) return FUSEDWASI_P1_E_SUCCESS;
    return g->state->core->random_bytes(g->state->core, FUSEDWASI_P1_MEM(g) + buf, size) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_IO;
}

static inline uint32_t fusedwasi_p1_sched_yield(void *vg, int *trapped) {
    (void)vg;
    (void)trapped;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline void fusedwasi_p1_proc_exit(void *vg, uint32_t code, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    g->core.exited = 1;  /* the end of a call is the guest's: the host reads `exited` and `exit_code` after the call and clears `exited` before the next */
    g->core.exit_code = code;
    *trapped = 1;
}

/* proc_raise: the signals of WASI 0.1 by number. What a signal does to a program is the host's decision, not WASI's, so the library does not end the program: it asks the hook
   `signal` of the guest, which returns the errno of the call or ends the program by its own means. Without the hook the answer is NOTSUP and the program goes on (as in
   Wasmtime). A number above 30 is INVAL, and 0 (no signal) is success. */
static inline uint32_t fusedwasi_p1_proc_raise(void *vg, uint32_t sig, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    if (sig > 30) return FUSEDWASI_P1_E_INVAL;
    if (sig == 0) return FUSEDWASI_P1_E_SUCCESS;
    return g->signal ? g->signal(g, sig) : FUSEDWASI_P1_E_NOTSUP;
}

/* ---- descriptors ----------------------------------------------------------------------------------------------------- */

static inline fusedwasi_p1_entry *fusedwasi_p1_entry_of(fusedwasi_p1_guest *g, uint32_t fd) {
    return fd < g->state->nentries && g->state->entries[fd].kind != FUSEDWASI_P1_K_NONE ? &g->state->entries[fd] : NULL;
}

/* the real path of a directory descriptor: root, or root/rel; new memory */
static inline char *fusedwasi_p1_join(const char *root, const char *rel) {
    size_t a = strlen(root), b = strlen(rel);
    char *out = (char *)FUSEDWASI_P1_MALLOC(a + b + 2);
    if (out == NULL) return NULL;
    memcpy(out, root, a);
    if (b) {
        out[a] = '/';
        memcpy(out + a + 1, rel, b);
        out[a + 1 + b] = '\0';
    } else {
        out[a] = '\0';
    }
    return out;
}

/* ---- the standard streams (the hooks of the host; without them descriptors 0, 1 and 2) --------------------------------- */

/* All the buffers of a call are looked at before any of them is used: a bad one in the middle must not leave the data before it already written (or read)
   with an error and no count (found by comparing with uvwasi). */
static inline uint32_t fusedwasi_p1_iovs_valid(fusedwasi_p1_guest *g, uint32_t iovs, uint32_t count) {
    if (!fusedwasi_p1_in_memory(g, iovs, (uint64_t)count * 8)) return FUSEDWASI_P1_E_FAULT;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the status of a stream that failed: a closed one is a broken pipe; the errno of the system, when the stream knows it, as it is */
static inline uint32_t fusedwasi_p1_stream_status(int result, const fusedwasi_p2_stream *st) {
    if (result == FUSEDWASI_P2_STREAM_CLOSED) return FUSEDWASI_P1_E_PIPE;
    return st->failure_errno ? fusedwasi_p1_errno(st->failure_errno) : FUSEDWASI_P1_E_IO;
}

/* The standard streams are those of the state of 0.2 (`s->std[i]`, else the descriptors of the process): what was written is reported (a short write); the error comes
   with the next call. A trap of the host is a trap, through `s->trap`. */
static inline uint32_t fusedwasi_p1_write_std(fusedwasi_p1_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nwritten, int *trapped) {
    (void)trapped;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    fusedwasi_p2_stream *st = fusedwasi_p2_std(&g->core, (int)fd);
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        int result = st->write(g->state->core, st, FUSEDWASI_P1_MEM(g) + base, len);
        if (result != FUSEDWASI_P2_STREAM_OK) {
            if (total == 0) return fusedwasi_p1_stream_status(result, st);
            break;
        }
        total += len;
    }
    return fusedwasi_p1_put_u32(g, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_read_std(fusedwasi_p1_guest *g, uint32_t iovs, uint32_t count, uint32_t nread, int *trapped) {
    (void)trapped;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    uint32_t base = 0, len = 0;
    for (uint32_t i = 0; i < count; i++) {  /* the first buffer that is not empty: a short read is allowed */
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (len) break;
    }
    if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
    uint64_t got = 0;
    if (len) {
        fusedwasi_p2_stream *st = fusedwasi_p2_std(&g->core, 0);
        for (;;) {  /* a stream of 0.2 gives nothing when nothing is ready; a read of 0.1 waits for a byte or the end */
            int result = st->read(g->state->core, st, FUSEDWASI_P1_MEM(g) + base, len, &got);
            if (result == FUSEDWASI_P2_STREAM_FAILED) return fusedwasi_p1_stream_status(result, st);
            if (result == FUSEDWASI_P2_STREAM_CLOSED) {
                got = 0;
                break;
            }
            if (got > len) return FUSEDWASI_P1_E_IO;
            if (got > 0) break;
            g->core.wait(&g->core);
        }
    }
    return fusedwasi_p1_put_u32(g, nread, (uint32_t)got) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* ---- reading, seeking, closing ---------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_fd_write(void *vg, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nwritten, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL || e->kind == FUSEDWASI_P1_K_DIR) return e == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_ISDIR;
    if (e->kind == FUSEDWASI_P1_K_STD) return e->d.fd == 0 ? FUSEDWASI_P1_E_BADF : fusedwasi_p1_write_std(g, (uint32_t)e->d.fd, iovs, count, nwritten, trapped);
    if (!(e->rights & FUSEDWASI_P1_R_FD_WRITE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
#ifdef _WIN32
        if (e->flags & FUSEDWASI_P1_FD_APPEND) FUSEDWASI_P1_LSEEK(e->d.fd, 0, SEEK_END);
#endif
        long long n = FUSEDWASI_P1_WRITE(e->d.fd, FUSEDWASI_P1_MEM(g) + base, len);  /* straight from the memory of the module */
        if (n < 0) {
            if (total) break;
            return fusedwasi_p1_errno(errno);
        }
        total += (uint64_t)n;
        if ((uint32_t)n < len) break;
    }
    return fusedwasi_p1_put_u32(g, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_fd_read(void *vg, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nread, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return e->d.fd == 0 ? fusedwasi_p1_read_std(g, iovs, count, nread, trapped) : FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READ)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        long long n = FUSEDWASI_P1_READ(e->d.fd, FUSEDWASI_P1_MEM(g) + base, len);  /* no interpreter: this is the part that moves the data */
        if (n < 0) {
            if (total) break;  /* what was read is reported; the error comes with the next call */
            return fusedwasi_p1_errno(errno);
        }
        total += (uint64_t)n;
        if ((uint32_t)n < len) break;
#if FUSEDWASI_P1_NATIVE_POLL
        /* A pipe, a FIFO, a terminal: when the first buffer is full, a read into the next one may wait for more although something has been read, and a
           short read is allowed (found by comparing with uvwasi, which reads all the buffers with one readv). Only a regular file goes on. */
        if (n > 0 && i + 1 < count) {
            struct stat st;
            if (fstat(e->d.fd, &st) == 0 && !S_ISREG(st.st_mode)) break;
        }
#endif
    }
    return fusedwasi_p1_put_u32(g, nread, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* what fd_seek asks of the descriptor: its right, or (a seek by 0 bytes from the current position is a tell) the right to tell */
#define FUSEDWASI_P1_SEEK_RIGHT(offset, native) \
    (FUSEDWASI_P1_R_FD_SEEK | ((offset) == 0 && (native) == SEEK_CUR ? FUSEDWASI_P1_R_FD_TELL : 0))

static inline uint32_t fusedwasi_p1_seek_native(fusedwasi_p1_guest *g, uint32_t fd, int64_t offset, int native, uint32_t ptr, uint64_t right) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    long long pos = FUSEDWASI_P1_LSEEK(e->d.fd, offset, native);
    if (pos < 0) return fusedwasi_p1_errno(errno);
    return fusedwasi_p1_put_u64(g, ptr, (uint64_t)pos) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* the order of whence in this snapshot: set, cur, end */
static inline uint32_t fusedwasi_p1_fd_seek(void *vg, uint32_t fd, uint64_t offset, uint32_t whence, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    static const int native[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
    if (whence > 2) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1_seek_native(g, fd, (int64_t)offset, native[whence], ptr, FUSEDWASI_P1_SEEK_RIGHT((int64_t)offset, native[whence]));
}

static inline uint32_t fusedwasi_p1_fd_tell(void *vg, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_seek_native(g, fd, 0, SEEK_CUR, ptr, FUSEDWASI_P1_R_FD_TELL);
}

static inline uint32_t fusedwasi_p1_fd_close(void *vg, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF */
        memset(e, 0, sizeof *e);
        return FUSEDWASI_P1_E_SUCCESS;
    }
    return fusedwasi_p1_release(e) == 0 ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_errno(errno);
}

static inline uint32_t fusedwasi_p1_fd_renumber(void *vg, uint32_t from, uint32_t to, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *a = fusedwasi_p1_entry_of(g, from), *b = fusedwasi_p1_entry_of(g, to);
    if (a == NULL || b == NULL) return FUSEDWASI_P1_E_BADF;
    if (from == to) return FUSEDWASI_P1_E_SUCCESS;
    fusedwasi_p1_release(b);
    *b = *a;
    memset(a, 0, sizeof *a);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_advise(void *vg, uint32_t fd, uint64_t offset, uint64_t length, uint32_t advice, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)offset;
    (void)length;
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL || e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & FUSEDWASI_P1_R_FD_ADVISE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (advice > 5) return FUSEDWASI_P1_E_INVAL;  /* the advice is one of six */
    return FUSEDWASI_P1_E_SUCCESS;  /* a hint: nothing is done with it */
}

static inline int fusedwasi_p1_std_is_real(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e);
static inline uint8_t fusedwasi_p1_std_filetype(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e);

static inline uint32_t fusedwasi_p1_fd_fdstat_get(void *vg, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    uint8_t filetype = e->kind == FUSEDWASI_P1_K_STD ? fusedwasi_p1_std_filetype(g, e) : e->filetype;
    uint64_t base = e->kind == FUSEDWASI_P1_K_STD ? (e->d.fd == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT) : e->rights;
    if (!fusedwasi_p1_in_memory(g, ptr, 24)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(g) + ptr, 0, 24);
    fusedwasi_p1_put_u8(g, ptr, filetype);
    fusedwasi_p1_put_u16(g, ptr + 2, e->flags);
    fusedwasi_p1_put_u64(g, ptr + 8, base);
    fusedwasi_p1_put_u64(g, ptr + 16, e->inheriting);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_prestat_get(void *vg, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;  /* the end of the list the program walks */
    if (!fusedwasi_p1_in_memory(g, ptr, 8)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(g) + ptr, 0, 8);  /* the type of a preopen: a directory, 0 */
    fusedwasi_p1_put_u32(g, ptr + 4, (uint32_t)e->preopen_len);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_prestat_dir_name(void *vg, uint32_t fd, uint32_t path, uint32_t size, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;
    if (size < e->preopen_len) return FUSEDWASI_P1_E_NAMETOOLONG;
    if (!fusedwasi_p1_in_memory(g, path, e->preopen_len)) return FUSEDWASI_P1_E_FAULT;
    memcpy(FUSEDWASI_P1_MEM(g) + path, e->preopen, e->preopen_len);
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- what a file or a directory is ----------------------------------------------------------------------------------- */

static inline uint8_t fusedwasi_p1_filetype_of(unsigned long mode) {
    switch (mode & 0170000UL) {  /* the bits of st_mode, the same on every system Python runs on */
    case 0040000UL: return FUSEDWASI_P1_FT_DIR;
    case 0100000UL: return FUSEDWASI_P1_FT_REGULAR;
    case 0120000UL: return FUSEDWASI_P1_FT_SYMLINK;
    case 0020000UL: return FUSEDWASI_P1_FT_CHAR;
    case 0060000UL: return FUSEDWASI_P1_FT_BLOCK;
    case 0140000UL: return FUSEDWASI_P1_FT_SOCKET_STREAM;
    default: return FUSEDWASI_P1_FT_UNKNOWN;
    }
}

/* A standard stream that is the descriptor of the process (no hook stands in for it): it is what the system says it is. wasi-libc takes a character
   device without the right to seek for a terminal, so a stream that was redirected to a file or a pipe must not say it is one (found by comparing with
   uvwasi, which asks the descriptor). A stream that goes through a hook (Python's `sys.stdout`) has no descriptor to ask and stays a character device. */
static inline int fusedwasi_p1_std_is_real(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e) {
#if FUSEDWASI_P1_NATIVE_POLL
    return e->kind == FUSEDWASI_P1_K_STD && g->state->core->std[e->d.fd] == NULL;
#else
    (void)g, (void)e;
    return 0;
#endif
}

static inline uint8_t fusedwasi_p1_std_filetype(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e) {
#if FUSEDWASI_P1_NATIVE_POLL
    struct stat st;
    if (fusedwasi_p1_std_is_real(g, e) && fstat(e->d.fd, &st) == 0) return fusedwasi_p1_filetype_of((unsigned long)st.st_mode);  /* a pipe is unknown: not a terminal */
#endif
    (void)g, (void)e;
    return FUSEDWASI_P1_FT_CHAR;
}

/* what the specification calls filestat, the same for both ways of getting it */
typedef struct {
    uint64_t dev, ino, nlink, size, atim, mtim, ctim;
    uint8_t filetype;
} fusedwasi_p1_stat;

/* the filestat of the first snapshot has a 32-bit link count and 56 bytes */
static inline uint32_t fusedwasi_p1_put_filestat(fusedwasi_p1_guest *g, uint32_t ptr, const fusedwasi_p1_stat *st, int legacy) {
    size_t total = legacy ? 56 : 64;
    if (!fusedwasi_p1_in_memory(g, ptr, total)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(g) + ptr, 0, total);
    fusedwasi_p1_put_u64(g, ptr, st->dev);
    fusedwasi_p1_put_u64(g, ptr + 8, st->ino);
    fusedwasi_p1_put_u8(g, ptr + 16, st->filetype);
    if (legacy) {
        fusedwasi_p1_put_u32(g, ptr + 20, (uint32_t)st->nlink);
        fusedwasi_p1_put_u64(g, ptr + 24, st->size);
        fusedwasi_p1_put_u64(g, ptr + 32, st->atim);
        fusedwasi_p1_put_u64(g, ptr + 40, st->mtim);
        fusedwasi_p1_put_u64(g, ptr + 48, st->ctim);
    } else {
        fusedwasi_p1_put_u64(g, ptr + 24, st->nlink);
        fusedwasi_p1_put_u64(g, ptr + 32, st->size);
        fusedwasi_p1_put_u64(g, ptr + 40, st->atim);
        fusedwasi_p1_put_u64(g, ptr + 48, st->mtim);
        fusedwasi_p1_put_u64(g, ptr + 56, st->ctim);
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- the directory listing, as the specification wants it: "." and "..", then the names in order ------------------------ */

static inline int fusedwasi_p1_dirent_order(const void *a, const void *b) {
    const fusedwasi_p1_dirent *x = (const fusedwasi_p1_dirent *)a, *y = (const fusedwasi_p1_dirent *)b;
    int rx = strcmp(x->name, ".") == 0 ? 0 : strcmp(x->name, "..") == 0 ? 1 : 2;
    int ry = strcmp(y->name, ".") == 0 ? 0 : strcmp(y->name, "..") == 0 ? 1 : 2;
    return rx != ry ? rx - ry : strcmp(x->name, y->name);
}

static inline int fusedwasi_p1_dirlist_add(fusedwasi_p1_dirlist *list, const char *name, uint64_t ino, uint8_t type) {
    if (list->count == list->capacity) {  /* the room doubles: a directory of many names is not copied again and again */
        size_t room = list->capacity ? list->capacity * 2 : 128;
        fusedwasi_p1_dirent *bigger = (fusedwasi_p1_dirent *)FUSEDWASI_P1_REALLOC(list->items, room * sizeof(fusedwasi_p1_dirent));
        if (bigger == NULL) return -1;
        list->items = bigger;
        list->capacity = room;
    }
    char *copy = fusedwasi_p1_copy(name, strlen(name));
    if (copy == NULL) return -1;
    fusedwasi_p1_dirent *item = &list->items[list->count++];
    item->name = copy;
    item->namelen = (uint32_t)strlen(name);
    item->ino = ino;
    item->type = type;
    return 0;
}

/* the fdflags as flags of the system (the ones it has) */
static inline int fusedwasi_p1_native_fdflags(uint32_t fdflags) {
    int flags = 0;
#ifndef _WIN32  /* the C library of Windows cannot take it off again: fd_write does it by seeking to the end */
    if (fdflags & FUSEDWASI_P1_FD_APPEND) flags |= O_APPEND;
#endif
#ifdef O_DSYNC
    if (fdflags & FUSEDWASI_P1_FD_DSYNC) flags |= O_DSYNC;
#endif
#ifdef O_RSYNC
    if (fdflags & FUSEDWASI_P1_FD_RSYNC) flags |= O_RSYNC;
#endif
#ifdef O_SYNC
    if (fdflags & FUSEDWASI_P1_FD_SYNC) flags |= O_SYNC;
#endif
#ifdef O_NONBLOCK
    if (fdflags & FUSEDWASI_P1_FD_NONBLOCK) flags |= O_NONBLOCK;
#endif
    return flags;
}

/* ---- the file system: the system layer of WASI 0.2 (wasi_p2_filesystem.h, POSIX and Windows) -----------------------------
   A directory is an open descriptor of the system layer (a path on Windows), a path is walked from it one name at a time with the links read on the way, and nothing
   can lead out of the preopen (`fusedwasi_p2_walk_path`). What the layer answers is a code of 0.2; the calls of 0.1 give the errno it stands for. */

/* the errno of WASI for a code of 0.2 (the host of Wasmtime does the same for 0.1 over 0.2) */
static inline uint32_t fusedwasi_p1_of_code(int code) {
    switch (code) {
    case FUSEDWASI_P2_OK: return FUSEDWASI_P1_E_SUCCESS;
    case FUSEDWASI_P2_E_ACCESS: return FUSEDWASI_P1_E_ACCES;
    case FUSEDWASI_P2_E_WOULD_BLOCK: return FUSEDWASI_P1_E_AGAIN;
    case FUSEDWASI_P2_E_ALREADY: return FUSEDWASI_P1_E_ALREADY;
    case FUSEDWASI_P2_E_BAD_DESCRIPTOR: return FUSEDWASI_P1_E_BADF;
    case FUSEDWASI_P2_E_BUSY: return FUSEDWASI_P1_E_BUSY;
    case FUSEDWASI_P2_E_DEADLOCK: return FUSEDWASI_P1_E_DEADLK;
    case FUSEDWASI_P2_E_QUOTA: return FUSEDWASI_P1_E_DQUOT;
    case FUSEDWASI_P2_E_EXIST: return FUSEDWASI_P1_E_EXIST;
    case FUSEDWASI_P2_E_FILE_TOO_LARGE: return FUSEDWASI_P1_E_FBIG;
    case FUSEDWASI_P2_E_ILLEGAL_BYTE_SEQUENCE: return FUSEDWASI_P1_E_ILSEQ;
    case FUSEDWASI_P2_E_IN_PROGRESS: return FUSEDWASI_P1_E_INPROGRESS;
    case FUSEDWASI_P2_E_INTERRUPTED: return FUSEDWASI_P1_E_INTR;
    case FUSEDWASI_P2_E_INVALID: return FUSEDWASI_P1_E_INVAL;
    case FUSEDWASI_P2_E_IO: return FUSEDWASI_P1_E_IO;
    case FUSEDWASI_P2_E_IS_DIRECTORY: return FUSEDWASI_P1_E_ISDIR;
    case FUSEDWASI_P2_E_LOOP: return FUSEDWASI_P1_E_LOOP;
    case FUSEDWASI_P2_E_TOO_MANY_LINKS: return FUSEDWASI_P1_E_MLINK;
    case FUSEDWASI_P2_E_MESSAGE_SIZE: return FUSEDWASI_P1_E_MSGSIZE;
    case FUSEDWASI_P2_E_NAME_TOO_LONG: return FUSEDWASI_P1_E_NAMETOOLONG;
    case FUSEDWASI_P2_E_NO_DEVICE: return FUSEDWASI_P1_E_NODEV;
    case FUSEDWASI_P2_E_NO_ENTRY: return FUSEDWASI_P1_E_NOENT;
    case FUSEDWASI_P2_E_NO_LOCK: return FUSEDWASI_P1_E_NOLCK;
    case FUSEDWASI_P2_E_INSUFFICIENT_MEMORY: return FUSEDWASI_P1_E_NOMEM;
    case FUSEDWASI_P2_E_INSUFFICIENT_SPACE: return FUSEDWASI_P1_E_NOSPC;
    case FUSEDWASI_P2_E_NOT_DIRECTORY: return FUSEDWASI_P1_E_NOTDIR;
    case FUSEDWASI_P2_E_NOT_EMPTY: return FUSEDWASI_P1_E_NOTEMPTY;
    case FUSEDWASI_P2_E_NOT_RECOVERABLE: return FUSEDWASI_P1_E_NOTRECOVERABLE;
    case FUSEDWASI_P2_E_UNSUPPORTED: return FUSEDWASI_P1_E_NOSYS;
    case FUSEDWASI_P2_E_NO_TTY: return FUSEDWASI_P1_E_NOTTY;
    case FUSEDWASI_P2_E_NO_SUCH_DEVICE: return FUSEDWASI_P1_E_NXIO;
    case FUSEDWASI_P2_E_OVERFLOW: return FUSEDWASI_P1_E_OVERFLOW;
    case FUSEDWASI_P2_E_NOT_PERMITTED: return FUSEDWASI_P1_E_PERM;
    case FUSEDWASI_P2_E_PIPE: return FUSEDWASI_P1_E_PIPE;
    case FUSEDWASI_P2_E_READ_ONLY: return FUSEDWASI_P1_E_ROFS;
    case FUSEDWASI_P2_E_INVALID_SEEK: return FUSEDWASI_P1_E_SPIPE;
    case FUSEDWASI_P2_E_TEXT_FILE_BUSY: return FUSEDWASI_P1_E_TXTBSY;
    case FUSEDWASI_P2_E_CROSS_DEVICE: return FUSEDWASI_P1_E_XDEV;
    default: return FUSEDWASI_P1_E_IO;
    }
}

/* the errno of the C library for a code of 0.2 (what `fusedwasi_p1_create` tells when a directory of a preopen cannot be opened) */
static inline int fusedwasi_p1_errno_of_code(int code) {
    switch (code) {
    case FUSEDWASI_P2_E_ACCESS: return EACCES;
    case FUSEDWASI_P2_E_NO_ENTRY: return ENOENT;
    case FUSEDWASI_P2_E_NOT_DIRECTORY: return ENOTDIR;
    case FUSEDWASI_P2_E_NAME_TOO_LONG: return ENAMETOOLONG;
    case FUSEDWASI_P2_E_INSUFFICIENT_MEMORY: return ENOMEM;
    default: return EIO;
    }
}

/* the errno of WASI for the failure of a walk: a path that leads out of the directory it is given from is not a capability the program has */
static inline uint32_t fusedwasi_p1_of_walk(int code) { return code == FUSEDWASI_P2_E_NOT_PERMITTED ? FUSEDWASI_P1_E_NOTCAPABLE : fusedwasi_p1_of_code(code); }

/* the type of a file of 0.2 as the filetype of WASI */
static inline uint8_t fusedwasi_p1_filetype_of_type(uint8_t type) {
    switch (type) {
    case FUSEDWASI_P2_T_DIRECTORY: return FUSEDWASI_P1_FT_DIR;
    case FUSEDWASI_P2_T_REGULAR: return FUSEDWASI_P1_FT_REGULAR;
    case FUSEDWASI_P2_T_SYMLINK: return FUSEDWASI_P1_FT_SYMLINK;
    case FUSEDWASI_P2_T_CHAR: return FUSEDWASI_P1_FT_CHAR;
    case FUSEDWASI_P2_T_BLOCK: return FUSEDWASI_P1_FT_BLOCK;
    case FUSEDWASI_P2_T_SOCKET: return FUSEDWASI_P1_FT_SOCKET_STREAM;
    default: return FUSEDWASI_P1_FT_UNKNOWN;
    }
}

/* a time of a file in nanoseconds since 1970 as WASI has it (unsigned): before 1970 is 0 */
static inline uint64_t fusedwasi_p1_time_of(int64_t ns) { return ns < 0 ? 0 : (uint64_t)ns; }

static inline void fusedwasi_p1_stat_of_p2(const fusedwasi_p2_stat *st, fusedwasi_p1_stat *out) {
    out->dev = st->dev;
    out->ino = st->ino;
    out->nlink = st->nlink;
    out->size = st->size;
    out->atim = fusedwasi_p1_time_of(st->atime_ns);
    out->mtim = fusedwasi_p1_time_of(st->mtime_ns);
    out->ctim = fusedwasi_p1_time_of(st->ctime_ns);
    out->filetype = fusedwasi_p1_filetype_of_type(st->type);
}

static inline uint32_t fusedwasi_p1_be_stat_fd(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e, fusedwasi_p1_stat *out, int *trapped) {
    (void)g;
    (void)trapped;
    fusedwasi_p2_stat st;
    int rc = fusedwasi_p2_sys_fstat(&e->d, &st);
    if (rc != FUSEDWASI_P2_OK) return fusedwasi_p1_of_code(rc);
    fusedwasi_p1_stat_of_p2(&st, out);
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the directory that holds the last name of a path, walked from `dir` (a link at the end is read when `follow`); "." (the directory itself) is `itself` */
static inline uint32_t fusedwasi_p1_be_walk(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, fusedwasi_p2_walk *walk,
                                    int *itself) {
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    int rc = fusedwasi_p2_walk_path_with(walk, &dir->d, FUSEDWASI_P1_MEM(g) + path, len, follow, 1);
    if (rc != FUSEDWASI_P2_OK) return fusedwasi_p1_of_walk(rc);
    *itself = strcmp(walk->leaf, ".") == 0;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_stat_path(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow,
                                         fusedwasi_p1_stat *out, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, follow, &walk, &itself);
    if (rc) return rc;
    fusedwasi_p2_stat st;
    int code = fusedwasi_p2_sys_lstat(walk.dir, walk.leaf, &st);  /* a link at the end was read by the walk if it was to be */
    fusedwasi_p2_walk_close(&walk);
    if (code != FUSEDWASI_P2_OK) return fusedwasi_p1_of_code(code);
    fusedwasi_p1_stat_of_p2(&st, out);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_open(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint32_t oflags,
                                    uint64_t base, uint32_t fdflags, fusedwasi_p1_entry *e, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, follow, &walk, &itself);
    if (rc) return rc;
    /* writing is what the rights say: to write, to sync the data, to allocate, to set the size (uvwasi; wasi-libc leaves these out of a read-only open) */
    int writable = (base & (FUSEDWASI_P1_R_FD_WRITE | FUSEDWASI_P1_R_FD_DATASYNC | FUSEDWASI_P1_R_FD_ALLOCATE | FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE)) != 0 ||
                   (oflags & FUSEDWASI_P1_O_TRUNC) != 0,
        readable = (base & FUSEDWASI_P1_R_FD_READ) != 0;
    uint32_t flags = (writable ? FUSEDWASI_P2_WRITE : 0) | (readable || !writable ? FUSEDWASI_P2_READ : 0);
    if (fdflags & (FUSEDWASI_P1_FD_SYNC | FUSEDWASI_P1_FD_RSYNC)) flags |= FUSEDWASI_P2_FILE_SYNC;
    if (fdflags & FUSEDWASI_P1_FD_DSYNC) flags |= FUSEDWASI_P2_DATA_SYNC;
    uint32_t open_flags = ((oflags & FUSEDWASI_P1_O_CREAT) ? FUSEDWASI_P2_CREATE : 0) | ((oflags & FUSEDWASI_P1_O_DIRECTORY) ? FUSEDWASI_P2_DIRECTORY : 0) |
                          ((oflags & FUSEDWASI_P1_O_EXCL) ? FUSEDWASI_P2_EXCLUSIVE : 0) | ((oflags & FUSEDWASI_P1_O_TRUNC) ? FUSEDWASI_P2_TRUNCATE : 0);
    fusedwasi_p2_descriptor made;
    memset(&made, 0, sizeof made);
    made.fd = -1;
    int code = fusedwasi_p2_sys_open_at(walk.dir, walk.leaf, open_flags, flags, &made);
    if (code == FUSEDWASI_P2_E_IS_DIRECTORY && (base & FUSEDWASI_P1_R_OF_DIRECTORY) && !(oflags & (FUSEDWASI_P1_O_CREAT | FUSEDWASI_P1_O_TRUNC)))
        code = fusedwasi_p2_sys_open_at(walk.dir, walk.leaf, FUSEDWASI_P2_DIRECTORY, FUSEDWASI_P2_READ, &made);  /* it was a directory, and asked for as one */
    fusedwasi_p2_walk_close(&walk);
    if (code != FUSEDWASI_P2_OK) return fusedwasi_p1_of_code(code);
    fusedwasi_p2_stat st;
    code = fusedwasi_p2_sys_fstat(&made, &st);
    if (code != FUSEDWASI_P2_OK) {
        fusedwasi_p2_sys_close(&made);
        return fusedwasi_p1_of_code(code);
    }
#ifndef _WIN32  /* what the open of 0.2 does not take: appending, not blocking (on Windows fd_write seeks to the end itself) */
    if (made.fd >= 0 && (fdflags & (FUSEDWASI_P1_FD_APPEND | FUSEDWASI_P1_FD_NONBLOCK)) &&
        fcntl(made.fd, F_SETFL, fcntl(made.fd, F_GETFL) | fusedwasi_p1_native_fdflags(fdflags & (FUSEDWASI_P1_FD_APPEND | FUSEDWASI_P1_FD_NONBLOCK))) < 0) {
        int saved = errno;
        fusedwasi_p2_sys_close(&made);
        return fusedwasi_p1_errno(saved);
    }
#endif
    e->kind = made.is_dir ? FUSEDWASI_P1_K_DIR : FUSEDWASI_P1_K_FILE;
    e->filetype = fusedwasi_p1_filetype_of_type(st.type);
    e->d = made;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* A name to be made that has a slash after it (a link): the code when it can only be an error; FUSEDWASI_P2_OK when it has none */
static inline int fusedwasi_p1_leaf_slash(fusedwasi_p2_dirref parent, const char *leaf) {
    size_t n = strlen(leaf);
    if (n == 0 || leaf[n - 1] != '/') return FUSEDWASI_P2_OK;
    char bare[FUSEDWASI_P2_NAME_MAX + 2];
    memcpy(bare, leaf, n - 1);
    bare[n - 1] = '\0';
    fusedwasi_p2_stat st;
    int code = fusedwasi_p2_sys_lstat(parent, bare, &st);
    if (code != FUSEDWASI_P2_OK) return code;
    return st.type == FUSEDWASI_P2_T_DIRECTORY ? FUSEDWASI_P2_E_EXIST : FUSEDWASI_P2_E_NOT_DIRECTORY;
}

static inline uint32_t fusedwasi_p1_be_mkdir(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, 0, &walk, &itself);
    if (rc) return rc;
    int code = itself ? FUSEDWASI_P2_E_EXIST : fusedwasi_p2_sys_mkdir(walk.dir, walk.leaf);
    fusedwasi_p2_walk_close(&walk);
    return fusedwasi_p1_of_code(code);
}

static inline uint32_t fusedwasi_p1_be_remove(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int isdir, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, 0, &walk, &itself);
    if (rc) return rc;
    int code = itself ? FUSEDWASI_P2_E_INVALID : isdir ? fusedwasi_p2_sys_rmdir(walk.dir, walk.leaf) : fusedwasi_p2_sys_unlink(walk.dir, walk.leaf);
    fusedwasi_p2_walk_close(&walk);
    if (!isdir && code == FUSEDWASI_P2_E_IS_DIRECTORY) { /* unlink of a directory: the answer of the system (the layer of 0.2 says is-directory for all) */
#if defined(__APPLE__)
        return FUSEDWASI_P1_E_PERM;
#elif defined(_WIN32)
        return FUSEDWASI_P1_E_ACCES;
#endif
    }
    return fusedwasi_p1_of_code(code);
}

/* a time to set as the layer takes it: a value, the time of the file as it is, or now (-1: now; -2: as it is) */
static inline int64_t fusedwasi_p1_new_time(uint64_t value, uint32_t flags, uint32_t set, uint32_t now, int64_t current, int64_t at) {
    if (flags & set) return (int64_t)value;
    if (flags & now) return at;
    return current;
}

static inline uint32_t fusedwasi_p1_be_times_fd(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e, uint64_t atim, uint64_t mtim, uint32_t flags, int *trapped) {
    (void)trapped;
    fusedwasi_p2_stat st;
    int code = fusedwasi_p2_sys_fstat(&e->d, &st);
    if (code == FUSEDWASI_P2_OK) {
        int64_t now = (int64_t)fusedwasi_p2_clock_ns(&g->core, 0);
        code = fusedwasi_p2_sys_set_times_fd(&e->d, fusedwasi_p1_new_time(atim, flags, FUSEDWASI_P1_T_ATIM, FUSEDWASI_P1_T_ATIM_NOW, st.atime_ns, now),
                                             fusedwasi_p1_new_time(mtim, flags, FUSEDWASI_P1_T_MTIM, FUSEDWASI_P1_T_MTIM_NOW, st.mtime_ns, now));
    }
    return fusedwasi_p1_of_code(code);
}

static inline uint32_t fusedwasi_p1_be_times_path(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint64_t atim,
                                          uint64_t mtim, uint32_t flags, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, follow, &walk, &itself);
    if (rc) return rc;
    fusedwasi_p2_stat st;
    int code = fusedwasi_p2_sys_lstat(walk.dir, walk.leaf, &st);  /* a link at the end was read by the walk if it was to be */
    if (code == FUSEDWASI_P2_OK) {
        int64_t now = (int64_t)fusedwasi_p2_clock_ns(&g->core, 0);
        code = fusedwasi_p2_sys_set_times(walk.dir, walk.leaf, fusedwasi_p1_new_time(atim, flags, FUSEDWASI_P1_T_ATIM, FUSEDWASI_P1_T_ATIM_NOW, st.atime_ns, now),
                                          fusedwasi_p1_new_time(mtim, flags, FUSEDWASI_P1_T_MTIM, FUSEDWASI_P1_T_MTIM_NOW, st.mtime_ns, now));
    }
    fusedwasi_p2_walk_close(&walk);
    return fusedwasi_p1_of_code(code);
}

/* the two directories and names of rename and link; both walks have to be closed by the caller */
static inline uint32_t fusedwasi_p1_be_pair(fusedwasi_p1_guest *g, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow, fusedwasi_p1_entry *d2,
                                    uint32_t p2, uint32_t l2, fusedwasi_p2_walk *walk1, fusedwasi_p2_walk *walk2) {
    int itself1, itself2;
    uint32_t rc = fusedwasi_p1_be_walk(g, d1, p1, l1, follow, walk1, &itself1);
    if (rc) return rc;
    rc = fusedwasi_p1_be_walk(g, d2, p2, l2, 0, walk2, &itself2);
    if (rc) {
        fusedwasi_p2_walk_close(walk1);
        return rc;
    }
    if (itself1 || itself2) {  /* a directory itself is neither moved nor made a link */
        fusedwasi_p2_walk_close(walk1);
        fusedwasi_p2_walk_close(walk2);
        return itself2 ? FUSEDWASI_P1_E_EXIST : FUSEDWASI_P1_E_INVAL;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_rename(fusedwasi_p1_guest *g, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, fusedwasi_p1_entry *d2, uint32_t p2,
                                      uint32_t l2, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk1, walk2;
    uint32_t rc = fusedwasi_p1_be_pair(g, d1, p1, l1, 0, d2, p2, l2, &walk1, &walk2);
    if (rc) return rc;
    int code = fusedwasi_p2_sys_rename(walk1.dir, walk1.leaf, walk2.dir, walk2.leaf);
    fusedwasi_p2_walk_close(&walk1);
    fusedwasi_p2_walk_close(&walk2);
    return fusedwasi_p1_of_code(code);
}

static inline uint32_t fusedwasi_p1_be_link(fusedwasi_p1_guest *g, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow, fusedwasi_p1_entry *d2,
                                    uint32_t p2, uint32_t l2, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk1, walk2;
    uint32_t rc = fusedwasi_p1_be_pair(g, d1, p1, l1, follow, d2, p2, l2, &walk1, &walk2);
    if (rc) return rc;
    int code = fusedwasi_p1_leaf_slash(walk2.dir, walk2.leaf);
    if (code == FUSEDWASI_P2_OK) code = fusedwasi_p2_sys_link(walk1.dir, walk1.leaf, walk2.dir, walk2.leaf);  /* (the walk read a link at the end if it was to be followed) */
    fusedwasi_p2_walk_close(&walk1);
    fusedwasi_p2_walk_close(&walk2);
    return fusedwasi_p1_of_code(code);
}

static inline uint32_t fusedwasi_p1_be_symlink(fusedwasi_p1_guest *g, const unsigned char *target, uint32_t tlen, fusedwasi_p1_entry *dir, uint32_t path,
                                       uint32_t len, int *trapped) {
    (void)trapped;
    char text[FUSEDWASI_P2_PATH_MAX];
    if (tlen >= sizeof text) return FUSEDWASI_P1_E_NAMETOOLONG;
    memcpy(text, target, tlen);
    text[tlen] = '\0';
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, 0, &walk, &itself);
    if (rc) return rc;
    int code = itself ? FUSEDWASI_P2_E_EXIST : fusedwasi_p1_leaf_slash(walk.dir, walk.leaf);
    if (code == FUSEDWASI_P2_OK) code = fusedwasi_p2_sys_symlink(text, walk.dir, walk.leaf);
    fusedwasi_p2_walk_close(&walk);
    return fusedwasi_p1_of_code(code);
}

static inline uint32_t fusedwasi_p1_be_readlink(fusedwasi_p1_guest *g, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, char *out, size_t cap,
                                        size_t *used, int *trapped) {
    (void)trapped;
    fusedwasi_p2_walk walk;
    int itself;
    uint32_t rc = fusedwasi_p1_be_walk(g, dir, path, len, 0, &walk, &itself);
    if (rc) return rc;
    char text[FUSEDWASI_P1_LINK_MAX + 2];  /* the whole target: the layer says "too long" for a buffer that is too small (Windows), and 0.1 cuts it to the room it has */
    int code = itself ? FUSEDWASI_P2_E_INVALID : fusedwasi_p2_sys_readlink(walk.dir, walk.leaf, text, sizeof text);
    fusedwasi_p2_walk_close(&walk);
    if (code != FUSEDWASI_P2_OK) return fusedwasi_p1_of_code(code);
    if (text[0] == '/') return FUSEDWASI_P1_E_PERM;  /* an absolute or rooted target is not shown, as readlink-at of 0.2 does (and path_symlink refuses to make one) */
    size_t n = strlen(text), room = cap < FUSEDWASI_P1_LINK_MAX ? cap : FUSEDWASI_P1_LINK_MAX;
    *used = cap ? (n < room ? n : room) : 0;  /* a buffer of 0 bytes still says whether this is a link */
    memcpy(out, text, *used);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_list(fusedwasi_p1_guest *g, fusedwasi_p1_entry *e, fusedwasi_p1_dirlist *list, int *trapped) {
    (void)trapped;
    char **names;
    uint8_t *types;
    uint32_t count;
    int code = fusedwasi_p2_sys_list(&e->d, &names, &types, &count);
    if (code != FUSEDWASI_P2_OK) return fusedwasi_p1_of_code(code);
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    for (uint32_t i = 0; i < count + 2 && rc == FUSEDWASI_P1_E_SUCCESS; i++) {  /* "." and "..", then the names */
        const char *name = i == 0 ? "." : i == 1 ? ".." : names[i - 2];
        uint8_t type = i < 2 ? FUSEDWASI_P1_FT_DIR : fusedwasi_p1_filetype_of_type(types[i - 2]);
        fusedwasi_p2_stat st;
        uint64_t ino = 0;
        if (fusedwasi_p2_sys_lstat(FUSEDWASI_P2_DIRREF_OF(&e->d), name, &st) == FUSEDWASI_P2_OK) ino = st.ino;
        if (fusedwasi_p1_dirlist_add(list, name, ino, type) < 0) rc = FUSEDWASI_P1_E_NOMEM;
    }
    for (uint32_t i = 0; i < count; i++) free(names[i]);
    free(names);
    free(types);
    return rc;
}

/* ---- the calls that use them ------------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_filestat_of_fd(fusedwasi_p1_guest *g, uint32_t fd, uint32_t ptr, int legacy, int *trapped) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    fusedwasi_p1_stat st;
    if (e->kind == FUSEDWASI_P1_K_STD) {
        if (fusedwasi_p1_std_is_real(g, e) && fusedwasi_p1_be_stat_fd(g, e, &st, trapped) == FUSEDWASI_P1_E_SUCCESS) return fusedwasi_p1_put_filestat(g, ptr, &st, legacy);  /* what it is */
        *trapped = 0;
        memset(&st, 0, sizeof st);  /* a stream of the host: nothing to say but that it is a character device (with one name) */
        st.filetype = FUSEDWASI_P1_FT_CHAR;
        st.nlink = 1;
        return fusedwasi_p1_put_filestat(g, ptr, &st, legacy);
    }
    if (!(e->rights & FUSEDWASI_P1_R_FD_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    uint32_t rc = fusedwasi_p1_be_stat_fd(g, e, &st, trapped);
    return rc ? rc : fusedwasi_p1_put_filestat(g, ptr, &st, legacy);
}

static inline uint32_t fusedwasi_p1_filestat_of_path(fusedwasi_p1_guest *g, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len,
                                             uint32_t ptr, int legacy, int *trapped) {
    fusedwasi_p1_entry *dir = fusedwasi_p1_entry_of(g, fd);
    if (dir == NULL) return FUSEDWASI_P1_E_BADF;
    if (!(dir->rights & FUSEDWASI_P1_R_PATH_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    fusedwasi_p1_stat st;
    uint32_t rc = fusedwasi_p1_be_stat_path(g, dir, path, len, (flags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, &st, trapped);
    return rc ? rc : fusedwasi_p1_put_filestat(g, ptr, &st, legacy);
}

static inline uint32_t fusedwasi_p1_fd_filestat_get(void *vg, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    return fusedwasi_p1_filestat_of_fd(g, fd, ptr, 0, trapped);
}

static inline uint32_t fusedwasi_p1_path_filestat_get(void *vg, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len, uint32_t ptr,
                                              int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    return fusedwasi_p1_filestat_of_path(g, fd, flags, path, len, ptr, 0, trapped);
}

static inline uint32_t fusedwasi_p1_fd_readdir(void *vg, uint32_t fd, uint32_t buf, uint32_t size, uint64_t cookie, uint32_t used,
                                       int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READDIR)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (!fusedwasi_p1_in_memory(g, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (cookie == 0 || e->list == NULL) {  /* the listing is made again when the program starts from the beginning */
        fusedwasi_p1_dirlist *list = (fusedwasi_p1_dirlist *)FUSEDWASI_P1_CALLOC(1, sizeof(fusedwasi_p1_dirlist));
        if (list == NULL) return FUSEDWASI_P1_E_NOMEM;
        uint32_t rc = fusedwasi_p1_be_list(g, e, list, trapped);
        if (rc) {
            fusedwasi_p1_dirlist_free(list);
            return rc;
        }
        if (list->count > 1) qsort(list->items, list->count, sizeof(fusedwasi_p1_dirent), fusedwasi_p1_dirent_order);  /* the same order for both ways */
        fusedwasi_p1_dirlist_free(e->list);
        e->list = list;
    }
    uint32_t at = 0;
    for (uint64_t i = cookie; i < e->list->count && at < size; i++) {
        const fusedwasi_p1_dirent *item = &e->list->items[i];
        unsigned char head[24];
        memset(head, 0, sizeof head);
        uint64_t next = i + 1;
        for (int k = 0; k < 8; k++) head[k] = (unsigned char)(next >> (8 * k));
        for (int k = 0; k < 8; k++) head[8 + k] = (unsigned char)(item->ino >> (8 * k));
        for (int k = 0; k < 4; k++) head[16 + k] = (unsigned char)(item->namelen >> (8 * k));
        head[20] = item->type;
        size_t room = size - at, part = room < sizeof head ? room : sizeof head;  /* the last entry may be cut short */
        memcpy(FUSEDWASI_P1_MEM(g) + buf + at, head, part);
        at += (uint32_t)part;
        if (part < sizeof head) break;
        room = size - at;
        part = room < item->namelen ? room : item->namelen;
        memcpy(FUSEDWASI_P1_MEM(g) + buf + at, item->name, part);
        at += (uint32_t)part;
    }
    return fusedwasi_p1_put_u32(g, used, at) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_path_open(void *vg, uint32_t fd, uint32_t dirflags, uint32_t path, uint32_t len, uint32_t oflags,
                                      uint64_t base, uint64_t inheriting, uint32_t fdflags, uint32_t out, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir = fusedwasi_p1_entry_of(g, fd);
    if (dir == NULL) return FUSEDWASI_P1_E_BADF;
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if ((oflags >> 4) != 0 || (fdflags >> 5) != 0) return FUSEDWASI_P1_E_INVAL;
    if (!(dir->rights & FUSEDWASI_P1_R_PATH_OPEN)) return FUSEDWASI_P1_E_NOTCAPABLE;
    base &= dir->inheriting & FUSEDWASI_P1_R_ALL;
    inheriting &= dir->inheriting & FUSEDWASI_P1_R_ALL;
    if (oflags & FUSEDWASI_P1_O_CREAT) {
        if (oflags & FUSEDWASI_P1_O_DIRECTORY) return FUSEDWASI_P1_E_INVAL;
        if (!(dir->rights & FUSEDWASI_P1_R_PATH_CREATE_FILE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    }
    if ((oflags & FUSEDWASI_P1_O_TRUNC) && !(dir->rights & FUSEDWASI_P1_R_PATH_FILESTAT_SET_SIZE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    int64_t number = fusedwasi_p1_new_fd(g->state);
    if (number < 0) return FUSEDWASI_P1_E_NOMEM;
    dir = &g->state->entries[fd];  /* the table may have moved when it grew */
    fusedwasi_p1_entry *e = &g->state->entries[number];
    memset(e, 0, sizeof *e);
    uint32_t rc = fusedwasi_p1_be_open(g, dir, path, len, (dirflags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, oflags, base, fdflags, e, trapped);
    if (rc) {
        fusedwasi_p1_release(e);
        return rc;
    }
    e->rights = base;
    if (e->kind == FUSEDWASI_P1_K_DIR) e->rights &= ~(FUSEDWASI_P1_R_FD_SEEK | FUSEDWASI_P1_R_FD_TELL);  /* a directory is not seeked */
    e->inheriting = inheriting;
    e->flags = (uint16_t)fdflags;
    if (!fusedwasi_p1_put_u32(g, out, (uint32_t)number)) {
        fusedwasi_p1_release(e);
        return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- writing, creating, removing ---------------------------------------------------------------------------------------- */

/* the entry of a file descriptor that has the right, or an errno of WASI */
static inline uint32_t fusedwasi_p1_entry_with(fusedwasi_p1_guest *g, uint32_t fd, uint64_t right, fusedwasi_p1_entry **out) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    *out = e;
    if (e->kind != FUSEDWASI_P1_K_STD && !(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the directory of a call on a path: it has to be one and to have the right */
static inline uint32_t fusedwasi_p1_dir_with(fusedwasi_p1_guest *g, uint32_t fd, uint64_t right, fusedwasi_p1_entry **out) {
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, right, out);
    if (rc) return rc;
    return (*out)->kind == FUSEDWASI_P1_K_DIR ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_NOTDIR;
}

static inline uint32_t fusedwasi_p1_sync_entry(fusedwasi_p1_guest *g, uint32_t fd, uint64_t right, int data_only) {
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, right, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SUCCESS;  /* the stream is Python's: it flushes when it wants */
#if FUSEDWASI_P1_DIRS_ARE_PATHS
    (void)data_only;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_SUCCESS;  /* not an open descriptor: nothing to flush */
    return FUSEDWASI_P1_SYNC(e->d.fd) < 0 ? fusedwasi_p1_errno(errno) : FUSEDWASI_P1_E_SUCCESS;
#else
#ifdef __linux__
    int failed = data_only ? fdatasync(e->d.fd) : FUSEDWASI_P1_SYNC(e->d.fd);
#else
    (void)data_only;
    int failed = FUSEDWASI_P1_SYNC(e->d.fd);
#endif
    return failed < 0 ? fusedwasi_p1_errno(errno) : FUSEDWASI_P1_E_SUCCESS;
#endif
}

static inline uint32_t fusedwasi_p1_fd_datasync(void *vg, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_sync_entry(g, fd, FUSEDWASI_P1_R_FD_DATASYNC, 1);
}

static inline uint32_t fusedwasi_p1_fd_sync(void *vg, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    return fusedwasi_p1_sync_entry(g, fd, FUSEDWASI_P1_R_FD_SYNC, 0);
}

static inline int fusedwasi_p1_truncate_native(int osfd, uint64_t size) {
#ifdef _WIN32
    return _chsize_s(osfd, (__int64)size);  /* the error itself, not -1 */
#else
    return ftruncate(osfd, (off_t)size) < 0 ? errno : 0;
#endif
}

static inline uint32_t fusedwasi_p1_set_size(fusedwasi_p1_entry *e, uint64_t size) {
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (size > (uint64_t)INT64_MAX) return FUSEDWASI_P1_E_FBIG;
    return fusedwasi_p1_errno(fusedwasi_p1_truncate_native(e->d.fd, size));
}

static inline uint32_t fusedwasi_p1_fd_filestat_set_size(void *vg, uint32_t fd, uint64_t size, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE, &e);
    return rc ? rc : fusedwasi_p1_set_size(e, size);
}

/* makes the file at least offset + length long (the file system may not reserve the room, but it is as long) */
static inline uint32_t fusedwasi_p1_fd_allocate(void *vg, uint32_t fd, uint64_t offset, uint64_t length, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, FUSEDWASI_P1_R_FD_ALLOCATE, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (offset > (uint64_t)INT64_MAX || length > (uint64_t)INT64_MAX - offset) return FUSEDWASI_P1_E_FBIG;
#if FUSEDWASI_P1_NATIVE_POLL && (defined(__linux__) || defined(__FreeBSD__))
    {  /* space is really reserved (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE) */
        int err = posix_fallocate(e->d.fd, (off_t)offset, (off_t)length);
        if (err == 0) return FUSEDWASI_P1_E_SUCCESS;
        if (err != EINVAL && err != EOPNOTSUPP && err != ENODEV) return fusedwasi_p1_errno(err);  /* a file system that cannot do it gets the old way */
    }
#endif
    fusedwasi_p1_stat st;
    memset(&st, 0, sizeof st);
    rc = fusedwasi_p1_be_stat_fd(g, e, &st, trapped);
    if (rc || *trapped) return rc;
    if (st.filetype != FUSEDWASI_P1_FT_REGULAR) return FUSEDWASI_P1_E_SPIPE; /* a pipe: macOS would truncate it without a word */
    return st.size >= offset + length ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_set_size(e, offset + length);
}

static inline uint32_t fusedwasi_p1_times_flags_ok(uint32_t flags) {
    return flags > 15 || ((flags & FUSEDWASI_P1_T_ATIM) && (flags & FUSEDWASI_P1_T_ATIM_NOW)) || ((flags & FUSEDWASI_P1_T_MTIM) && (flags & FUSEDWASI_P1_T_MTIM_NOW))
               ? FUSEDWASI_P1_E_INVAL
               : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_filestat_set_times(void *vg, uint32_t fd, uint64_t atim, uint64_t mtim, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_TIMES, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_BADF;
    rc = fusedwasi_p1_times_flags_ok(flags);
    return rc ? rc : fusedwasi_p1_be_times_fd(g, e, atim, mtim, flags, trapped);
}

static inline uint32_t fusedwasi_p1_path_filestat_set_times(void *vg, uint32_t fd, uint32_t lookup, uint32_t path, uint32_t len, uint64_t atim,
                                                    uint64_t mtim, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R_PATH_FILESTAT_SET_TIMES, &dir);
    if (rc) return rc;
    rc = fusedwasi_p1_times_flags_ok(flags);
    return rc ? rc : fusedwasi_p1_be_times_path(g, dir, path, len, (lookup & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, atim, mtim, flags, trapped);
}

static inline uint32_t fusedwasi_p1_fd_fdstat_set_flags(void *vg, uint32_t fd, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(g, fd, FUSEDWASI_P1_R_FD_FDSTAT_SET_FLAGS, &e);
    if (rc) return rc;
    if (flags > 31) return FUSEDWASI_P1_E_INVAL;
    if (e->kind != FUSEDWASI_P1_K_FILE) return e->kind == FUSEDWASI_P1_K_STD ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_BADF;
#ifndef _WIN32
    int current = fcntl(e->d.fd, F_GETFL);
    if (current < 0) return fusedwasi_p1_errno(errno);
    int all = fusedwasi_p1_native_fdflags(FUSEDWASI_P1_FD_APPEND | FUSEDWASI_P1_FD_DSYNC | FUSEDWASI_P1_FD_NONBLOCK | FUSEDWASI_P1_FD_RSYNC | FUSEDWASI_P1_FD_SYNC);
    if (fcntl(e->d.fd, F_SETFL, (current & ~all) | fusedwasi_p1_native_fdflags(flags)) < 0) return fusedwasi_p1_errno(errno);
#endif
    e->flags = (uint16_t)flags;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the rights can only be given up, never gained */
static inline uint32_t fusedwasi_p1_fd_fdstat_set_rights(void *vg, uint32_t fd, uint64_t base, uint64_t inheriting, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the rights of a standard stream are fixed: giving some up (or none) is fine, getting more is not (the narrowing is not kept) */
        uint64_t have = e->d.fd == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT;
        return (base & ~have) != 0 || inheriting != 0 ? FUSEDWASI_P1_E_NOTCAPABLE : FUSEDWASI_P1_E_SUCCESS;
    }
    if ((base & ~e->rights) != 0 || (inheriting & ~e->inheriting) != 0) return FUSEDWASI_P1_E_NOTCAPABLE;
    e->rights = base;
    e->inheriting = inheriting;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_path_create_directory(void *vg, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R_PATH_CREATE_DIRECTORY, &dir);
    return rc ? rc : fusedwasi_p1_be_mkdir(g, dir, path, len, trapped);
}

static inline uint32_t fusedwasi_p1_path_remove_directory(void *vg, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R_PATH_REMOVE_DIRECTORY, &dir);
    return rc ? rc : fusedwasi_p1_be_remove(g, dir, path, len, 1, trapped);
}

static inline uint32_t fusedwasi_p1_path_unlink_file(void *vg, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R_PATH_UNLINK_FILE, &dir);
    return rc ? rc : fusedwasi_p1_be_remove(g, dir, path, len, 0, trapped);
}

/* ---- positions, renaming, links, waiting ---------------------------------------------------------------------------------- */

/* read or write at a position, leaving the position of the descriptor where it was */
static inline long long fusedwasi_p1_at_position(int osfd, void *buf, size_t n, uint64_t offset, int writing) {
#ifdef _WIN32
    long long saved = _lseeki64(osfd, 0, SEEK_CUR);  /* Windows has no pread: seek, do it, seek back */
    if (saved < 0 || _lseeki64(osfd, (long long)offset, SEEK_SET) < 0) return -1;
    int count = (int)(n < 0x7fffffff ? n : 0x7fffffff);
    long long done = writing ? _write(osfd, buf, (unsigned int)count) : _read(osfd, buf, (unsigned int)count);
    int err = errno;
    _lseeki64(osfd, saved, SEEK_SET);
    errno = err;
    return done;
#else
    return writing ? pwrite(osfd, buf, n, (off_t)offset) : pread(osfd, buf, n, (off_t)offset);
#endif
}

static inline uint32_t fusedwasi_p1_fd_at(void *vg, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t out, int writing) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    /* the specification asks for fd_seek as well; wasi-libc opens a file for writing without it and calls pwrite all the same */
    uint64_t needed = writing ? FUSEDWASI_P1_R_FD_WRITE : FUSEDWASI_P1_R_FD_READ;
    if (!(e->rights & needed)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (offset > (uint64_t)INT64_MAX) return FUSEDWASI_P1_E_INVAL;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        long long n = fusedwasi_p1_at_position(e->d.fd, FUSEDWASI_P1_MEM(g) + base, len, offset + total, writing);
        if (n < 0) {
            if (total) break;
            return fusedwasi_p1_errno(errno);
        }
        total += (uint64_t)n;
        if ((uint32_t)n < len) break;
    }
    return fusedwasi_p1_put_u32(g, out, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_fd_pread(void *vg, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nread,
                                     int *trapped) {
    (void)trapped;
    return fusedwasi_p1_fd_at(vg, fd, iovs, count, offset, nread, 0);
}

static inline uint32_t fusedwasi_p1_fd_pwrite(void *vg, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nwritten,
                                      int *trapped) {
    (void)trapped;
    return fusedwasi_p1_fd_at(vg, fd, iovs, count, offset, nwritten, 1);
}

static inline uint32_t fusedwasi_p1_path_rename(void *vg, uint32_t fd, uint32_t old, uint32_t old_len, uint32_t new_fd, uint32_t new_,
                                        uint32_t new_len, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *from, *to;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R(16), &from);  /* path_rename_source */
    if (rc) return rc;
    rc = fusedwasi_p1_dir_with(g, new_fd, FUSEDWASI_P1_R(17), &to);  /* path_rename_target */
    return rc ? rc : fusedwasi_p1_be_rename(g, from, old, old_len, to, new_, new_len, trapped);
}

static inline uint32_t fusedwasi_p1_path_link(void *vg, uint32_t fd, uint32_t flags, uint32_t old, uint32_t old_len, uint32_t new_fd,
                                      uint32_t new_, uint32_t new_len, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *from, *to;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R(11), &from);  /* path_link_source */
    if (rc) return rc;
    rc = fusedwasi_p1_dir_with(g, new_fd, FUSEDWASI_P1_R(12), &to);  /* path_link_target */
    return rc ? rc : fusedwasi_p1_be_link(g, from, old, old_len, (flags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, to, new_, new_len, trapped);
}

/* A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made. */
static inline uint32_t fusedwasi_p1_path_symlink(void *vg, uint32_t target, uint32_t target_len, uint32_t fd, uint32_t path, uint32_t len,
                                         int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R(24), &dir);  /* path_symlink */
    if (rc) return rc;
    if (!fusedwasi_p1_in_memory(g, target, target_len)) return FUSEDWASI_P1_E_FAULT;
    if (target_len == 0) return FUSEDWASI_P1_E_NOENT;
    if (memchr(FUSEDWASI_P1_MEM(g) + target, 0, target_len) != NULL) return FUSEDWASI_P1_E_INVAL;
    if (FUSEDWASI_P1_MEM(g)[target] == '/') return FUSEDWASI_P1_E_NOTCAPABLE;
    return fusedwasi_p1_be_symlink(g, FUSEDWASI_P1_MEM(g) + target, target_len, dir, path, len, trapped);
}

static inline uint32_t fusedwasi_p1_path_readlink(void *vg, uint32_t fd, uint32_t path, uint32_t len, uint32_t buf, uint32_t buf_len,
                                          uint32_t used, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(g, fd, FUSEDWASI_P1_R(15), &dir);  /* path_readlink */
    if (rc) return rc;
    if (!fusedwasi_p1_in_memory(g, buf, buf_len)) return FUSEDWASI_P1_E_FAULT;
    char text[FUSEDWASI_P1_LINK_MAX];
    size_t n = 0;
    rc = fusedwasi_p1_be_readlink(g, dir, path, len, text, buf_len < sizeof text ? buf_len : sizeof text, &n, trapped);
    if (rc || *trapped) return rc;
    memcpy(FUSEDWASI_P1_MEM(g) + buf, text, n);
    return fusedwasi_p1_put_u32(g, used, (uint32_t)n) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* poll_oneoff: the descriptors of files are always ready; what makes the program wait is a clock, and it sleeps (in C, without the
   interpreter, so that other threads run) until the first one. `legacy`: the first snapshot, whose clock subscription starts with an identifier. */
/* how long a clock subscription still has to wait (0: it is due), or an errno of WASI */
static inline uint32_t fusedwasi_p1_subscription_wait(fusedwasi_p1_guest *g, uint32_t sub, int legacy, uint64_t entry_now[2], int have[2],
                                              uint64_t *wait, int *trapped) {
    uint32_t shift = legacy ? 8 : 0;
    uint32_t id = 0, flags16 = 0;
    uint64_t timeout = 0;
    fusedwasi_p1_get_u32(g, sub + 16 + shift, &id);
    uint32_t lo = 0, hi = 0;
    fusedwasi_p1_get_u32(g, sub + 24 + shift, &lo);
    fusedwasi_p1_get_u32(g, sub + 28 + shift, &hi);
    timeout = ((uint64_t)hi << 32) | lo;
    flags16 = FUSEDWASI_P1_MEM(g)[sub + 40 + shift] | ((uint32_t)FUSEDWASI_P1_MEM(g)[sub + 41 + shift] << 8);
    if (id > 1) return FUSEDWASI_P1_E_INVAL;  /* the clocks of the process and the thread are not something to wait for */
    if (flags16 & ~1u) return FUSEDWASI_P1_E_INVAL;  /* subclockflags has one flag, abstime */
    if (!(flags16 & 1)) {
        *wait = timeout;
        return FUSEDWASI_P1_E_SUCCESS;
    }
    if (!have[id]) {
        uint32_t rc = fusedwasi_p1_clock_ns(g, id, &entry_now[id]);
        if (rc) return rc;
        have[id] = 1;
    }
    *wait = timeout > entry_now[id] ? timeout - entry_now[id] : 0;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* A subscription to a descriptor: 0 if it is answered at once (a bad descriptor, a right that is missing, a regular file, a directory, a stream that
   goes through a hook, the standard output); 1 if the system has to say when it is ready (a pipe, a FIFO, a terminal, a socket, the standard input).
   `*error` is the error of the event (a right that is missing is NOTCAPABLE, as the descriptor is not one to wait for) and `*fd` the descriptor to poll. */
static inline int fusedwasi_p1_poll_kind(fusedwasi_p1_guest *g, uint32_t sub, uint8_t tag, uint32_t *error, int *fd) {
    uint32_t number = 0;
    fusedwasi_p1_get_u32(g, sub + 16, &number);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, number);
    *error = FUSEDWASI_P1_E_SUCCESS;
    *fd = -1;
    if (e == NULL) {
        *error = FUSEDWASI_P1_E_BADF;
        return 0;
    }
    uint64_t need = FUSEDWASI_P1_R_POLL_FD_READWRITE | (tag == 1 ? FUSEDWASI_P1_R_FD_READ : FUSEDWASI_P1_R_FD_WRITE);
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the rights of the standard streams are fixed and include polling */
#if FUSEDWASI_P1_NATIVE_POLL
        if (tag == 1 && e->d.fd == 0 && g->state->core->std[0] == NULL) {
            *fd = 0;
            return 1;
        }
#endif
        return 0;
    }
    if ((e->rights & need) != need) {
        *error = FUSEDWASI_P1_E_NOTCAPABLE;
        return 0;
    }
#if FUSEDWASI_P1_NATIVE_POLL
    if (e->kind == FUSEDWASI_P1_K_FILE && e->d.fd >= 0) {
        struct stat st;
        if (fstat(e->d.fd, &st) == 0 && !S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            *fd = e->d.fd;
            return 1;
        }
    }
#endif
    return 0;
}

static inline uint32_t fusedwasi_p1_poll(fusedwasi_p1_guest *g, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents, int legacy,
                                 int *trapped) {
    uint32_t stride = legacy ? 56 : 48;
    if (count == 0) return FUSEDWASI_P1_E_INVAL;
    if (!fusedwasi_p1_in_memory(g, in, (uint64_t)count * stride) || !fusedwasi_p1_in_memory(g, out, (uint64_t)count * 32)) return FUSEDWASI_P1_E_FAULT;
    uint64_t entry_now[2] = {0, 0}, shortest = 0;
    int have[2] = {0, 0}, immediate = 0, timed = 0;  /* timed: a clock was subscribed, so `shortest` is a time (it may be UINT64_MAX: forever) */
    uint32_t polled = 0;                              /* how many descriptors the system has to say about */
    for (uint32_t i = 0; i < count; i++) {  /* first, what is ready and how long the clocks wait */
        uint32_t sub = in + i * stride;
        uint8_t tag = FUSEDWASI_P1_MEM(g)[sub + 8];
        if (tag == 0) {
            uint64_t wait;
            uint32_t rc = fusedwasi_p1_subscription_wait(g, sub, legacy, entry_now, have, &wait, trapped);
            if (rc == FUSEDWASI_P1_E_SUCCESS && !*trapped && (!timed || wait < shortest)) {
                shortest = wait;
                timed = 1;
            }
            if (rc == FUSEDWASI_P1_E_INVAL) immediate = 1;
            else if (rc || *trapped) return rc;
        } else if (tag == 1 || tag == 2) {
            uint32_t error;
            int fd;
            if (fusedwasi_p1_poll_kind(g, sub, tag, &error, &fd)) polled++;
            else immediate = 1;
        } else {
            return FUSEDWASI_P1_E_INVAL;
        }
    }
    (void)polled;  /* only the native poll uses the count */
    /* what the system says about the descriptors: ready or not, and the end of the wait (a clock, or the first descriptor that is ready) */
    short *revents = NULL; (void)revents;
    int *slot = NULL;
    uint64_t waited = 0;
#if FUSEDWASI_P1_NATIVE_POLL
    if (polled) {
        struct pollfd *fds = (struct pollfd *)FUSEDWASI_P1_MALLOC(polled * sizeof *fds);
        revents = (short *)FUSEDWASI_P1_MALLOC(polled * sizeof *revents);
        slot = (int *)FUSEDWASI_P1_MALLOC(count * sizeof *slot);
        if (fds == NULL || revents == NULL || slot == NULL) {
            FUSEDWASI_P1_FREE(fds);
            FUSEDWASI_P1_FREE(revents);
            FUSEDWASI_P1_FREE(slot);
            return FUSEDWASI_P1_E_NOMEM;
        }
        uint32_t k = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t sub = in + i * stride, error;
            uint8_t tag = FUSEDWASI_P1_MEM(g)[sub + 8];
            int fd;
            slot[i] = -1;
            if ((tag == 1 || tag == 2) && fusedwasi_p1_poll_kind(g, sub, tag, &error, &fd)) {
                fds[k].fd = fd;
                fds[k].events = tag == 1 ? POLLIN : POLLOUT;
                fds[k].revents = 0;
                slot[i] = (int)k++;
            }
        }
        uint64_t started = 0;
        int have_start = g->state->core->clock(g->state->core, 1, &started) == 0;
        int ready = 0;
        for (;;) {
            int timeout = 0;  /* ms; the wait is for the clocks, or for ever when there is none */
            if (!immediate) {
                if (!timed) {
                    timeout = -1;
                } else {
                    uint64_t spent = 0, now;
                    if (have_start && g->state->core->clock(g->state->core, 1, &now) == 0) spent = now - started;
                    uint64_t left = shortest > spent ? shortest - spent : 0;
                    timeout = left > 2000000000ull ? 2000000000 : (int)((left + 999999u) / 1000000u);
                }
            }
            ready = poll(fds, polled, timeout);
            if (ready < 0 && errno == EINTR) continue;
            break;
        }
        for (uint32_t i = 0; i < polled; i++) revents[i] = fds[i].revents;
        FUSEDWASI_P1_FREE(fds);
        if (ready <= 0 && !immediate && timed) {
            waited = shortest;  /* the wait is over: the clocks are due */
        } else if (ready > 0 && have_start) {
            uint64_t now;
            if (g->state->core->clock(g->state->core, 1, &now) == 0) waited = now - started;
        }
    } else
#endif
    if (!immediate && timed && shortest > 0) {
        if (g->state->core->sleep(g->state->core, shortest) != 0) return FUSEDWASI_P1_E_NOTSUP;
        waited = shortest;
    }
    uint32_t made = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sub = in + i * stride, event = out + made * 32;
        uint8_t tag = FUSEDWASI_P1_MEM(g)[sub + 8];
        uint32_t error = FUSEDWASI_P1_E_SUCCESS;
        uint64_t nbytes = 0;
        uint16_t flags = 0;
        if (tag == 0) {
            uint64_t wait;
            error = fusedwasi_p1_subscription_wait(g, sub, legacy, entry_now, have, &wait, trapped);
            if (*trapped) {
                FUSEDWASI_P1_FREE(revents);
                FUSEDWASI_P1_FREE(slot);
                return 0;
            }
            if (error == FUSEDWASI_P1_E_SUCCESS && wait > waited) continue;  /* not yet */
        } else {
            uint32_t number = 0;
            fusedwasi_p1_get_u32(g, sub + 16, &number);
            fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(g, number);
            int fd;
#if FUSEDWASI_P1_NATIVE_POLL
            if (slot != NULL && slot[i] >= 0) {  /* the system has said */
                short r = revents[slot[i]];
                if (!(r & (POLLIN | POLLOUT | POLLHUP | POLLERR | POLLNVAL))) continue;  /* not ready */
                if (r & POLLNVAL) error = FUSEDWASI_P1_E_BADF;
                else if (r & (POLLHUP | POLLERR) && !(r & (POLLIN | POLLOUT))) flags = 1;  /* fd_readwrite_hangup */
                if (tag == 1 && !(r & POLLNVAL)) {
                    int pending = 0;
                    if (ioctl(e->kind == FUSEDWASI_P1_K_STD ? 0 : e->d.fd, FIONREAD, &pending) == 0 && pending > 0) nbytes = (uint64_t)pending;
                }
            } else
#endif
            if (!fusedwasi_p1_poll_kind(g, sub, tag, &error, &fd) && error != FUSEDWASI_P1_E_SUCCESS) {
                /* a bad descriptor or a right that is missing: told in the event */
            } else if (e != NULL && e->kind == FUSEDWASI_P1_K_FILE && tag == 1) {
                fusedwasi_p1_stat st;
                memset(&st, 0, sizeof st);
                long long at = FUSEDWASI_P1_LSEEK(e->d.fd, 0, SEEK_CUR);
                if (at >= 0 && fusedwasi_p1_be_stat_fd(g, e, &st, trapped) == FUSEDWASI_P1_E_SUCCESS && st.size > (uint64_t)at) nbytes = st.size - (uint64_t)at;
                *trapped = 0;
            }
        }
        memset(FUSEDWASI_P1_MEM(g) + event, 0, 32);
        memcpy(FUSEDWASI_P1_MEM(g) + event, FUSEDWASI_P1_MEM(g) + sub, 8);  /* userdata */
        fusedwasi_p1_put_u16(g, event + 8, (uint16_t)error);
        FUSEDWASI_P1_MEM(g)[event + 10] = tag;
        if (tag != 0) {
            fusedwasi_p1_put_u64(g, event + 16, nbytes);
            fusedwasi_p1_put_u16(g, event + 24, flags);
        }
        made++;
    }
    FUSEDWASI_P1_FREE(revents);
    FUSEDWASI_P1_FREE(slot);
    return fusedwasi_p1_put_u32(g, nevents, made) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_poll_oneoff(void *vg, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents, int *trapped) {
    FUSEDWASI_P1_GUEST(vg);
    return fusedwasi_p1_poll(g, in, out, count, nevents, 0, trapped);
}

/* ---- sockets: a program cannot make one, so there is none to speak of --------------------------------------------------------- */

/* a descriptor is never a socket (a program cannot make one): the call says so, or that there is no such descriptor */
#define FUSEDWASI_P1_SOCKET(name, ...) \
    static inline uint32_t fusedwasi_p1_##name(void *vg, uint32_t fd, __VA_ARGS__, int *trapped) { \
        FUSEDWASI_P1_GUEST(vg); \
        (void)trapped; \
        return fusedwasi_p1_entry_of(g, fd) == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_NOTSOCK; \
    }

FUSEDWASI_P1_SOCKET(sock_accept, uint32_t b, uint32_t c)
FUSEDWASI_P1_SOCKET(sock_recv, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
FUSEDWASI_P1_SOCKET(sock_send, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
FUSEDWASI_P1_SOCKET(sock_shutdown, uint32_t b)

#endif
