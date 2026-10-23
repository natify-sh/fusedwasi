/* The core of WASI 0.1 that `wasi_snapshot_preview1` and `wasi_unstable` (the first snapshot) share: the state of an instance and a
   function `fusedwasi_p1_<name>` for each call of the specification. Header only, included by wasi_p1.h and wasi_p1_unstable.h; the
   functions are `static inline`, so those the module does not import are not in the object. A module needs nothing of wasmext
   when it runs. See "Design: WASI" and "Design: C first, the seams" in DESIGN.md.

   There is no Python in this file: what the host decides (the standard streams, the clocks, the random bytes, the sleep, the end of
   the program) comes through the hooks of `fusedwasi_p1_config`, and where a hook is absent the system's own is used. The host that
   has Python is wasi_python.h. The files are the system's own: on Windows the backend is wasi_p1_files_win32.h (Win32 only), elsewhere
   openat and its kin (below).

   First stage: arguments, environment, clocks, random bytes, the standard streams, proc_exit. The calls of the file system answer
   ENOSYS (the descriptors beyond 2 are not open, so BADF where the specification asks for it). Written from the specification
   (the witx files of the wasi-0.1 branch of WebAssembly/WASI).

   A WASI call is `uint32_t fusedwasi_p1_<name>(void *state, parameters..., int *trapped)` (proc_exit returns nothing): a trap is reported
   through `trapped`, and the host knows what it was (Python: the exception it set). `fusedwasi_p1_create` makes the state.

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
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt") /* MSVC links it by this; MinGW (zig cc) is told with -lbcrypt, by wasmext._exe */
#endif
#endif
#if !FUSEDWASI_P1_DIRS_ARE_PATHS
#include <dirent.h>
#include <sys/stat.h>
#endif

/* the descriptors are the system's own (also where the files are those of Python: os.open gives them) and can be asked about with poll and fstat: not on Windows */
#ifndef _WIN32
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#if defined(__has_include) && !defined(__APPLE__)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define FUSEDWASI_P1_HAVE_GETRANDOM 1
#endif
#endif
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

enum { FUSEDWASI_P1_END_NONE = 0, FUSEDWASI_P1_END_EXIT, FUSEDWASI_P1_END_SIGNAL, FUSEDWASI_P1_END_TRAP };


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
    FUSEDWASI_P1_FT_UNKNOWN = 0, FUSEDWASI_P1_FT_BLOCK = 1, FUSEDWASI_P1_FT_CHAR = 2, FUSEDWASI_P1_FT_DIR = 3, FUSEDWASI_P1_FT_REGULAR = 4, FUSEDWASI_P1_FT_SOCKET_STREAM = 6,
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
    int osfd;            /* a file, or a directory when the system has openat (an operating system descriptor) */
    uint8_t owns_fd;     /* ... which is closed with the descriptor */
    uint64_t rights, inheriting;
    char *root;          /* a directory on Windows (where a directory is a path): the real path of the preopen it is under */
    char *rel;           /* ... and its place below it, '/' between names, "" at the root */
    char *preopen;       /* the name the program sees, for a preopen */
    size_t preopen_len;
    fusedwasi_p1_dirlist *list;  /* fd_readdir: the names of the directory, made at cookie 0 */
} fusedwasi_p1_entry;


/* ---- what the host gives: the configuration and the hooks ---------------------------------------------------------------- */

/* The return of a hook: 0 is success; a positive number is an errno of WASI (FUSEDWASI_P1_E_*) that the program is given as it is;
   FUSEDWASI_P1_HOOK_TRAP is a trap of the call (the host keeps what it needs to report it: Python keeps its exception). */
#define FUSEDWASI_P1_HOOK_TRAP 0x10000

typedef struct {
    int (*stdin_read)(void *user, uint8_t *buf, size_t cap, size_t *got);
    int (*stdout_write)(void *user, int fd, const uint8_t *buf, size_t n);  /* fd 1 or 2 */
    int (*clock)(void *user, uint32_t id, uint64_t *ns);
    int (*random)(void *user, uint8_t *buf, size_t n);
    int (*sleep)(void *user, uint64_t ns);
    void (*end)(void *user, int how, uint32_t value);  /* proc_exit, proc_raise: told before the program is marked as over */
} fusedwasi_p1_hooks;

typedef struct {
    const char *name;  /* what the program sees */
    size_t name_len;
    const char *host;  /* the real path of the directory */
    int readonly;
} fusedwasi_p1_preopen;

typedef struct {
    const char *args;  /* each ended by a NUL; NULL: the program's name alone */
    size_t args_size;
    uint32_t argc;
    const char *env;  /* "NAME=value", each ended by a NUL */
    size_t env_size;
    uint32_t envc;
    const fusedwasi_p1_preopen *preopens;
    size_t npreopens;
    const char *program;  /* argv[0] when args is NULL */
    fusedwasi_p1_hooks hooks;
    void *user;  /* the host's own state, given to every hook; never freed by the core */
} fusedwasi_p1_config;

struct fusedwasi_p1_state {
    char *args;       /* the arguments, each ended by a NUL */
    size_t args_size;
    uint32_t argc;
    char *env;        /* "NAME=value", each ended by a NUL */
    size_t env_size;
    uint32_t envc;
    fusedwasi_p1_hooks hooks;
    void *user;           /* the host's own state */
    int finished;         /* the program is over: 0, or how it ended (FUSEDWASI_P1_END_*) */
    uint32_t end_value;   /* ... the code of proc_exit, or the signal of proc_raise */
    uint8_t **memory_data;  /* where the base of the memory is kept (it moves when the memory grows) */
    uint64_t *memory_size;  /* ... and its size in bytes: the engine owns both, the core only reads them */
    fusedwasi_p1_entry *entries;  /* the descriptors, by number */
    uint32_t nentries;
};

/* the memory of the instance as the core reads it: the engine keeps the base and the size, and may move the first when the memory grows */
#define FUSEDWASI_P1_MEM(w) (*(w)->memory_data)

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
    if (e->owns_fd) rc = FUSEDWASI_P1_CLOSE(e->osfd);
    FUSEDWASI_P1_FREE(e->root);
    FUSEDWASI_P1_FREE(e->rel);
    FUSEDWASI_P1_FREE(e->preopen);
    fusedwasi_p1_dirlist_free(e->list);
    memset(e, 0, sizeof *e);
    return rc;
}

/* the directory of a preopen, from its real path: an open descriptor, or (on Windows) the path */
static inline int fusedwasi_p1_fill_preopen(fusedwasi_p1_entry *e, const char *root) {
#if FUSEDWASI_P1_DIRS_ARE_PATHS
#if defined(_WIN32)
    { /* the directory must be there: on POSIX it is opened here, and a path that is not a directory fails at once */
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1, NULL, 0);
        wchar_t *wide = n > 0 ? (wchar_t *)FUSEDWASI_P1_MALLOC((size_t)n * sizeof(wchar_t)) : NULL;
        if (wide == NULL) {
            errno = n > 0 ? ENOMEM : EINVAL;
            return -1;
        }
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1, wide, n);
        DWORD attributes = GetFileAttributesW(wide);
        FUSEDWASI_P1_FREE(wide);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            errno = ENOENT;
            return -1;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            errno = ENOTDIR;
            return -1;
        }
    }
#endif
    e->root = fusedwasi_p1_copy(root, strlen(root));
    e->rel = fusedwasi_p1_copy("", 0);
    if (e->root == NULL || e->rel == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
#else
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    e->osfd = fd;
    e->owns_fd = 1;
    return 0;
#endif
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

/* The state from a configuration (which it copies: the host may free it at once). NULL on failure with *err an errno; when it is
   the directory of a preopen that could not be opened, *bad is its number in the configuration, otherwise (size_t)-1. */
static inline fusedwasi_p1_state *fusedwasi_p1_create(const fusedwasi_p1_config *cfg, int *err, size_t *bad) {
    *bad = (size_t)-1;
    fusedwasi_p1_state *w = (fusedwasi_p1_state *)FUSEDWASI_P1_CALLOC(1, sizeof(fusedwasi_p1_state));
    if (w == NULL) {
        *err = ENOMEM;
        return NULL;
    }
    w->hooks = cfg->hooks;
    w->user = cfg->user;
    w->nentries = 8;
    w->entries = (fusedwasi_p1_entry *)FUSEDWASI_P1_CALLOC(w->nentries, sizeof(fusedwasi_p1_entry));
    if (w->entries == NULL) goto nomem;
    for (int i = 0; i < 3; i++) {
        w->entries[i].kind = FUSEDWASI_P1_K_STD;
        w->entries[i].osfd = i;  /* which stream it is, wherever it is renumbered to */
    }
    if (cfg->args != NULL) {
        w->args = fusedwasi_p1_copy(cfg->args, cfg->args_size);
        if (w->args == NULL) goto nomem;
        w->args_size = cfg->args_size;
        w->argc = cfg->argc;
    } else {
        size_t len = strlen(cfg->program);  /* no args given: the program's own name */
        w->args = fusedwasi_p1_copy(cfg->program, len);
        if (w->args == NULL) goto nomem;
        w->args_size = len + 1;
        w->argc = 1;
    }
    if (cfg->env != NULL && cfg->env_size > 0) {
        w->env = fusedwasi_p1_copy(cfg->env, cfg->env_size);
        if (w->env == NULL) goto nomem;
        w->env_size = cfg->env_size;
        w->envc = cfg->envc;
    }
    for (size_t i = 0; i < cfg->npreopens; i++) {
        const fusedwasi_p1_preopen *p = &cfg->preopens[i];
        int64_t number = fusedwasi_p1_new_fd(w);
        if (number < 0) goto nomem;
        fusedwasi_p1_entry *e = &w->entries[number];
        memset(e, 0, sizeof *e);
        e->kind = FUSEDWASI_P1_K_DIR;
        e->filetype = FUSEDWASI_P1_FT_DIR;
        e->rights = e->inheriting = p->readonly ? (FUSEDWASI_P1_R_ALL & ~FUSEDWASI_P1_R_MUTATING) : FUSEDWASI_P1_R_ALL;
        e->preopen = fusedwasi_p1_copy(p->name, p->name_len);
        e->preopen_len = p->name_len;
        if (e->preopen == NULL) {
            fusedwasi_p1_release(e);
            goto nomem;
        }
        if (fusedwasi_p1_fill_preopen(e, p->host) < 0) {
            *err = errno;
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
    FUSEDWASI_P1_FREE(w->args);
    FUSEDWASI_P1_FREE(w->env);
    FUSEDWASI_P1_FREE(w);
}

static inline void fusedwasi_p1_bind(fusedwasi_p1_state *w, uint8_t **data, uint64_t *size) {
    w->memory_data = data;
    w->memory_size = size;
}

/* The program is over (proc_exit, proc_raise, or a trap of a command): as on an operating system, what it opened is closed at once, and
   the instance says so to any call afterwards. What the caller gave is not closed: the streams, and the directories of the host. */
static inline void fusedwasi_p1_finish(fusedwasi_p1_state *w, int how, uint32_t value) {
    if (w == NULL || w->finished) return;
    w->finished = how;
    w->end_value = value;
    for (uint32_t i = 0; i < w->nentries; i++) fusedwasi_p1_release(&w->entries[i]);
}

/* the status of a hook as the result of a call: a trap is not an errno */
static inline uint32_t fusedwasi_p1_hook_status(int rc, int *trapped) {
    if (rc == FUSEDWASI_P1_HOOK_TRAP) {
        *trapped = 1;
        return 0;
    }
    return (uint32_t)rc;
}

/* ---- the memory of the instance (little-endian, as WebAssembly says) -------------------------------------------------- */

static inline int fusedwasi_p1_in_memory(const fusedwasi_p1_state *w, uint32_t ptr, uint64_t len) {
    return w->memory_data != NULL && (uint64_t)ptr + len <= *w->memory_size;
}

static inline int fusedwasi_p1_put_u8(fusedwasi_p1_state *w, uint32_t ptr, uint8_t v) {
    if (!fusedwasi_p1_in_memory(w, ptr, 1)) return 0;
    FUSEDWASI_P1_MEM(w)[ptr] = v;
    return 1;
}

static inline int fusedwasi_p1_put_u16(fusedwasi_p1_state *w, uint32_t ptr, uint16_t v) {
    if (!fusedwasi_p1_in_memory(w, ptr, 2)) return 0;
    FUSEDWASI_P1_MEM(w)[ptr] = (uint8_t)v;
    FUSEDWASI_P1_MEM(w)[ptr + 1] = (uint8_t)(v >> 8);
    return 1;
}

static inline int fusedwasi_p1_put_u32(fusedwasi_p1_state *w, uint32_t ptr, uint32_t v) {
    if (!fusedwasi_p1_in_memory(w, ptr, 4)) return 0;
    for (int i = 0; i < 4; i++) FUSEDWASI_P1_MEM(w)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1_put_u64(fusedwasi_p1_state *w, uint32_t ptr, uint64_t v) {
    if (!fusedwasi_p1_in_memory(w, ptr, 8)) return 0;
    for (int i = 0; i < 8; i++) FUSEDWASI_P1_MEM(w)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1_get_u32(const fusedwasi_p1_state *w, uint32_t ptr, uint32_t *v) {
    if (!fusedwasi_p1_in_memory(w, ptr, 4)) return 0;
    *v = 0;
    for (int i = 0; i < 4; i++) *v |= (uint32_t)FUSEDWASI_P1_MEM(w)[ptr + i] << (8 * i);
    return 1;
}

#define FUSEDWASI_P1_STATE(s) fusedwasi_p1_state *w = (fusedwasi_p1_state *)(s)

/* ---- arguments and environment --------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_strings_sizes(fusedwasi_p1_state *w, uint32_t count, size_t size, uint32_t count_ptr, uint32_t size_ptr) {
    if (!fusedwasi_p1_put_u32(w, count_ptr, count) || !fusedwasi_p1_put_u32(w, size_ptr, (uint32_t)size)) return FUSEDWASI_P1_E_FAULT;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_strings_get(fusedwasi_p1_state *w, const char *buf, size_t size, uint32_t count, uint32_t table, uint32_t out) {
    if (!fusedwasi_p1_in_memory(w, out, size) || !fusedwasi_p1_in_memory(w, table, (uint64_t)count * 4)) return FUSEDWASI_P1_E_FAULT;
    memcpy(FUSEDWASI_P1_MEM(w) + out, buf, size);
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        fusedwasi_p1_put_u32(w, table + 4 * i, out + (uint32_t)offset);
        offset += strlen(buf + offset) + 1;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_args_sizes_get(void *s, uint32_t argc, uint32_t size, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_strings_sizes(w, w->argc, w->args_size, argc, size);
}

static inline uint32_t fusedwasi_p1_args_get(void *s, uint32_t argv, uint32_t buf, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_strings_get(w, w->args, w->args_size, w->argc, argv, buf);
}

static inline uint32_t fusedwasi_p1_environ_sizes_get(void *s, uint32_t count, uint32_t size, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_strings_sizes(w, w->envc, w->env_size, count, size);
}

static inline uint32_t fusedwasi_p1_environ_get(void *s, uint32_t environ_, uint32_t buf, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_strings_get(w, w->env, w->env_size, w->envc, environ_, buf);
}

/* ---- clocks and random bytes (plain C: the system's own; a function given by the caller takes the place of either) ---------- */

/* the clock `id` of WASI (0 real time, 1 monotonic, 2 of the process, 3 of the thread) in nanoseconds; 0 on success */
#ifdef _WIN32
static inline uint64_t fusedwasi_p1_filetime_ns(FILETIME t) {
    return ((((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime)) * 100u;
}

static inline int fusedwasi_p1_clock_system(uint32_t id, uint64_t *ns) {
    FILETIME created, exited, kernel, user, now;
    LARGE_INTEGER counter, frequency;
    switch (id) {
    case 0:
        GetSystemTimePreciseAsFileTime(&now);
        *ns = fusedwasi_p1_filetime_ns(now) - 11644473600ull * 1000000000ull;  /* from 1601 to 1970 */
        return 0;
    case 1:
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) return -1;
        *ns = (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000ull +
              (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000000ull / (uint64_t)frequency.QuadPart;
        return 0;
    case 2:
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
        *ns = fusedwasi_p1_filetime_ns(kernel) + fusedwasi_p1_filetime_ns(user);
        return 0;
    case 3:
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
        *ns = fusedwasi_p1_filetime_ns(kernel) + fusedwasi_p1_filetime_ns(user);
        return 0;
    default: return -1;
    }
}

static inline int fusedwasi_p1_clock_resolution(uint32_t id, uint64_t *ns) {
    LARGE_INTEGER frequency;
    if (id == 1) {
        if (!QueryPerformanceFrequency(&frequency)) return -1;
        *ns = (1000000000ull + (uint64_t)frequency.QuadPart - 1) / (uint64_t)frequency.QuadPart;
        return 0;
    }
    *ns = 100;  /* the unit of a FILETIME */
    return id > 3 ? -1 : 0;
}
#else
static inline clockid_t fusedwasi_p1_clock_id(uint32_t id) {
    switch (id) {
    case 0: return CLOCK_REALTIME;
    case 1: return CLOCK_MONOTONIC;
    case 2: return CLOCK_PROCESS_CPUTIME_ID;
    default: return CLOCK_THREAD_CPUTIME_ID;
    }
}

static inline int fusedwasi_p1_clock_system(uint32_t id, uint64_t *ns) {
    struct timespec t;
    if (id > 3 || clock_gettime(fusedwasi_p1_clock_id(id), &t) != 0) return -1;
    *ns = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
    return 0;
}

static inline int fusedwasi_p1_clock_resolution(uint32_t id, uint64_t *ns) {
    struct timespec t;
    if (id > 3 || clock_getres(fusedwasi_p1_clock_id(id), &t) != 0) return -1;
    *ns = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
    if (*ns == 0) *ns = 1;
    return 0;
}
#endif

/* the time of a clock: the hook of the host or the system's */
static inline uint32_t fusedwasi_p1_clock_ns(fusedwasi_p1_state *w, uint32_t id, uint64_t *ns, int *trapped) {
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    if (w->hooks.clock != NULL) return fusedwasi_p1_hook_status(w->hooks.clock(w->user, id, ns), trapped);
    return fusedwasi_p1_clock_system(id, ns) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_NOTSUP;
}

static inline uint32_t fusedwasi_p1_clock_res_get(void *s, uint32_t id, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    uint64_t ns;
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    if (fusedwasi_p1_clock_resolution(id, &ns) != 0) return FUSEDWASI_P1_E_NOTSUP;
    return fusedwasi_p1_put_u64(w, ptr, ns) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_clock_time_get(void *s, uint32_t id, uint64_t precision, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)precision;
    uint64_t ns = 0;
    uint32_t rc = fusedwasi_p1_clock_ns(w, id, &ns, trapped);
    if (rc != FUSEDWASI_P1_E_SUCCESS) return rc;
    return fusedwasi_p1_put_u64(w, ptr, ns) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* `size` random bytes from the system; 0 on success */
static inline int fusedwasi_p1_random_system(unsigned char *buf, size_t size) {
#if defined(_WIN32)
    return BCryptGenRandom(NULL, buf, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#elif defined(__APPLE__)
    arc4random_buf(buf, size);
    return 0;
#else
#ifdef FUSEDWASI_P1_HAVE_GETRANDOM
    {  /* the system call needs no file: /dev/urandom fails in a chroot, without /dev, or when the process has no descriptors left */
        size_t got = 0;
        while (got < size) {
            ssize_t n = getrandom(buf + got, size - got, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (got == size) return 0;
    }
#endif
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, buf + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    close(fd);
    return 0;
#endif
}

static inline uint32_t fusedwasi_p1_random_get(void *s, uint32_t buf, uint32_t size, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    if (!fusedwasi_p1_in_memory(w, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (size == 0) return FUSEDWASI_P1_E_SUCCESS;
    if (w->hooks.random != NULL) return fusedwasi_p1_hook_status(w->hooks.random(w->user, FUSEDWASI_P1_MEM(w) + buf, size), trapped);
    return fusedwasi_p1_random_system(FUSEDWASI_P1_MEM(w) + buf, size) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_IO;
}

/* waits `ns` nanoseconds on the monotonic clock: the hook of the host (the interpreter released, a signal seen) or, without one, the
   system's sleep in slices of 100 ms; 0, or an errno / FUSEDWASI_P1_HOOK_TRAP */
static inline int fusedwasi_p1_sleep_ns(fusedwasi_p1_state *w, uint64_t ns) {
    if (w->hooks.sleep != NULL) return w->hooks.sleep(w->user, ns);
    uint64_t start;
    if (fusedwasi_p1_clock_system(1, &start) != 0) return FUSEDWASI_P1_E_NOTSUP;
    for (;;) {
        uint64_t now;
        if (fusedwasi_p1_clock_system(1, &now) != 0) return 0;
        uint64_t spent = now - start;
        if (spent >= ns) return 0;
        uint64_t left = ns - spent, slice = left < 100000000ull ? left : 100000000ull;
#ifdef _WIN32
        Sleep((DWORD)((slice + 999999u) / 1000000u));
#else
        {
            struct timespec t;
            t.tv_sec = (time_t)(slice / 1000000000ull);
            t.tv_nsec = (long)(slice % 1000000000ull);
            nanosleep(&t, NULL);
        }
#endif
    }
}

static inline uint32_t fusedwasi_p1_sched_yield(void *s, int *trapped) {
    (void)s;
    (void)trapped;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline void fusedwasi_p1_proc_exit(void *s, uint32_t code, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    if (w->hooks.end != NULL) w->hooks.end(w->user, FUSEDWASI_P1_END_EXIT, code);
    fusedwasi_p1_finish(w, FUSEDWASI_P1_END_EXIT, code);
    *trapped = 1;
}

/* proc_raise: the signals of WASI 0.1 by number. A signal that is ignored when nothing handles it (the child, continue, urgent data, the
   size of the window) or that stops a process (stop, and the ones of a terminal) is answered with success and the program goes on;
   any other ends it, as it would end a process: the host is told (Python: WasiSignal, with the number and the name). 0 is no signal. */
static inline uint32_t fusedwasi_p1_proc_raise(void *s, uint32_t sig, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    if (sig > 30) return FUSEDWASI_P1_E_INVAL;
    if (sig == 0 || (sig >= 16 && sig <= 22) || sig == 27) return FUSEDWASI_P1_E_SUCCESS;  /* nothing ends */
    if (w->hooks.end != NULL) w->hooks.end(w->user, FUSEDWASI_P1_END_SIGNAL, sig);
    fusedwasi_p1_finish(w, FUSEDWASI_P1_END_SIGNAL, sig);
    *trapped = 1;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- descriptors ----------------------------------------------------------------------------------------------------- */

static inline fusedwasi_p1_entry *fusedwasi_p1_entry_of(fusedwasi_p1_state *w, uint32_t fd) {
    return fd < w->nentries && w->entries[fd].kind != FUSEDWASI_P1_K_NONE ? &w->entries[fd] : NULL;
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
static inline uint32_t fusedwasi_p1_iovs_valid(fusedwasi_p1_state *w, uint32_t iovs, uint32_t count) {
    if (!fusedwasi_p1_in_memory(w, iovs, (uint64_t)count * 8)) return FUSEDWASI_P1_E_FAULT;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_write_std(fusedwasi_p1_state *w, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nwritten, int *trapped) {
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(w, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        if (w->hooks.stdout_write != NULL) {
            int rc = w->hooks.stdout_write(w->user, (int)fd, FUSEDWASI_P1_MEM(w) + base, len);
            if (rc) {  /* what was written is reported (a short write); the error comes with the next call. A trap is a trap. */
                uint32_t status = fusedwasi_p1_hook_status(rc, trapped);
                if (*trapped || total == 0) return status;
                break;
            }
        } else {
            long long done = 0;
            int failed = 0;
            while (done < (long long)len) {  /* all of it: a short write of a pipe is not the end of the iovec */
                long long n = FUSEDWASI_P1_WRITE((int)fd, FUSEDWASI_P1_MEM(w) + base + done, len - done);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    if (total + (uint64_t)done == 0) return fusedwasi_p1_errno(errno);
                    failed = 1;
                    break;
                }
                done += n;
            }
            if (failed) {
                total += (uint64_t)done;
                break;
            }
        }
        total += len;
    }
    return fusedwasi_p1_put_u32(w, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_read_std(fusedwasi_p1_state *w, uint32_t iovs, uint32_t count, uint32_t nread, int *trapped) {
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(w, iovs, count);
        if (bad) return bad;
    }
    uint32_t base = 0, len = 0;
    for (uint32_t i = 0; i < count; i++) {  /* the first buffer that is not empty: a short read is allowed */
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (len) break;
    }
    if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
    size_t got = 0;
    if (len) {
        if (w->hooks.stdin_read != NULL) {
            int rc = w->hooks.stdin_read(w->user, FUSEDWASI_P1_MEM(w) + base, len, &got);
            if (rc) return fusedwasi_p1_hook_status(rc, trapped);
            if (got > len) return FUSEDWASI_P1_E_IO;
        } else {
            long long n;
            do n = FUSEDWASI_P1_READ(0, FUSEDWASI_P1_MEM(w) + base, len);
            while (n < 0 && errno == EINTR);
            if (n < 0) return fusedwasi_p1_errno(errno);
            got = (size_t)n;
        }
    }
    return fusedwasi_p1_put_u32(w, nread, (uint32_t)got) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* ---- reading, seeking, closing ---------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_fd_write(void *s, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nwritten, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL || e->kind == FUSEDWASI_P1_K_DIR) return e == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_ISDIR;
    if (e->kind == FUSEDWASI_P1_K_STD) return e->osfd == 0 ? FUSEDWASI_P1_E_BADF : fusedwasi_p1_write_std(w, (uint32_t)e->osfd, iovs, count, nwritten, trapped);
    if (!(e->rights & FUSEDWASI_P1_R_FD_WRITE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(w, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
#ifdef _WIN32
        if (e->flags & FUSEDWASI_P1_FD_APPEND) FUSEDWASI_P1_LSEEK(e->osfd, 0, SEEK_END);
#endif
        long long n = FUSEDWASI_P1_WRITE(e->osfd, FUSEDWASI_P1_MEM(w) + base, len);  /* straight from the memory of the module */
        if (n < 0) {
            if (total) break;
            return fusedwasi_p1_errno(errno);
        }
        total += (uint64_t)n;
        if ((uint32_t)n < len) break;
    }
    return fusedwasi_p1_put_u32(w, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_fd_read(void *s, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nread, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return e->osfd == 0 ? fusedwasi_p1_read_std(w, iovs, count, nread, trapped) : FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READ)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(w, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        long long n = FUSEDWASI_P1_READ(e->osfd, FUSEDWASI_P1_MEM(w) + base, len);  /* no interpreter: this is the part that moves the data */
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
            if (fstat(e->osfd, &st) == 0 && !S_ISREG(st.st_mode)) break;
        }
#endif
    }
    return fusedwasi_p1_put_u32(w, nread, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_seek_native(fusedwasi_p1_state *w, uint32_t fd, int64_t offset, int native, uint32_t ptr, uint64_t right) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    long long pos = FUSEDWASI_P1_LSEEK(e->osfd, offset, native);
    if (pos < 0) return fusedwasi_p1_errno(errno);
    return fusedwasi_p1_put_u64(w, ptr, (uint64_t)pos) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* the order of whence in this snapshot: set, cur, end */
static inline uint32_t fusedwasi_p1_fd_seek(void *s, uint32_t fd, uint64_t offset, uint32_t whence, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    static const int native[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
    if (whence > 2) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1_seek_native(w, fd, (int64_t)offset, native[whence], ptr, FUSEDWASI_P1_R_FD_SEEK);
}

static inline uint32_t fusedwasi_p1_fd_tell(void *s, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_seek_native(w, fd, 0, SEEK_CUR, ptr, FUSEDWASI_P1_R_FD_TELL);
}

static inline uint32_t fusedwasi_p1_fd_close(void *s, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF */
        memset(e, 0, sizeof *e);
        return FUSEDWASI_P1_E_SUCCESS;
    }
    return fusedwasi_p1_release(e) == 0 ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_errno(errno);
}

static inline uint32_t fusedwasi_p1_fd_renumber(void *s, uint32_t from, uint32_t to, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *a = fusedwasi_p1_entry_of(w, from), *b = fusedwasi_p1_entry_of(w, to);
    if (a == NULL || b == NULL) return FUSEDWASI_P1_E_BADF;
    if (from == to) return FUSEDWASI_P1_E_SUCCESS;
    fusedwasi_p1_release(b);
    *b = *a;
    memset(a, 0, sizeof *a);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_advise(void *s, uint32_t fd, uint64_t offset, uint64_t length, uint32_t advice, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)offset;
    (void)length;
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL || e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & FUSEDWASI_P1_R_FD_ADVISE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (advice > 5) return FUSEDWASI_P1_E_INVAL;  /* the advice is one of six */
    return FUSEDWASI_P1_E_SUCCESS;  /* a hint: nothing is done with it */
}

static inline int fusedwasi_p1_std_is_real(fusedwasi_p1_state *w, fusedwasi_p1_entry *e);
static inline uint8_t fusedwasi_p1_std_filetype(fusedwasi_p1_state *w, fusedwasi_p1_entry *e);

static inline uint32_t fusedwasi_p1_fd_fdstat_get(void *s, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    uint8_t filetype = e->kind == FUSEDWASI_P1_K_STD ? fusedwasi_p1_std_filetype(w, e) : e->filetype;
    uint64_t base = e->kind == FUSEDWASI_P1_K_STD ? (e->osfd == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT) : e->rights;
    if (!fusedwasi_p1_in_memory(w, ptr, 24)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(w) + ptr, 0, 24);
    fusedwasi_p1_put_u8(w, ptr, filetype);
    fusedwasi_p1_put_u16(w, ptr + 2, e->flags);
    fusedwasi_p1_put_u64(w, ptr + 8, base);
    fusedwasi_p1_put_u64(w, ptr + 16, e->inheriting);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_prestat_get(void *s, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;  /* the end of the list the program walks */
    if (!fusedwasi_p1_in_memory(w, ptr, 8)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(w) + ptr, 0, 8);  /* the type of a preopen: a directory, 0 */
    fusedwasi_p1_put_u32(w, ptr + 4, (uint32_t)e->preopen_len);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_prestat_dir_name(void *s, uint32_t fd, uint32_t path, uint32_t size, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;
    if (size < e->preopen_len) return FUSEDWASI_P1_E_NAMETOOLONG;
    if (!fusedwasi_p1_in_memory(w, path, e->preopen_len)) return FUSEDWASI_P1_E_FAULT;
    memcpy(FUSEDWASI_P1_MEM(w) + path, e->preopen, e->preopen_len);
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
static inline int fusedwasi_p1_std_is_real(fusedwasi_p1_state *w, fusedwasi_p1_entry *e) {
#if FUSEDWASI_P1_NATIVE_POLL
    return e->kind == FUSEDWASI_P1_K_STD && (e->osfd == 0 ? w->hooks.stdin_read == NULL : w->hooks.stdout_write == NULL);
#else
    (void)w, (void)e;
    return 0;
#endif
}

static inline uint8_t fusedwasi_p1_std_filetype(fusedwasi_p1_state *w, fusedwasi_p1_entry *e) {
#if FUSEDWASI_P1_NATIVE_POLL
    struct stat st;
    if (fusedwasi_p1_std_is_real(w, e) && fstat(e->osfd, &st) == 0) return fusedwasi_p1_filetype_of((unsigned long)st.st_mode);  /* a pipe is unknown: not a terminal */
#endif
    (void)w, (void)e;
    return FUSEDWASI_P1_FT_CHAR;
}

/* what the specification calls filestat, the same for both ways of getting it */
typedef struct {
    uint64_t dev, ino, nlink, size, atim, mtim, ctim;
    uint8_t filetype;
} fusedwasi_p1_stat;

/* the filestat of the first snapshot has a 32-bit link count and 56 bytes */
static inline uint32_t fusedwasi_p1_put_filestat(fusedwasi_p1_state *w, uint32_t ptr, const fusedwasi_p1_stat *st, int legacy) {
    size_t total = legacy ? 56 : 64;
    if (!fusedwasi_p1_in_memory(w, ptr, total)) return FUSEDWASI_P1_E_FAULT;
    memset(FUSEDWASI_P1_MEM(w) + ptr, 0, total);
    fusedwasi_p1_put_u64(w, ptr, st->dev);
    fusedwasi_p1_put_u64(w, ptr + 8, st->ino);
    fusedwasi_p1_put_u8(w, ptr + 16, st->filetype);
    if (legacy) {
        fusedwasi_p1_put_u32(w, ptr + 20, (uint32_t)st->nlink);
        fusedwasi_p1_put_u64(w, ptr + 24, st->size);
        fusedwasi_p1_put_u64(w, ptr + 32, st->atim);
        fusedwasi_p1_put_u64(w, ptr + 40, st->mtim);
        fusedwasi_p1_put_u64(w, ptr + 48, st->ctim);
    } else {
        fusedwasi_p1_put_u64(w, ptr + 24, st->nlink);
        fusedwasi_p1_put_u64(w, ptr + 32, st->size);
        fusedwasi_p1_put_u64(w, ptr + 40, st->atim);
        fusedwasi_p1_put_u64(w, ptr + 48, st->mtim);
        fusedwasi_p1_put_u64(w, ptr + 56, st->ctim);
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

/* the flags of open(2) that a path_open asks for */
static inline int fusedwasi_p1_open_flags(uint64_t base, uint32_t oflags, uint32_t fdflags) {
    /* writing is what the rights say: to write, to sync the data, to allocate, to set the size (uvwasi; wasi-libc leaves these out of a read-only open) */
    int writable = (base & (FUSEDWASI_P1_R_FD_WRITE | FUSEDWASI_P1_R_FD_DATASYNC | FUSEDWASI_P1_R_FD_ALLOCATE | FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE)) != 0 || (oflags & FUSEDWASI_P1_O_TRUNC) != 0,
        readable = (base & FUSEDWASI_P1_R_FD_READ) != 0;
    int flags = writable ? (readable ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (oflags & FUSEDWASI_P1_O_CREAT) flags |= O_CREAT;
    if (oflags & FUSEDWASI_P1_O_EXCL) flags |= O_EXCL;
    if (oflags & FUSEDWASI_P1_O_TRUNC) flags |= O_TRUNC;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
    return flags | fusedwasi_p1_native_fdflags(fdflags);
}

#if defined(_WIN32)
#include "wasi_p1_files_win32.h"
#else
/* ---- the file system through the system: openat and its kin (POSIX) ----------------------------------------------------
   A directory is an open descriptor, a path is walked from it one name at a time, and a symbolic link met on the way is read
   and its target put in front of the rest of the path (never an absolute one): nothing can lead out of the preopen, and there is
   no gap between a check and a use. No interpreter is involved. */

/* renameat is declared by <stdio.h> only when the feature macros were set before it was first included, which a build on an old
   glibc (manylinux) does not always have: the same prototype again is harmless where it is declared */
int renameat(int, const char *, int, const char *);

#define FUSEDWASI_P1_PATH_MAX 4096
#define FUSEDWASI_P1_NAME_MAX 255
#define FUSEDWASI_P1_LEAF_MAX (FUSEDWASI_P1_NAME_MAX + 2)  /* a name, a slash that ended the path, and the NUL */
#define FUSEDWASI_P1_MAX_DEPTH 64

/* Walks `given` (len bytes) from the directory `base`. 0, with the directory that holds the last name in `*parent` (a new
   descriptor, to close, when `*owned`; else `base` itself) and that name in `leaf` ("." for a path that names a directory itself).
   When `follow` is set a last name that is a link is expanded too. Otherwise an errno of WASI. */
static inline uint32_t fusedwasi_p1_walk(int base, const unsigned char *given, uint32_t len, int follow, int *parent, int *owned,
                                 char leaf[FUSEDWASI_P1_LEAF_MAX]) {
    char work[FUSEDWASI_P1_PATH_MAX], target[FUSEDWASI_P1_PATH_MAX], name[FUSEDWASI_P1_NAME_MAX + 1];
    int fds[FUSEDWASI_P1_MAX_DEPTH + 1];
    int depth = 0, links = 0;
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    if (len == 0) return FUSEDWASI_P1_E_NOENT;
    if (len >= sizeof work) return FUSEDWASI_P1_E_NAMETOOLONG;
    if (memchr(given, 0, len) != NULL) return FUSEDWASI_P1_E_INVAL;
    if (given[0] == '/') return FUSEDWASI_P1_E_NOTCAPABLE;
    memcpy(work, given, len);
    work[len] = '\0';
    fds[0] = base;
    char *cur = work;
    for (;;) {
        while (*cur == '/') cur++;
        if (*cur == '\0') {  /* nothing left: the path named a directory (it ended with a slash) */
            strcpy(leaf, ".");
            break;
        }
        char *end = strchr(cur, '/');
        size_t clen = end ? (size_t)(end - cur) : strlen(cur);
        char *rest = cur + clen;
        while (*rest == '/') rest++;
        int last = (*rest == '\0');
        int slash = last && end != NULL;  /* the path ended with a slash: what it names has to be a directory */
        if (clen > FUSEDWASI_P1_NAME_MAX) {
            rc = FUSEDWASI_P1_E_NAMETOOLONG;
            goto done;
        }
        memcpy(name, cur, clen);
        name[clen] = '\0';
        if (clen == 1 && name[0] == '.') {
            if (last) {
                strcpy(leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        if (clen == 2 && name[0] == '.' && name[1] == '.') {
            if (depth == 0) {  /* out of the directory it was given */
                rc = FUSEDWASI_P1_E_NOTCAPABLE;
                goto done;
            }
            close(fds[depth--]);
            if (last) {
                strcpy(leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        int expand = 0;
        if (last) {
            if (!follow) {
                /* A slash after a name makes the kernel follow a link there whatever O_NOFOLLOW says, and the walk would hand over a way out of the
                   preopen: a link with a slash and no symlink_follow is not a directory (found by comparing with uvwasi). */
                char probe[1];
                if (slash && readlinkat(fds[depth], name, probe, sizeof probe) >= 0) {
                    rc = FUSEDWASI_P1_E_NOTDIR;
                    goto done;
                }
                strcpy(leaf, name);
                if (slash) strcat(leaf, "/");
                break;
            }
            expand = 1;  /* a link at the end is read before it is used */
        } else {
            if (depth >= FUSEDWASI_P1_MAX_DEPTH) {
                rc = FUSEDWASI_P1_E_NAMETOOLONG;
                goto done;
            }
            int next = openat(fds[depth], name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next >= 0) {
                fds[++depth] = next;
                cur = rest;
                continue;
            }
            if (errno != ELOOP && errno != ENOTDIR && errno != EMLINK) {
                rc = fusedwasi_p1_errno(errno);
                goto done;
            }
            expand = 1;  /* not a directory we can enter: perhaps a link */
        }
        if (expand) {
            ssize_t n = readlinkat(fds[depth], name, target, sizeof target - 1);
            if (n < 0) {
                if (last) {  /* not a link: this is the last name */
                    strcpy(leaf, name);
                if (slash) strcat(leaf, "/");
                    break;
                }
                rc = FUSEDWASI_P1_E_NOTDIR;  /* a name in the middle that is not a directory, nor a link */
                goto done;
            }
            target[n] = '\0';
            if (++links > 40) {
                rc = FUSEDWASI_P1_E_LOOP;
                goto done;
            }
            if (target[0] == '/') {
                rc = FUSEDWASI_P1_E_NOTCAPABLE;
                goto done;
            }
            size_t rest_len = strlen(rest);
            if ((size_t)n + 1 + rest_len >= sizeof work) {
                rc = FUSEDWASI_P1_E_NAMETOOLONG;
                goto done;
            }
            memmove(work + n + 1, rest, rest_len + 1);  /* rest may lie inside work */
            memcpy(work, target, (size_t)n);
            work[n] = '/';
            if (rest_len == 0 && !slash) work[n] = '\0';  /* a slash only where the path had one */
            cur = work;
        }
    }
    for (int i = 1; i < depth; i++) close(fds[i]);
    *parent = fds[depth];
    *owned = depth > 0;
    return FUSEDWASI_P1_E_SUCCESS;
done:
    for (int i = 1; i <= depth; i++) close(fds[i]);
    return rc;
}

/* a time of a file in nanoseconds since 1970 as WASI has it (unsigned): before 1970 is 0, and past the year 2262 it saturates (the product would overflow) */
static inline int64_t fusedwasi_p1_ns(const struct timespec *t) {
    if (t->tv_sec < 0) return 0;
    if ((int64_t)t->tv_sec > INT64_MAX / 1000000000LL - 1) return INT64_MAX;
    return (int64_t)t->tv_sec * 1000000000LL + t->tv_nsec;
}

static inline void fusedwasi_p1_stat_of_native(const struct stat *st, fusedwasi_p1_stat *out) {
    out->dev = (uint64_t)st->st_dev;
    out->ino = (uint64_t)st->st_ino;
    out->nlink = (uint64_t)st->st_nlink;
    out->size = (uint64_t)st->st_size;
#ifdef __APPLE__
    out->atim = (uint64_t)fusedwasi_p1_ns(&st->st_atimespec);
    out->mtim = (uint64_t)fusedwasi_p1_ns(&st->st_mtimespec);
    out->ctim = (uint64_t)fusedwasi_p1_ns(&st->st_ctimespec);
#else
    out->atim = (uint64_t)fusedwasi_p1_ns(&st->st_atim);
    out->mtim = (uint64_t)fusedwasi_p1_ns(&st->st_mtim);
    out->ctim = (uint64_t)fusedwasi_p1_ns(&st->st_ctim);
#endif
    out->filetype = fusedwasi_p1_filetype_of((unsigned long)st->st_mode);
}

static inline uint32_t fusedwasi_p1_be_stat_fd(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, fusedwasi_p1_stat *out, int *trapped) {
    (void)w;
    (void)trapped;
    struct stat st;
    if (fstat(e->osfd, &st) < 0) return fusedwasi_p1_errno(errno);
    fusedwasi_p1_stat_of_native(&st, out);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_stat_path(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow,
                                         fusedwasi_p1_stat *out, int *trapped) {
    (void)trapped;
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(w, path, len)) return FUSEDWASI_P1_E_FAULT;
    int parent, owned;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_walk(dir->osfd, FUSEDWASI_P1_MEM(w) + path, len, follow, &parent, &owned, leaf);
    if (rc) return rc;
    struct stat st;
    int failed = fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW);  /* a link at the end was expanded by the walk if it was to be */
    int saved = errno;
    if (owned) close(parent);
    if (failed < 0) return fusedwasi_p1_errno(saved);
    fusedwasi_p1_stat_of_native(&st, out);
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_open(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint32_t oflags,
                                    uint64_t base, uint32_t fdflags, fusedwasi_p1_entry *e, int *trapped) {
    (void)trapped;
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(w, path, len)) return FUSEDWASI_P1_E_FAULT;
    int parent, owned;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_walk(dir->osfd, FUSEDWASI_P1_MEM(w) + path, len, follow, &parent, &owned, leaf);
    if (rc) return rc;
    int flags = fusedwasi_p1_open_flags(base, oflags, fdflags) | O_NOFOLLOW | O_CLOEXEC;
    if (oflags & FUSEDWASI_P1_O_DIRECTORY) flags |= O_DIRECTORY;
    int fd = openat(parent, leaf, flags, 0666);
    if (fd < 0 && errno == EISDIR && (base & FUSEDWASI_P1_R_OF_DIRECTORY) && !(oflags & (FUSEDWASI_P1_O_CREAT | FUSEDWASI_P1_O_TRUNC))) {
        fd = openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY, 0);  /* it was a directory, and asked for as one */
    }
    int saved = errno;
    if (owned) close(parent);
    if (fd < 0) return fusedwasi_p1_errno(saved);
    struct stat st;
    if (fstat(fd, &st) < 0) {
        saved = errno;
        close(fd);
        return fusedwasi_p1_errno(saved);
    }
    e->kind = fusedwasi_p1_filetype_of((unsigned long)st.st_mode) == FUSEDWASI_P1_FT_DIR ? FUSEDWASI_P1_K_DIR : FUSEDWASI_P1_K_FILE;
    e->filetype = fusedwasi_p1_filetype_of((unsigned long)st.st_mode);
    e->osfd = fd;
    e->owns_fd = 1;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the directory that holds the last name of a path (a link at the end is expanded when `follow`), and that name; "." (the directory itself) is `itself` */
static inline uint32_t fusedwasi_p1_be_parent(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, int *parent,
                                      int *owned, char leaf[FUSEDWASI_P1_LEAF_MAX], int *itself) {
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(w, path, len)) return FUSEDWASI_P1_E_FAULT;
    uint32_t rc = fusedwasi_p1_walk(dir->osfd, FUSEDWASI_P1_MEM(w) + path, len, follow, parent, owned, leaf);
    *itself = rc == FUSEDWASI_P1_E_SUCCESS && strcmp(leaf, ".") == 0;
    return rc;
}

/* A name to be made that has a slash after it (a link): 1, with errno set, when it can only be an error; 0 when it has none */
static inline int fusedwasi_p1_leaf_slash(int parent, const char *leaf) {
    size_t n = strlen(leaf);
    if (n == 0 || leaf[n - 1] != '/') return 0;
    char bare[FUSEDWASI_P1_LEAF_MAX];
    memcpy(bare, leaf, n - 1);
    bare[n - 1] = '\0';
    struct stat st;
    if (fstatat(parent, bare, &st, AT_SYMLINK_NOFOLLOW) < 0) errno = errno == ENOENT ? ENOENT : errno;
    else errno = S_ISDIR(st.st_mode) ? EEXIST : ENOTDIR;
    return 1;
}

static inline uint32_t fusedwasi_p1_be_mkdir(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int *trapped) {
    (void)trapped;
    int parent, owned, itself;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_parent(w, dir, path, len, 0, &parent, &owned, leaf, &itself);
    if (rc) return rc;
    int failed = -1;
    if (itself) errno = EEXIST;
    else failed = mkdirat(parent, leaf, 0777);
    int saved = errno;
    if (owned) close(parent);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_remove(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int isdir, int *trapped) {
    (void)trapped;
    int parent, owned, itself;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_parent(w, dir, path, len, 0, &parent, &owned, leaf, &itself);
    if (rc) return rc;
    int failed = -1;
    if (itself) errno = EINVAL;
    else failed = unlinkat(parent, leaf, isdir ? AT_REMOVEDIR : 0);
    int saved = errno;
    /* unlink of a directory: EISDIR on Linux, EPERM on macOS; the system's own answer is what is given */
    if (owned) close(parent);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

static inline void fusedwasi_p1_timespecs(uint64_t atim, uint64_t mtim, uint32_t flags, struct timespec ts[2]) {
    for (int i = 0; i < 2; i++) {
        uint32_t set = i == 0 ? FUSEDWASI_P1_T_ATIM : FUSEDWASI_P1_T_MTIM, now = i == 0 ? FUSEDWASI_P1_T_ATIM_NOW : FUSEDWASI_P1_T_MTIM_NOW;
        uint64_t value = i == 0 ? atim : mtim;
        memset(&ts[i], 0, sizeof ts[i]);
        if (flags & set) {
            ts[i].tv_sec = (time_t)(value / 1000000000u);
            ts[i].tv_nsec = (long)(value % 1000000000u);
        } else {
            ts[i].tv_nsec = (flags & now) ? UTIME_NOW : UTIME_OMIT;
        }
    }
}

static inline uint32_t fusedwasi_p1_be_times_fd(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, uint64_t atim, uint64_t mtim, uint32_t flags, int *trapped) {
    (void)w;
    (void)trapped;
    struct timespec ts[2];
    fusedwasi_p1_timespecs(atim, mtim, flags, ts);
    return futimens(e->osfd, ts) < 0 ? fusedwasi_p1_errno(errno) : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_times_path(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint64_t atim,
                                          uint64_t mtim, uint32_t flags, int *trapped) {
    (void)trapped;
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(w, path, len)) return FUSEDWASI_P1_E_FAULT;
    int parent, owned;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_walk(dir->osfd, FUSEDWASI_P1_MEM(w) + path, len, follow, &parent, &owned, leaf);
    if (rc) return rc;
    struct timespec ts[2];
    fusedwasi_p1_timespecs(atim, mtim, flags, ts);
    int failed = utimensat(parent, leaf, ts, AT_SYMLINK_NOFOLLOW);  /* a link at the end was expanded by the walk if it was to be */
    int saved = errno;
    if (owned) close(parent);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

/* the two directories and names of rename and link; both descriptors have to be closed by the caller (`owned`) */
static inline uint32_t fusedwasi_p1_be_pair(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow, fusedwasi_p1_entry *d2,
                                    uint32_t p2, uint32_t l2, int *parent1, int *owned1, char leaf1[FUSEDWASI_P1_LEAF_MAX],
                                    int *parent2, int *owned2, char leaf2[FUSEDWASI_P1_LEAF_MAX]) {
    int itself1, itself2;
    uint32_t rc = fusedwasi_p1_be_parent(w, d1, p1, l1, follow, parent1, owned1, leaf1, &itself1);
    if (rc) return rc;
    rc = fusedwasi_p1_be_parent(w, d2, p2, l2, 0, parent2, owned2, leaf2, &itself2);
    if (rc) {
        if (*owned1) close(*parent1);
        return rc;
    }
    if (itself1 || itself2) {  /* a directory itself is neither moved nor made a link */
        if (*owned1) close(*parent1);
        if (*owned2) close(*parent2);
        return itself2 ? FUSEDWASI_P1_E_EXIST : FUSEDWASI_P1_E_INVAL;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_rename(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, fusedwasi_p1_entry *d2, uint32_t p2,
                                      uint32_t l2, int *trapped) {
    (void)trapped;
    int parent1, owned1, parent2, owned2;
    char leaf1[FUSEDWASI_P1_LEAF_MAX], leaf2[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_pair(w, d1, p1, l1, 0, d2, p2, l2, &parent1, &owned1, leaf1, &parent2, &owned2, leaf2);
    if (rc) return rc;
    int failed = renameat(parent1, leaf1, parent2, leaf2);
    int saved = errno;
    if (owned1) close(parent1);
    if (owned2) close(parent2);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_link(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow, fusedwasi_p1_entry *d2,
                                    uint32_t p2, uint32_t l2, int *trapped) {
    (void)trapped;
    int parent1, owned1, parent2, owned2;
    char leaf1[FUSEDWASI_P1_LEAF_MAX], leaf2[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_pair(w, d1, p1, l1, follow, d2, p2, l2, &parent1, &owned1, leaf1, &parent2, &owned2, leaf2);
    if (rc) return rc;
    int failed = fusedwasi_p1_leaf_slash(parent2, leaf2) ? -1 : linkat(parent1, leaf1, parent2, leaf2, 0);  /* (the walk expanded a link at the end if it was to be followed) */
    int saved = errno;
    if (owned1) close(parent1);
    if (owned2) close(parent2);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_symlink(fusedwasi_p1_state *w, const unsigned char *target, uint32_t tlen, fusedwasi_p1_entry *dir, uint32_t path,
                                       uint32_t len, int *trapped) {
    (void)trapped;
    char text[FUSEDWASI_P1_PATH_MAX];
    if (tlen >= sizeof text) return FUSEDWASI_P1_E_NAMETOOLONG;
    memcpy(text, target, tlen);
    text[tlen] = '\0';
    int parent, owned, itself;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_parent(w, dir, path, len, 0, &parent, &owned, leaf, &itself);
    if (rc) return rc;
    int failed = -1;
    if (itself) errno = EEXIST;
    else if (fusedwasi_p1_leaf_slash(parent, leaf)) failed = -1;  /* errno is set */
    else failed = symlinkat(text, parent, leaf);
    int saved = errno;
    if (owned) close(parent);
    return failed < 0 ? fusedwasi_p1_errno(saved) : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_readlink(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, char *out, size_t cap,
                                        size_t *used, int *trapped) {
    (void)trapped;
    int parent, owned, itself;
    char leaf[FUSEDWASI_P1_LEAF_MAX];
    uint32_t rc = fusedwasi_p1_be_parent(w, dir, path, len, 0, &parent, &owned, leaf, &itself);
    if (rc) return rc;
    ssize_t n = -1;
    if (itself) errno = EINVAL;
    else n = readlinkat(parent, leaf, out, cap ? cap : 1);  /* a buffer of 0 bytes is the system's EINVAL; asked with 1 it says whether this is a link */
    int saved = errno;
    if (owned) close(parent);
    if (n < 0) return fusedwasi_p1_errno(saved);
    *used = cap ? (size_t)n : 0;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_list(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, fusedwasi_p1_dirlist *list, int *trapped) {
    (void)w;
    (void)trapped;
    int copy = dup(e->osfd);  /* closedir closes its descriptor */
    if (copy < 0) return fusedwasi_p1_errno(errno);
    DIR *d = fdopendir(copy);
    if (d == NULL) {
        int saved = errno;
        close(copy);
        return fusedwasi_p1_errno(saved);
    }
    rewinddir(d);
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    struct dirent *entry;
    while (rc == FUSEDWASI_P1_E_SUCCESS && (entry = readdir(d)) != NULL) {
        uint8_t type = FUSEDWASI_P1_FT_UNKNOWN;
        switch (entry->d_type) {
        case DT_DIR: type = FUSEDWASI_P1_FT_DIR; break;
        case DT_REG: type = FUSEDWASI_P1_FT_REGULAR; break;
        case DT_LNK: type = FUSEDWASI_P1_FT_SYMLINK; break;
        case DT_CHR: type = FUSEDWASI_P1_FT_CHAR; break;
        case DT_BLK: type = FUSEDWASI_P1_FT_BLOCK; break;
        case DT_SOCK: type = FUSEDWASI_P1_FT_SOCKET_STREAM; break;
        default: {  /* the file system does not say: ask */
            struct stat st;
            if (fstatat(e->osfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) type = fusedwasi_p1_filetype_of((unsigned long)st.st_mode);
        }
        }
        if (fusedwasi_p1_dirlist_add(list, entry->d_name, (uint64_t)entry->d_ino, type) < 0) rc = FUSEDWASI_P1_E_NOMEM;
    }
    closedir(d);
    return rc;
}
#endif

/* ---- the calls that use them ------------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_filestat_of_fd(fusedwasi_p1_state *w, uint32_t fd, uint32_t ptr, int legacy, int *trapped) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    fusedwasi_p1_stat st;
    if (e->kind == FUSEDWASI_P1_K_STD) {
        if (fusedwasi_p1_std_is_real(w, e) && fusedwasi_p1_be_stat_fd(w, e, &st, trapped) == FUSEDWASI_P1_E_SUCCESS) return fusedwasi_p1_put_filestat(w, ptr, &st, legacy);  /* what it is */
        *trapped = 0;
        memset(&st, 0, sizeof st);  /* a stream of the host: nothing to say but that it is a character device */
        st.filetype = FUSEDWASI_P1_FT_CHAR;
        return fusedwasi_p1_put_filestat(w, ptr, &st, legacy);
    }
    if (!(e->rights & FUSEDWASI_P1_R_FD_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    uint32_t rc = fusedwasi_p1_be_stat_fd(w, e, &st, trapped);
    return rc ? rc : fusedwasi_p1_put_filestat(w, ptr, &st, legacy);
}

static inline uint32_t fusedwasi_p1_filestat_of_path(fusedwasi_p1_state *w, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len,
                                             uint32_t ptr, int legacy, int *trapped) {
    fusedwasi_p1_entry *dir = fusedwasi_p1_entry_of(w, fd);
    if (dir == NULL) return FUSEDWASI_P1_E_BADF;
    if (!(dir->rights & FUSEDWASI_P1_R_PATH_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    fusedwasi_p1_stat st;
    uint32_t rc = fusedwasi_p1_be_stat_path(w, dir, path, len, (flags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, &st, trapped);
    return rc ? rc : fusedwasi_p1_put_filestat(w, ptr, &st, legacy);
}

static inline uint32_t fusedwasi_p1_fd_filestat_get(void *s, uint32_t fd, uint32_t ptr, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    return fusedwasi_p1_filestat_of_fd(w, fd, ptr, 0, trapped);
}

static inline uint32_t fusedwasi_p1_path_filestat_get(void *s, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len, uint32_t ptr,
                                              int *trapped) {
    FUSEDWASI_P1_STATE(s);
    return fusedwasi_p1_filestat_of_path(w, fd, flags, path, len, ptr, 0, trapped);
}

static inline uint32_t fusedwasi_p1_fd_readdir(void *s, uint32_t fd, uint32_t buf, uint32_t size, uint64_t cookie, uint32_t used,
                                       int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READDIR)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (!fusedwasi_p1_in_memory(w, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (cookie == 0 || e->list == NULL) {  /* the listing is made again when the program starts from the beginning */
        fusedwasi_p1_dirlist *list = (fusedwasi_p1_dirlist *)FUSEDWASI_P1_CALLOC(1, sizeof(fusedwasi_p1_dirlist));
        if (list == NULL) return FUSEDWASI_P1_E_NOMEM;
        uint32_t rc = fusedwasi_p1_be_list(w, e, list, trapped);
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
        memcpy(FUSEDWASI_P1_MEM(w) + buf + at, head, part);
        at += (uint32_t)part;
        if (part < sizeof head) break;
        room = size - at;
        part = room < item->namelen ? room : item->namelen;
        memcpy(FUSEDWASI_P1_MEM(w) + buf + at, item->name, part);
        at += (uint32_t)part;
    }
    return fusedwasi_p1_put_u32(w, used, at) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_path_open(void *s, uint32_t fd, uint32_t dirflags, uint32_t path, uint32_t len, uint32_t oflags,
                                      uint64_t base, uint64_t inheriting, uint32_t fdflags, uint32_t out, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir = fusedwasi_p1_entry_of(w, fd);
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
    int64_t number = fusedwasi_p1_new_fd(w);
    if (number < 0) return FUSEDWASI_P1_E_NOMEM;
    dir = &w->entries[fd];  /* the table may have moved when it grew */
    fusedwasi_p1_entry *e = &w->entries[number];
    memset(e, 0, sizeof *e);
    uint32_t rc = fusedwasi_p1_be_open(w, dir, path, len, (dirflags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, oflags, base, fdflags, e, trapped);
    if (rc) {
        fusedwasi_p1_release(e);
        return rc;
    }
    e->rights = base;
    if (e->kind == FUSEDWASI_P1_K_DIR) e->rights &= ~(FUSEDWASI_P1_R_FD_SEEK | FUSEDWASI_P1_R_FD_TELL);  /* a directory is not seeked */
    e->inheriting = inheriting;
    e->flags = (uint16_t)fdflags;
    if (!fusedwasi_p1_put_u32(w, out, (uint32_t)number)) {
        fusedwasi_p1_release(e);
        return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- writing, creating, removing ---------------------------------------------------------------------------------------- */

/* the entry of a file descriptor that has the right, or an errno of WASI */
static inline uint32_t fusedwasi_p1_entry_with(fusedwasi_p1_state *w, uint32_t fd, uint64_t right, fusedwasi_p1_entry **out) {
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    *out = e;
    if (e->kind != FUSEDWASI_P1_K_STD && !(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the directory of a call on a path: it has to be one and to have the right */
static inline uint32_t fusedwasi_p1_dir_with(fusedwasi_p1_state *w, uint32_t fd, uint64_t right, fusedwasi_p1_entry **out) {
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, right, out);
    if (rc) return rc;
    return (*out)->kind == FUSEDWASI_P1_K_DIR ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_NOTDIR;
}

static inline uint32_t fusedwasi_p1_sync_entry(fusedwasi_p1_state *w, uint32_t fd, uint64_t right, int data_only) {
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, right, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SUCCESS;  /* the stream is Python's: it flushes when it wants */
#if FUSEDWASI_P1_DIRS_ARE_PATHS
    (void)data_only;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_SUCCESS;  /* not an open descriptor: nothing to flush */
    return FUSEDWASI_P1_SYNC(e->osfd) < 0 ? fusedwasi_p1_errno(errno) : FUSEDWASI_P1_E_SUCCESS;
#else
#ifdef __linux__
    int failed = data_only ? fdatasync(e->osfd) : FUSEDWASI_P1_SYNC(e->osfd);
#else
    (void)data_only;
    int failed = FUSEDWASI_P1_SYNC(e->osfd);
#endif
    return failed < 0 ? fusedwasi_p1_errno(errno) : FUSEDWASI_P1_E_SUCCESS;
#endif
}

static inline uint32_t fusedwasi_p1_fd_datasync(void *s, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_sync_entry(w, fd, FUSEDWASI_P1_R_FD_DATASYNC, 1);
}

static inline uint32_t fusedwasi_p1_fd_sync(void *s, uint32_t fd, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    return fusedwasi_p1_sync_entry(w, fd, FUSEDWASI_P1_R_FD_SYNC, 0);
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
    return fusedwasi_p1_errno(fusedwasi_p1_truncate_native(e->osfd, size));
}

static inline uint32_t fusedwasi_p1_fd_filestat_set_size(void *s, uint32_t fd, uint64_t size, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE, &e);
    return rc ? rc : fusedwasi_p1_set_size(e, size);
}

/* makes the file at least offset + length long (the file system may not reserve the room, but it is as long) */
static inline uint32_t fusedwasi_p1_fd_allocate(void *s, uint32_t fd, uint64_t offset, uint64_t length, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, FUSEDWASI_P1_R_FD_ALLOCATE, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (e->kind != FUSEDWASI_P1_K_FILE) return FUSEDWASI_P1_E_BADF;
    if (offset > (uint64_t)INT64_MAX || length > (uint64_t)INT64_MAX - offset) return FUSEDWASI_P1_E_FBIG;
#if FUSEDWASI_P1_NATIVE_POLL && (defined(__linux__) || defined(__FreeBSD__))
    {  /* space is really reserved (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE) */
        int err = posix_fallocate(e->osfd, (off_t)offset, (off_t)length);
        if (err == 0) return FUSEDWASI_P1_E_SUCCESS;
        if (err != EINVAL && err != EOPNOTSUPP && err != ENODEV) return fusedwasi_p1_errno(err);  /* a file system that cannot do it gets the old way */
    }
#endif
    fusedwasi_p1_stat st;
    memset(&st, 0, sizeof st);
    rc = fusedwasi_p1_be_stat_fd(w, e, &st, trapped);
    if (rc || *trapped) return rc;
    if (st.filetype != FUSEDWASI_P1_FT_REGULAR) return FUSEDWASI_P1_E_SPIPE; /* a pipe: macOS would truncate it without a word */
    return st.size >= offset + length ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_set_size(e, offset + length);
}

static inline uint32_t fusedwasi_p1_times_flags_ok(uint32_t flags) {
    return flags > 15 || ((flags & FUSEDWASI_P1_T_ATIM) && (flags & FUSEDWASI_P1_T_ATIM_NOW)) || ((flags & FUSEDWASI_P1_T_MTIM) && (flags & FUSEDWASI_P1_T_MTIM_NOW))
               ? FUSEDWASI_P1_E_INVAL
               : FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_fd_filestat_set_times(void *s, uint32_t fd, uint64_t atim, uint64_t mtim, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_TIMES, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_BADF;
    rc = fusedwasi_p1_times_flags_ok(flags);
    return rc ? rc : fusedwasi_p1_be_times_fd(w, e, atim, mtim, flags, trapped);
}

static inline uint32_t fusedwasi_p1_path_filestat_set_times(void *s, uint32_t fd, uint32_t lookup, uint32_t path, uint32_t len, uint64_t atim,
                                                    uint64_t mtim, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R_PATH_FILESTAT_SET_TIMES, &dir);
    if (rc) return rc;
    rc = fusedwasi_p1_times_flags_ok(flags);
    return rc ? rc : fusedwasi_p1_be_times_path(w, dir, path, len, (lookup & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, atim, mtim, flags, trapped);
}

static inline uint32_t fusedwasi_p1_fd_fdstat_set_flags(void *s, uint32_t fd, uint32_t flags, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e;
    uint32_t rc = fusedwasi_p1_entry_with(w, fd, FUSEDWASI_P1_R_FD_FDSTAT_SET_FLAGS, &e);
    if (rc) return rc;
    if (flags > 31) return FUSEDWASI_P1_E_INVAL;
    if (e->kind != FUSEDWASI_P1_K_FILE) return e->kind == FUSEDWASI_P1_K_STD ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_BADF;
#ifndef _WIN32
    int current = fcntl(e->osfd, F_GETFL);
    if (current < 0) return fusedwasi_p1_errno(errno);
    int all = fusedwasi_p1_native_fdflags(FUSEDWASI_P1_FD_APPEND | FUSEDWASI_P1_FD_DSYNC | FUSEDWASI_P1_FD_NONBLOCK | FUSEDWASI_P1_FD_RSYNC | FUSEDWASI_P1_FD_SYNC);
    if (fcntl(e->osfd, F_SETFL, (current & ~all) | fusedwasi_p1_native_fdflags(flags)) < 0) return fusedwasi_p1_errno(errno);
#endif
    e->flags = (uint16_t)flags;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the rights can only be given up, never gained */
static inline uint32_t fusedwasi_p1_fd_fdstat_set_rights(void *s, uint32_t fd, uint64_t base, uint64_t inheriting, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    (void)trapped;
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the rights of a standard stream are fixed: giving some up (or none) is fine, getting more is not (the narrowing is not kept) */
        uint64_t have = e->osfd == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT;
        return (base & ~have) != 0 || inheriting != 0 ? FUSEDWASI_P1_E_NOTCAPABLE : FUSEDWASI_P1_E_SUCCESS;
    }
    if ((base & ~e->rights) != 0 || (inheriting & ~e->inheriting) != 0) return FUSEDWASI_P1_E_NOTCAPABLE;
    e->rights = base;
    e->inheriting = inheriting;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_path_create_directory(void *s, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R_PATH_CREATE_DIRECTORY, &dir);
    return rc ? rc : fusedwasi_p1_be_mkdir(w, dir, path, len, trapped);
}

static inline uint32_t fusedwasi_p1_path_remove_directory(void *s, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R_PATH_REMOVE_DIRECTORY, &dir);
    return rc ? rc : fusedwasi_p1_be_remove(w, dir, path, len, 1, trapped);
}

static inline uint32_t fusedwasi_p1_path_unlink_file(void *s, uint32_t fd, uint32_t path, uint32_t len, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R_PATH_UNLINK_FILE, &dir);
    return rc ? rc : fusedwasi_p1_be_remove(w, dir, path, len, 0, trapped);
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

static inline uint32_t fusedwasi_p1_fd_at(void *s, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t out, int writing) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1_K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind == FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_ISDIR;
    /* the specification asks for fd_seek as well; wasi-libc opens a file for writing without it and calls pwrite all the same */
    uint64_t needed = writing ? FUSEDWASI_P1_R_FD_WRITE : FUSEDWASI_P1_R_FD_READ;
    if (!(e->rights & needed)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (offset > (uint64_t)INT64_MAX) return FUSEDWASI_P1_E_INVAL;
    {
        uint32_t bad = fusedwasi_p1_iovs_valid(w, iovs, count);
        if (bad) return bad;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1_get_u32(w, iovs + 8 * i, &base);
        fusedwasi_p1_get_u32(w, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1_in_memory(w, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        long long n = fusedwasi_p1_at_position(e->osfd, FUSEDWASI_P1_MEM(w) + base, len, offset + total, writing);
        if (n < 0) {
            if (total) break;
            return fusedwasi_p1_errno(errno);
        }
        total += (uint64_t)n;
        if ((uint32_t)n < len) break;
    }
    return fusedwasi_p1_put_u32(w, out, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_fd_pread(void *s, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nread,
                                     int *trapped) {
    (void)trapped;
    return fusedwasi_p1_fd_at(s, fd, iovs, count, offset, nread, 0);
}

static inline uint32_t fusedwasi_p1_fd_pwrite(void *s, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nwritten,
                                      int *trapped) {
    (void)trapped;
    return fusedwasi_p1_fd_at(s, fd, iovs, count, offset, nwritten, 1);
}

static inline uint32_t fusedwasi_p1_path_rename(void *s, uint32_t fd, uint32_t old, uint32_t old_len, uint32_t new_fd, uint32_t new_,
                                        uint32_t new_len, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *from, *to;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R(16), &from);  /* path_rename_source */
    if (rc) return rc;
    rc = fusedwasi_p1_dir_with(w, new_fd, FUSEDWASI_P1_R(17), &to);  /* path_rename_target */
    return rc ? rc : fusedwasi_p1_be_rename(w, from, old, old_len, to, new_, new_len, trapped);
}

static inline uint32_t fusedwasi_p1_path_link(void *s, uint32_t fd, uint32_t flags, uint32_t old, uint32_t old_len, uint32_t new_fd,
                                      uint32_t new_, uint32_t new_len, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *from, *to;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R(11), &from);  /* path_link_source */
    if (rc) return rc;
    rc = fusedwasi_p1_dir_with(w, new_fd, FUSEDWASI_P1_R(12), &to);  /* path_link_target */
    return rc ? rc : fusedwasi_p1_be_link(w, from, old, old_len, (flags & FUSEDWASI_P1_LOOKUP_FOLLOW) != 0, to, new_, new_len, trapped);
}

/* A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made. */
static inline uint32_t fusedwasi_p1_path_symlink(void *s, uint32_t target, uint32_t target_len, uint32_t fd, uint32_t path, uint32_t len,
                                         int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R(24), &dir);  /* path_symlink */
    if (rc) return rc;
    if (!fusedwasi_p1_in_memory(w, target, target_len)) return FUSEDWASI_P1_E_FAULT;
    if (target_len == 0) return FUSEDWASI_P1_E_NOENT;
    if (memchr(FUSEDWASI_P1_MEM(w) + target, 0, target_len) != NULL) return FUSEDWASI_P1_E_INVAL;
    if (FUSEDWASI_P1_MEM(w)[target] == '/') return FUSEDWASI_P1_E_NOTCAPABLE;
    return fusedwasi_p1_be_symlink(w, FUSEDWASI_P1_MEM(w) + target, target_len, dir, path, len, trapped);
}

static inline uint32_t fusedwasi_p1_path_readlink(void *s, uint32_t fd, uint32_t path, uint32_t len, uint32_t buf, uint32_t buf_len,
                                          uint32_t used, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    fusedwasi_p1_entry *dir;
    uint32_t rc = fusedwasi_p1_dir_with(w, fd, FUSEDWASI_P1_R(15), &dir);  /* path_readlink */
    if (rc) return rc;
    if (!fusedwasi_p1_in_memory(w, buf, buf_len)) return FUSEDWASI_P1_E_FAULT;
    char text[FUSEDWASI_P1_LINK_MAX];
    size_t n = 0;
    rc = fusedwasi_p1_be_readlink(w, dir, path, len, text, buf_len < sizeof text ? buf_len : sizeof text, &n, trapped);
    if (rc || *trapped) return rc;
    memcpy(FUSEDWASI_P1_MEM(w) + buf, text, n);
    return fusedwasi_p1_put_u32(w, used, (uint32_t)n) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* poll_oneoff: the descriptors of files are always ready; what makes the program wait is a clock, and it sleeps (in C, without the
   interpreter, so that other threads run) until the first one. `legacy`: the first snapshot, whose clock subscription starts with an identifier. */
/* how long a clock subscription still has to wait (0: it is due), or an errno of WASI */
static inline uint32_t fusedwasi_p1_subscription_wait(fusedwasi_p1_state *w, uint32_t sub, int legacy, uint64_t entry_now[2], int have[2],
                                              uint64_t *wait, int *trapped) {
    uint32_t shift = legacy ? 8 : 0;
    uint32_t id = 0, flags16 = 0;
    uint64_t timeout = 0;
    fusedwasi_p1_get_u32(w, sub + 16 + shift, &id);
    uint32_t lo = 0, hi = 0;
    fusedwasi_p1_get_u32(w, sub + 24 + shift, &lo);
    fusedwasi_p1_get_u32(w, sub + 28 + shift, &hi);
    timeout = ((uint64_t)hi << 32) | lo;
    flags16 = FUSEDWASI_P1_MEM(w)[sub + 40 + shift] | ((uint32_t)FUSEDWASI_P1_MEM(w)[sub + 41 + shift] << 8);
    if (id > 1) return FUSEDWASI_P1_E_INVAL;  /* the clocks of the process and the thread are not something to wait for */
    if (flags16 & ~1u) return FUSEDWASI_P1_E_INVAL;  /* subclockflags has one flag, abstime */
    if (!(flags16 & 1)) {
        *wait = timeout;
        return FUSEDWASI_P1_E_SUCCESS;
    }
    if (!have[id]) {
        uint32_t rc = fusedwasi_p1_clock_ns(w, id, &entry_now[id], trapped);
        if (rc || *trapped) return rc;
        have[id] = 1;
    }
    *wait = timeout > entry_now[id] ? timeout - entry_now[id] : 0;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* A subscription to a descriptor: 0 if it is answered at once (a bad descriptor, a right that is missing, a regular file, a directory, a stream that
   goes through a hook, the standard output); 1 if the system has to say when it is ready (a pipe, a FIFO, a terminal, a socket, the standard input).
   `*error` is the error of the event (a right that is missing is NOTCAPABLE, as the descriptor is not one to wait for) and `*fd` the descriptor to poll. */
static inline int fusedwasi_p1_poll_kind(fusedwasi_p1_state *w, uint32_t sub, uint8_t tag, uint32_t *error, int *fd) {
    uint32_t number = 0;
    fusedwasi_p1_get_u32(w, sub + 16, &number);
    fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, number);
    *error = FUSEDWASI_P1_E_SUCCESS;
    *fd = -1;
    if (e == NULL) {
        *error = FUSEDWASI_P1_E_BADF;
        return 0;
    }
    uint64_t need = FUSEDWASI_P1_R_POLL_FD_READWRITE | (tag == 1 ? FUSEDWASI_P1_R_FD_READ : FUSEDWASI_P1_R_FD_WRITE);
    if (e->kind == FUSEDWASI_P1_K_STD) {  /* the rights of the standard streams are fixed and include polling */
#if FUSEDWASI_P1_NATIVE_POLL
        if (tag == 1 && e->osfd == 0 && w->hooks.stdin_read == NULL) {
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
    if (e->kind == FUSEDWASI_P1_K_FILE && e->osfd >= 0) {
        struct stat st;
        if (fstat(e->osfd, &st) == 0 && !S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            *fd = e->osfd;
            return 1;
        }
    }
#endif
    return 0;
}

static inline uint32_t fusedwasi_p1_poll(fusedwasi_p1_state *w, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents, int legacy,
                                 int *trapped) {
    uint32_t stride = legacy ? 56 : 48;
    if (count == 0) return FUSEDWASI_P1_E_INVAL;
    if (!fusedwasi_p1_in_memory(w, in, (uint64_t)count * stride) || !fusedwasi_p1_in_memory(w, out, (uint64_t)count * 32)) return FUSEDWASI_P1_E_FAULT;
    uint64_t entry_now[2] = {0, 0}, shortest = 0;
    int have[2] = {0, 0}, immediate = 0, timed = 0;  /* timed: a clock was subscribed, so `shortest` is a time (it may be UINT64_MAX: forever) */
    uint32_t polled = 0;                              /* how many descriptors the system has to say about */
    for (uint32_t i = 0; i < count; i++) {  /* first, what is ready and how long the clocks wait */
        uint32_t sub = in + i * stride;
        uint8_t tag = FUSEDWASI_P1_MEM(w)[sub + 8];
        if (tag == 0) {
            uint64_t wait;
            uint32_t rc = fusedwasi_p1_subscription_wait(w, sub, legacy, entry_now, have, &wait, trapped);
            if (rc == FUSEDWASI_P1_E_SUCCESS && !*trapped && (!timed || wait < shortest)) {
                shortest = wait;
                timed = 1;
            }
            if (rc == FUSEDWASI_P1_E_INVAL) immediate = 1;
            else if (rc || *trapped) return rc;
        } else if (tag == 1 || tag == 2) {
            uint32_t error;
            int fd;
            if (fusedwasi_p1_poll_kind(w, sub, tag, &error, &fd)) polled++;
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
            uint8_t tag = FUSEDWASI_P1_MEM(w)[sub + 8];
            int fd;
            slot[i] = -1;
            if ((tag == 1 || tag == 2) && fusedwasi_p1_poll_kind(w, sub, tag, &error, &fd)) {
                fds[k].fd = fd;
                fds[k].events = tag == 1 ? POLLIN : POLLOUT;
                fds[k].revents = 0;
                slot[i] = (int)k++;
            }
        }
        uint64_t started = 0;
        int have_start = fusedwasi_p1_clock_system(1, &started) == 0;
        int ready = 0;
        for (;;) {
            int timeout = 0;  /* ms; the wait is for the clocks, or for ever when there is none */
            if (!immediate) {
                if (!timed) {
                    timeout = -1;
                } else {
                    uint64_t spent = 0, now;
                    if (have_start && fusedwasi_p1_clock_system(1, &now) == 0) spent = now - started;
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
            if (fusedwasi_p1_clock_system(1, &now) == 0) waited = now - started;
        }
    } else
#endif
    if (!immediate && timed && shortest > 0) {
        int slept = fusedwasi_p1_sleep_ns(w, shortest);
        if (slept) return fusedwasi_p1_hook_status(slept, trapped);
        waited = shortest;
    }
    uint32_t made = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sub = in + i * stride, event = out + made * 32;
        uint8_t tag = FUSEDWASI_P1_MEM(w)[sub + 8];
        uint32_t error = FUSEDWASI_P1_E_SUCCESS;
        uint64_t nbytes = 0;
        uint16_t flags = 0;
        if (tag == 0) {
            uint64_t wait;
            error = fusedwasi_p1_subscription_wait(w, sub, legacy, entry_now, have, &wait, trapped);
            if (*trapped) {
                FUSEDWASI_P1_FREE(revents);
                FUSEDWASI_P1_FREE(slot);
                return 0;
            }
            if (error == FUSEDWASI_P1_E_SUCCESS && wait > waited) continue;  /* not yet */
        } else {
            uint32_t number = 0;
            fusedwasi_p1_get_u32(w, sub + 16, &number);
            fusedwasi_p1_entry *e = fusedwasi_p1_entry_of(w, number);
            int fd;
#if FUSEDWASI_P1_NATIVE_POLL
            if (slot != NULL && slot[i] >= 0) {  /* the system has said */
                short r = revents[slot[i]];
                if (!(r & (POLLIN | POLLOUT | POLLHUP | POLLERR | POLLNVAL))) continue;  /* not ready */
                if (r & POLLNVAL) error = FUSEDWASI_P1_E_BADF;
                else if (r & (POLLHUP | POLLERR) && !(r & (POLLIN | POLLOUT))) flags = 1;  /* fd_readwrite_hangup */
                if (tag == 1 && !(r & POLLNVAL)) {
                    int pending = 0;
                    if (ioctl(e->kind == FUSEDWASI_P1_K_STD ? 0 : e->osfd, FIONREAD, &pending) == 0 && pending > 0) nbytes = (uint64_t)pending;
                }
            } else
#endif
            if (!fusedwasi_p1_poll_kind(w, sub, tag, &error, &fd) && error != FUSEDWASI_P1_E_SUCCESS) {
                /* a bad descriptor or a right that is missing: told in the event */
            } else if (e != NULL && e->kind == FUSEDWASI_P1_K_FILE && tag == 1) {
                fusedwasi_p1_stat st;
                memset(&st, 0, sizeof st);
                long long at = FUSEDWASI_P1_LSEEK(e->osfd, 0, SEEK_CUR);
                if (at >= 0 && fusedwasi_p1_be_stat_fd(w, e, &st, trapped) == FUSEDWASI_P1_E_SUCCESS && st.size > (uint64_t)at) nbytes = st.size - (uint64_t)at;
                *trapped = 0;
            }
        }
        memset(FUSEDWASI_P1_MEM(w) + event, 0, 32);
        memcpy(FUSEDWASI_P1_MEM(w) + event, FUSEDWASI_P1_MEM(w) + sub, 8);  /* userdata */
        fusedwasi_p1_put_u16(w, event + 8, (uint16_t)error);
        FUSEDWASI_P1_MEM(w)[event + 10] = tag;
        if (tag != 0) {
            fusedwasi_p1_put_u64(w, event + 16, nbytes);
            fusedwasi_p1_put_u16(w, event + 24, flags);
        }
        made++;
    }
    FUSEDWASI_P1_FREE(revents);
    FUSEDWASI_P1_FREE(slot);
    return fusedwasi_p1_put_u32(w, nevents, made) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1_poll_oneoff(void *s, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents, int *trapped) {
    FUSEDWASI_P1_STATE(s);
    return fusedwasi_p1_poll(w, in, out, count, nevents, 0, trapped);
}

/* ---- sockets: a program cannot make one, so there is none to speak of --------------------------------------------------------- */

/* a descriptor is never a socket (a program cannot make one): the call says so, or that there is no such descriptor */
#define FUSEDWASI_P1_SOCKET(name, ...) \
    static inline uint32_t fusedwasi_p1_##name(void *s, uint32_t fd, __VA_ARGS__, int *trapped) { \
        FUSEDWASI_P1_STATE(s); \
        (void)trapped; \
        return fusedwasi_p1_entry_of(w, fd) == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_NOTSOCK; \
    }

FUSEDWASI_P1_SOCKET(sock_accept, uint32_t b, uint32_t c)
FUSEDWASI_P1_SOCKET(sock_recv, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
FUSEDWASI_P1_SOCKET(sock_send, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
FUSEDWASI_P1_SOCKET(sock_shutdown, uint32_t b)

#endif
