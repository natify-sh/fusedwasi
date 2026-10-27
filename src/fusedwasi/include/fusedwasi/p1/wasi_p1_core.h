/* The calls of WASI 0.1 that `wasi_snapshot_preview1` and `wasi_unstable` (the first snapshot) share: a function `fusedwasi_p1_<name>` for each call of the specification, over the state and the
   guest of WASI 0.2 (`fusedwasi_p2_state`, `fusedwasi_p2_guest`; p2/wasi_p2_core.h). Header only, included by wasi_p1.h and wasi_p1_unstable.h; the functions are `static inline`, so those the
   module does not import are not in the object.

   There is one state and one guest for both families. The configuration of 0.1 is the fields of the state (the arguments, the environment, the standard streams, the preopens, the clocks, the random
   bytes, the sleep); the memory of an instance of 0.1 is memory 0 of its guest. A descriptor of 0.1 is a map to a resource of 0.2 plus what 0.1 keeps for each descriptor (its rights, its position,
   the name of a preopen): a file or a directory is a `descriptor` in the table of the state, the same resource that a module of 0.2 sees in that state, and the functions of 0.1 do their work through
   the operations of 0.2 (p2/wasi_p2_filesystem.h: `fusedwasi_p2__fs_*`, the standard streams, the clocks of the state). The map is private; it is made by the first function of 0.1 that needs it, or by
   `fusedwasi_p1_open_descriptors`, and `fusedwasi_p2_free` frees it with the rest. Many guests of 0.1 may point to one state; they share its descriptors.

   A WASI call is `uint32_t fusedwasi_p1_<name>(fusedwasi_p2_guest *g, parameters...)` (proc_exit returns nothing): it answers the errno of WASI, and a trap is `g->trap` (it does not return). The end of
   the program is the guest's, as in 0.2: `proc_exit` is wasi:cli/exit (the hook `exit` of the guest, which by default records `exited` and `exit_code` and traps); the host reads them after the call and
   clears them before the next one. `proc_raise` asks the hook `signal` of the guest (without one it answers NOTSUP and the program goes on).

   The two snapshots differ in four things (the order of `whence`, one right less, a 32-bit link count in `filestat`, an extra
   `identifier` in a clock subscription) and in `sock_accept`, which the first one has not. None of the calls built so far reads or
   writes them, so the same function serves both; the day one does, wasi_p1_unstable.h defines `fusedwasi_p1_unstable_<name>`, which the glue
   then calls for it.

   Host API (docs/public_api.md): the functions of its imports (`c_name` in signatures/wasi_p1.json), and
     FUSEDWASI_P1_FN, fusedwasi_p1_open_descriptors, fusedwasi_p1_errno, FUSEDWASI_P1_E_*, FUSEDWASI_P1_FT_*, FUSEDWASI_P1_FD_*, FUSEDWASI_P1_O_*,
     FUSEDWASI_P1_R_*, FUSEDWASI_P1_RIGHTS_STDIN, FUSEDWASI_P1_RIGHTS_STDOUT, FUSEDWASI_P1_T_*, FUSEDWASI_P1_LOOKUP_FOLLOW.
   Every other name in this file is private: a double underscore after the prefix (`fusedwasi_p2__x`, `FUSEDWASI_P2__X`), and a private field ends with two underscores (`x__`). */
#ifndef FUSEDWASI_P1__CORE_H
#define FUSEDWASI_P1__CORE_H

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
#define FUSEDWASI_P1__READ(fd, buf, n) _read((fd), (buf), (unsigned int)(n))
#define FUSEDWASI_P1__WRITE(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#define FUSEDWASI_P1__SYNC(fd) _commit(fd)
#define FUSEDWASI_P1__LSEEK(fd, off, whence) _lseeki64((fd), (off), (whence))
#define FUSEDWASI_P1__CLOSE(fd) _close(fd)
#else
#include <unistd.h>
#define FUSEDWASI_P1__READ(fd, buf, n) read((fd), (buf), (n))
#define FUSEDWASI_P1__WRITE(fd, buf, n) write((fd), (buf), (n))
#define FUSEDWASI_P1__SYNC(fd) fsync(fd)
#define FUSEDWASI_P1__LSEEK(fd, off, whence) lseek((fd), (off), (whence))
#define FUSEDWASI_P1__CLOSE(fd) close(fd)
#endif
/* Windows has no openat and cannot open a directory: there a directory is a path */
#if defined(_WIN32)
#define FUSEDWASI_P1__DIRS_ARE_PATHS 1
#else
#define FUSEDWASI_P1__DIRS_ARE_PATHS 0
#endif
#include <stdlib.h>
#include <time.h>
#include "../p2/wasi_p2_cli.h"
#include "../p2/wasi_p2_filesystem.h"  /* the state of WASI 0.2 that this is over; it includes windows.h and bcrypt.h (-lbcrypt where there is no pragma) */
#if !FUSEDWASI_P1__DIRS_ARE_PATHS
#include <dirent.h>
#include <sys/stat.h>
#endif

/* the descriptors are the system's own and can be asked about with poll and fstat: not on Windows */
#ifndef _WIN32
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#define FUSEDWASI_P1__NATIVE_POLL 1
#else
#define FUSEDWASI_P1__NATIVE_POLL 0
#endif

/* a call of the specification (the others are helpers of this file); the table of signatures finds them by this word */
#define FUSEDWASI_P1_FN static inline

#define FUSEDWASI_P1__LINK_MAX 4096  /* the most that path_readlink reads of a link */

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
#define FUSEDWASI_P1__R(n) (UINT64_C(1) << (n))
#define FUSEDWASI_P1_R_FD_DATASYNC FUSEDWASI_P1__R(0)
#define FUSEDWASI_P1_R_FD_READ FUSEDWASI_P1__R(1)
#define FUSEDWASI_P1_R_FD_SEEK FUSEDWASI_P1__R(2)
#define FUSEDWASI_P1_R_FD_FDSTAT_SET_FLAGS FUSEDWASI_P1__R(3)
#define FUSEDWASI_P1_R_FD_SYNC FUSEDWASI_P1__R(4)
#define FUSEDWASI_P1_R_FD_TELL FUSEDWASI_P1__R(5)
#define FUSEDWASI_P1_R_FD_WRITE FUSEDWASI_P1__R(6)
#define FUSEDWASI_P1_R_FD_ALLOCATE FUSEDWASI_P1__R(8)
#define FUSEDWASI_P1_R_PATH_CREATE_DIRECTORY FUSEDWASI_P1__R(9)
#define FUSEDWASI_P1_R_PATH_CREATE_FILE FUSEDWASI_P1__R(10)
#define FUSEDWASI_P1_R_PATH_OPEN FUSEDWASI_P1__R(13)
#define FUSEDWASI_P1_R_FD_READDIR FUSEDWASI_P1__R(14)
#define FUSEDWASI_P1_R_PATH_FILESTAT_GET FUSEDWASI_P1__R(18)
#define FUSEDWASI_P1_R_PATH_FILESTAT_SET_SIZE FUSEDWASI_P1__R(19)
#define FUSEDWASI_P1_R_PATH_FILESTAT_SET_TIMES FUSEDWASI_P1__R(20)
#define FUSEDWASI_P1_R_FD_FILESTAT_GET FUSEDWASI_P1__R(21)
#define FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE FUSEDWASI_P1__R(22)
#define FUSEDWASI_P1_R_FD_FILESTAT_SET_TIMES FUSEDWASI_P1__R(23)
#define FUSEDWASI_P1_R_POLL_FD_READWRITE FUSEDWASI_P1__R(27)
#define FUSEDWASI_P1_R_FD_ADVISE FUSEDWASI_P1__R(7)
#define FUSEDWASI_P1_R_PATH_LINK_SOURCE FUSEDWASI_P1__R(11)
#define FUSEDWASI_P1_R_PATH_LINK_TARGET FUSEDWASI_P1__R(12)
#define FUSEDWASI_P1_R_PATH_READLINK FUSEDWASI_P1__R(15)
#define FUSEDWASI_P1_R_PATH_RENAME_SOURCE FUSEDWASI_P1__R(16)
#define FUSEDWASI_P1_R_PATH_RENAME_TARGET FUSEDWASI_P1__R(17)
#define FUSEDWASI_P1_R_PATH_SYMLINK FUSEDWASI_P1__R(24)
#define FUSEDWASI_P1_R_SOCK_SHUTDOWN FUSEDWASI_P1__R(28)
#define FUSEDWASI_P1_R_PATH_REMOVE_DIRECTORY FUSEDWASI_P1__R(25)
#define FUSEDWASI_P1_R_PATH_UNLINK_FILE FUSEDWASI_P1__R(26)
#define FUSEDWASI_P1_R_ALL ((UINT64_C(1) << 30) - 1)
/* the rights only a directory has: a program that asks for one of them along with the right to write (wasi-libc asks for all the
   rights there are, and so does the scratch directory of the test suite) may mean a directory, and then writing is not what is meant */
#define FUSEDWASI_P1_R_OF_DIRECTORY \
    (FUSEDWASI_P1__R(9) | FUSEDWASI_P1__R(10) | FUSEDWASI_P1__R(11) | FUSEDWASI_P1__R(12) | FUSEDWASI_P1__R(13) | FUSEDWASI_P1__R(14) | FUSEDWASI_P1__R(15) | FUSEDWASI_P1__R(16) | FUSEDWASI_P1__R(17) | FUSEDWASI_P1__R(18) | \
     FUSEDWASI_P1__R(19) | FUSEDWASI_P1__R(20) | FUSEDWASI_P1__R(24) | FUSEDWASI_P1__R(25) | FUSEDWASI_P1__R(26))
/* what changes something: taken away from a preopen that is read-only */
#define FUSEDWASI_P1_R_MUTATING \
    (FUSEDWASI_P1__R(0) | FUSEDWASI_P1__R(4) | FUSEDWASI_P1__R(6) | FUSEDWASI_P1__R(8) | FUSEDWASI_P1__R(9) | FUSEDWASI_P1__R(10) | FUSEDWASI_P1__R(11) | FUSEDWASI_P1__R(12) | FUSEDWASI_P1__R(16) | FUSEDWASI_P1__R(17) | \
     FUSEDWASI_P1__R(19) | FUSEDWASI_P1__R(20) | FUSEDWASI_P1__R(22) | FUSEDWASI_P1__R(23) | FUSEDWASI_P1__R(24) | FUSEDWASI_P1__R(25) | FUSEDWASI_P1__R(26))
/* the rights of the standard streams: fd_read, fd_fdstat_set_flags, fd_filestat_get, poll_fd_readwrite / fd_write, fd_sync ... */
#define FUSEDWASI_P1_RIGHTS_STDIN UINT64_C(0x820000a)
#define FUSEDWASI_P1_RIGHTS_STDOUT UINT64_C(0x8200058)

/* the names of a directory, as fd_readdir gives them */
typedef struct {
    uint64_t ino;
    uint32_t namelen;
    uint8_t type;
    char *name;
} fusedwasi_p1__dirent;
typedef struct {
    fusedwasi_p1__dirent *items;
    size_t count, capacity;
} fusedwasi_p1__dirlist;

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

/* ---- the descriptor map ------------------------------------------------------------------------------------------------------ */

/* An entry of the map: a standard stream (`handle` is 0, 1 or 2: the place in `std[]` of the state), a file or a directory (`handle` is the handle of a `descriptor` in the table of
   the state: the resource of 0.2). What 0.1 keeps for the descriptor is the rest. */
enum { FUSEDWASI_P1__K_NONE = 0, FUSEDWASI_P1__K_STD, FUSEDWASI_P1__K_FILE, FUSEDWASI_P1__K_DIR };
typedef struct {
    uint8_t kind;
    uint8_t filetype;
    uint16_t flags;      /* fdflags */
    uint32_t handle;
    uint64_t rights, inheriting;
    uint64_t position;   /* the offset of the next fd_read and fd_write of a file (0.2 has none in a descriptor) */
    char *preopen;       /* the name the program sees, for a preopen */
    size_t preopen_len;
    fusedwasi_p1__dirlist *list;  /* fd_readdir: the names of the directory, made at cookie 0 */
} fusedwasi_p1__entry;

typedef struct {
    fusedwasi_p1__entry *entries;
    uint32_t count;
} fusedwasi_p1__fds;

#define FUSEDWASI_P1__OPTS FUSEDWASI_P2__FS_P1

/* the memory of the instance: memory 0 of the guest (little-endian, as WebAssembly says) */
static inline uint8_t *fusedwasi_p1__mem(const fusedwasi_p2_guest *g) { return *g->memory__[0].data; }

static inline int fusedwasi_p1__in_memory(const fusedwasi_p2_guest *g, uint32_t ptr, uint64_t len) {
    return g->memories__ > 0 && g->memory__[0].data != NULL && g->memory__[0].size != NULL && (uint64_t)ptr + len <= *g->memory__[0].size;
}

/* errno of the C library for a code of 0.2 (what `fusedwasi_p1_open_descriptors` tells when a directory of a preopen cannot be opened) */
static inline int fusedwasi_p1__errno_of_code(int code) {
    switch (code) {
    case FUSEDWASI_P2_E_ACCESS: return EACCES;
    case FUSEDWASI_P2_E_NO_ENTRY: return ENOENT;
    case FUSEDWASI_P2_E_NOT_DIRECTORY: return ENOTDIR;
    case FUSEDWASI_P2_E_NAME_TOO_LONG: return ENAMETOOLONG;
    case FUSEDWASI_P2_E_INSUFFICIENT_MEMORY: return ENOMEM;
    case FUSEDWASI_P2_E_UNSUPPORTED: return ENOSYS;
    default: return EIO;
    }
}

static inline char *fusedwasi_p1__copy(const char *text, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (out != NULL) {
        memcpy(out, text, len);
        out[len] = '\0';
    }
    return out;
}

static inline void fusedwasi_p1__dirlist_free(fusedwasi_p1__dirlist *list) {
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i].name);
    free(list->items);
    free(list);
}

/* lets go of what the entry holds: a file or a directory is dropped from the table of the state (and closed with it), a standard stream stays the state's */
static inline void fusedwasi_p1__release(fusedwasi_p2_state *state, fusedwasi_p1__entry *e) {
    if (e->kind == FUSEDWASI_P1__K_FILE || e->kind == FUSEDWASI_P1__K_DIR) {
        fusedwasi_p2__slot *slot = fusedwasi_p2__table_find(state, e->handle, FUSEDWASI_P2_KIND_DESCRIPTOR);
        if (slot) {
            slot->live = 0;
            if (slot->destroy) slot->destroy(state, slot->obj);
            slot->obj = NULL;
        }
    }
    free(e->preopen);
    fusedwasi_p1__dirlist_free(e->list);
    memset(e, 0, sizeof *e);
}

static inline void fusedwasi_p1__fds_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p1__fds *fds = (fusedwasi_p1__fds *)obj;
    for (uint32_t i = 0; i < fds->count; i++) fusedwasi_p1__release(state, &fds->entries[i]);
    free(fds->entries);
    free(fds);
}

/* The descriptors of 0.1, made once for the state: 0, 1 and 2 are the standard streams, 3 and up the preopens in order. NULL on failure with *err an errno of the C library; when it
   is the directory of a preopen that could not be opened, *bad is its place in `preopens`, otherwise (size_t)-1. */
static inline fusedwasi_p1__fds *fusedwasi_p1__make_fds(fusedwasi_p2_state *state, int *err, size_t *bad) {
    *bad = (size_t)-1;
    fusedwasi_p1__fds *fds = (fusedwasi_p1__fds *)calloc(1, sizeof *fds);
    uint32_t handle = 0;
    if (fds) {
        fds->count = 8;
        fds->entries = (fusedwasi_p1__entry *)calloc(fds->count, sizeof(fusedwasi_p1__entry));
        if (fds->entries) handle = fusedwasi_p2_table_add(state, FUSEDWASI_P2__KIND_P1_FDS, fds, fusedwasi_p1__fds_destroy);
    }
    if (!handle) {
        if (fds) free(fds->entries);
        free(fds);
        *err = ENOMEM;
        return NULL;
    }
    for (int i = 0; i < 3; i++) {
        fds->entries[i].kind = FUSEDWASI_P1__K_STD;
        fds->entries[i].handle = (uint32_t)i;  /* which stream it is, wherever it is renumbered to */
    }
    for (uint32_t i = 0; i < state->preopen_count; i++) {
        const struct fusedwasi_p2_preopen *p = &state->preopens[i];
        fusedwasi_p1__entry *e;
        if (3 + i >= fds->count) {
            uint32_t n = fds->count * 2;
            fusedwasi_p1__entry *bigger = (fusedwasi_p1__entry *)realloc(fds->entries, (size_t)n * sizeof *bigger);
            if (!bigger) {
                *err = ENOMEM;
                goto fail;
            }
            memset(bigger + fds->count, 0, (size_t)(n - fds->count) * sizeof *bigger);
            fds->entries = bigger;
            fds->count = n;
        }
        e = &fds->entries[3 + i];
        fusedwasi_p2__descriptor made;
        int code = fusedwasi_p2__sys_open_directory(p->path, &made);
        if (code != FUSEDWASI_P2__OK) {
            *err = fusedwasi_p1__errno_of_code(code);
            *bad = i;
            goto fail;
        }
        e->handle = fusedwasi_p2__descriptor_new(state, &made, FUSEDWASI_P2__READ | (p->writable ? FUSEDWASI_P2__MUTATE : 0));
        e->preopen_len = strlen(p->name);
        e->preopen = fusedwasi_p1__copy(p->name, e->preopen_len);
        if (!e->handle || !e->preopen) {
            if (!e->handle) fusedwasi_p2__sys_close(&made);
            *err = ENOMEM;
            *bad = (size_t)-1;
            e->kind = FUSEDWASI_P1__K_DIR;  /* so that the release drops what was made */
            goto fail;
        }
        e->kind = FUSEDWASI_P1__K_DIR;
        e->filetype = FUSEDWASI_P1_FT_DIR;
        e->rights = e->inheriting = p->writable ? FUSEDWASI_P1_R_ALL : (FUSEDWASI_P1_R_ALL & ~FUSEDWASI_P1_R_MUTATING);
    }
    state->p1_table__ = handle;
    return fds;
fail:
    {
        fusedwasi_p2__slot *slot = fusedwasi_p2__table_find(state, handle, FUSEDWASI_P2__KIND_P1_FDS);
        if (slot) {
            slot->live = 0;
            slot->obj = NULL;
        }
        fusedwasi_p1__fds_destroy(state, fds);
    }
    return NULL;
}

/* Makes the descriptors of 0.1 now, so that a directory of `preopens` that cannot be opened is told here (0, or -1 with *err an errno of the C library and *bad its place in the list,
   (size_t)-1 when it was not a directory). The functions of 0.1 do it at their first call when this was not called; nothing is made twice. */
static inline int fusedwasi_p1_open_descriptors(fusedwasi_p2_state *state, int *err, size_t *bad) {
    *bad = (size_t)-1;
    if (state->p1_table__) return 0;
    return fusedwasi_p1__make_fds(state, err, bad) ? 0 : -1;
}

/* the descriptors of the state of the guest, made if they were not; NULL with *rc the errno of WASI of the failure */
static inline fusedwasi_p1__fds *fusedwasi_p1__fds_get(fusedwasi_p2_guest *g, uint32_t *rc) {
    fusedwasi_p2__slot *slot = fusedwasi_p2__table_find(g->state, g->state->p1_table__, FUSEDWASI_P2__KIND_P1_FDS);
    if (slot) return (fusedwasi_p1__fds *)slot->obj;
    int err = 0;
    size_t bad;
    fusedwasi_p1__fds *fds = fusedwasi_p1__make_fds(g->state, &err, &bad);
    if (!fds) *rc = err == ENOMEM ? FUSEDWASI_P1_E_NOMEM : err == EACCES ? FUSEDWASI_P1_E_ACCES : err == ENOENT ? FUSEDWASI_P1_E_NOENT : err == ENOTDIR ? FUSEDWASI_P1_E_NOTDIR : FUSEDWASI_P1_E_IO;
    return fds;
}

/* at the top of a function that looks at descriptors */
#define FUSEDWASI_P1__FDS(g) \
    uint32_t fds_rc = FUSEDWASI_P1_E_SUCCESS; \
    fusedwasi_p1__fds *fds = fusedwasi_p1__fds_get((g), &fds_rc); \
    if (fds == NULL) return fds_rc

static inline fusedwasi_p1__entry *fusedwasi_p1__entry_of(fusedwasi_p1__fds *fds, uint32_t fd) {
    return fd < fds->count && fds->entries[fd].kind != FUSEDWASI_P1__K_NONE ? &fds->entries[fd] : NULL;
}

/* the lowest free number from 3 on; -1 when there is no memory (the table may have moved) */
static inline int64_t fusedwasi_p1__new_fd(fusedwasi_p1__fds *fds) {
    for (uint32_t i = 3; i < fds->count; i++)
        if (fds->entries[i].kind == FUSEDWASI_P1__K_NONE) return i;
    uint32_t n = fds->count * 2;
    fusedwasi_p1__entry *bigger = (fusedwasi_p1__entry *)realloc(fds->entries, (size_t)n * sizeof(fusedwasi_p1__entry));
    if (bigger == NULL) return -1;
    memset(bigger + fds->count, 0, (size_t)(n - fds->count) * sizeof(fusedwasi_p1__entry));
    uint32_t first = fds->count;
    fds->entries = bigger;
    fds->count = n;
    return first;
}

/* the resource of 0.2 behind a file or a directory */
static inline fusedwasi_p2__descriptor *fusedwasi_p1__desc(fusedwasi_p2_guest *g, const fusedwasi_p1__entry *e) {
    return fusedwasi_p2__descriptor_find(g->state, e->handle);
}

/* ---- the memory of the instance ---------------------------------------------------------------------------------------------- */

static inline int fusedwasi_p1__put_u8(fusedwasi_p2_guest *g, uint32_t ptr, uint8_t v) {
    if (!fusedwasi_p1__in_memory(g, ptr, 1)) return 0;
    fusedwasi_p1__mem(g)[ptr] = v;
    return 1;
}

static inline int fusedwasi_p1__put_u16(fusedwasi_p2_guest *g, uint32_t ptr, uint16_t v) {
    if (!fusedwasi_p1__in_memory(g, ptr, 2)) return 0;
    fusedwasi_p1__mem(g)[ptr] = (uint8_t)v;
    fusedwasi_p1__mem(g)[ptr + 1] = (uint8_t)(v >> 8);
    return 1;
}

static inline int fusedwasi_p1__put_u32(fusedwasi_p2_guest *g, uint32_t ptr, uint32_t v) {
    if (!fusedwasi_p1__in_memory(g, ptr, 4)) return 0;
    for (int i = 0; i < 4; i++) fusedwasi_p1__mem(g)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1__put_u64(fusedwasi_p2_guest *g, uint32_t ptr, uint64_t v) {
    if (!fusedwasi_p1__in_memory(g, ptr, 8)) return 0;
    for (int i = 0; i < 8; i++) fusedwasi_p1__mem(g)[ptr + i] = (uint8_t)(v >> (8 * i));
    return 1;
}

static inline int fusedwasi_p1__get_u32(const fusedwasi_p2_guest *g, uint32_t ptr, uint32_t *v) {
    if (!fusedwasi_p1__in_memory(g, ptr, 4)) return 0;
    *v = 0;
    for (int i = 0; i < 4; i++) *v |= (uint32_t)fusedwasi_p1__mem(g)[ptr + i] << (8 * i);
    return 1;
}

/* ---- arguments and environment (wasi:cli/environment: the fields of the state) ------------------------------------------------ */

static inline uint32_t fusedwasi_p1__strings_sizes(fusedwasi_p2_guest *g, const char *const *items, uint32_t count, uint32_t count_ptr, uint32_t size_ptr) {
    size_t size = 0;
    for (uint32_t i = 0; i < count; i++) size += strlen(items[i]) + 1;
    if (!fusedwasi_p1__put_u32(g, count_ptr, count) || !fusedwasi_p1__put_u32(g, size_ptr, (uint32_t)size)) return FUSEDWASI_P1_E_FAULT;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the strings are in memory of the instance as the specification says; the environment is "NAME=value" (`values` is NULL for the arguments) */
static inline uint32_t fusedwasi_p1__strings_get(fusedwasi_p2_guest *g, const char *const *items, const char *const *values, uint32_t count, uint32_t table, uint32_t out) {
    size_t size = 0;
    for (uint32_t i = 0; i < count; i++) size += strlen(items[i]) + (values != NULL ? 1 + strlen(values[i]) : 0) + 1;
    if (!fusedwasi_p1__in_memory(g, out, size) || !fusedwasi_p1__in_memory(g, table, (uint64_t)count * 4)) return FUSEDWASI_P1_E_FAULT;
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        fusedwasi_p1__put_u32(g, table + 4 * i, out + (uint32_t)offset);
        size_t n = strlen(items[i]);
        memcpy(fusedwasi_p1__mem(g) + out + offset, items[i], n);
        offset += n;
        if (values != NULL) {
            fusedwasi_p1__mem(g)[out + offset++] = '=';
            n = strlen(values[i]);
            memcpy(fusedwasi_p1__mem(g) + out + offset, values[i], n);
            offset += n;
        }
        fusedwasi_p1__mem(g)[out + offset++] = 0;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_args_sizes_get(fusedwasi_p2_guest *g, uint32_t argc, uint32_t size) {
    return fusedwasi_p1__strings_sizes(g, g->state->args, g->state->arg_count, argc, size);
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_args_get(fusedwasi_p2_guest *g, uint32_t argv, uint32_t buf) {
    return fusedwasi_p1__strings_get(g, g->state->args, NULL, g->state->arg_count, argv, buf);
}

/* the sizes of the environment count the "=" */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_environ_sizes_get(fusedwasi_p2_guest *g, uint32_t count, uint32_t size) {
    size_t total = 0;
    for (uint32_t i = 0; i < g->state->env_count; i++) total += strlen(g->state->env_names[i]) + 1 + strlen(g->state->env_values[i]) + 1;
    if (!fusedwasi_p1__put_u32(g, count, g->state->env_count) || !fusedwasi_p1__put_u32(g, size, (uint32_t)total)) return FUSEDWASI_P1_E_FAULT;
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_environ_get(fusedwasi_p2_guest *g, uint32_t environ_, uint32_t buf) {
    return fusedwasi_p1__strings_get(g, g->state->env_names, g->state->env_values, g->state->env_count, environ_, buf);
}

/* ---- clocks and random bytes (the clocks, the random source and the sleep of the state) ------------------------------------------------ */

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_clock_res_get(fusedwasi_p2_guest *g, uint32_t id, uint32_t ptr) {
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1__put_u64(g, ptr, g->state->resolution_ns[id]) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* the time of a clock; a clock that cannot be read is NOTSUP (0.2 traps) */
static inline uint32_t fusedwasi_p1__clock_ns(fusedwasi_p2_guest *g, uint32_t id, uint64_t *ns) {
    if (id > 3) return FUSEDWASI_P1_E_INVAL;
    return g->state->clock(g->state, id, ns) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_NOTSUP;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_clock_time_get(fusedwasi_p2_guest *g, uint32_t id, uint64_t precision, uint32_t ptr) {
    (void)precision;
    uint64_t ns = 0;
    uint32_t rc = fusedwasi_p1__clock_ns(g, id, &ns);
    if (rc != FUSEDWASI_P1_E_SUCCESS) return rc;
    return fusedwasi_p1__put_u64(g, ptr, ns) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_random_get(fusedwasi_p2_guest *g, uint32_t buf, uint32_t size) {
    if (!fusedwasi_p1__in_memory(g, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (size == 0) return FUSEDWASI_P1_E_SUCCESS;
    return g->state->random_bytes(g->state, fusedwasi_p1__mem(g) + buf, size) == 0 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_IO;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_sched_yield(fusedwasi_p2_guest *g) {
    (void)g;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* proc_exit is wasi:cli/exit: the hook `exit` of the guest is told (the default records `exited` and `exit_code` and traps) */
FUSEDWASI_P1_FN void fusedwasi_p1_proc_exit(fusedwasi_p2_guest *g, uint32_t code) { g->exit(g, code); }

/* proc_raise: the signals of WASI 0.1 by number. What a signal does to a program is the host's decision, not WASI's, so the library does not end the program: it asks the hook
   `signal` of the guest, which returns the errno of the call or ends the program by its own means. Without the hook the answer is NOTSUP and the program goes on (as in Wasmtime).
   A number above 30 is INVAL, and 0 (no signal) is success. */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_proc_raise(fusedwasi_p2_guest *g, uint32_t sig) {
    if (sig > 30) return FUSEDWASI_P1_E_INVAL;
    if (sig == 0) return FUSEDWASI_P1_E_SUCCESS;
    return g->signal ? g->signal(g, sig) : FUSEDWASI_P1_E_NOTSUP;
}

/* ---- the standard streams (wasi:cli/stdin, stdout, stderr: `std[]` of the state, else the descriptors of the process) --------------------------------- */

/* All the buffers of a call are looked at before any of them is used: a bad one in the middle must not leave the data before it already written (or read)
   with an error and no count (found by comparing with uvwasi). */
static inline uint32_t fusedwasi_p1__iovs_valid(fusedwasi_p2_guest *g, uint32_t iovs, uint32_t count) {
    if (!fusedwasi_p1__in_memory(g, iovs, (uint64_t)count * 8)) return FUSEDWASI_P1_E_FAULT;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the status of a stream that failed: a closed one is a broken pipe; the errno of the system, when the stream knows it, as it is */
static inline uint32_t fusedwasi_p1__stream_status(int result, const fusedwasi_p2_stream *st) {
    if (result == FUSEDWASI_P2_STREAM_CLOSED) return FUSEDWASI_P1_E_PIPE;
    return st->failure_errno ? fusedwasi_p1_errno(st->failure_errno) : FUSEDWASI_P1_E_IO;
}

/* what was written is reported (a short write); the error comes with the next call */
static inline uint32_t fusedwasi_p1__write_std(fusedwasi_p2_guest *g, uint32_t which, uint32_t iovs, uint32_t count, uint32_t nwritten) {
    {
        uint32_t bad = fusedwasi_p1__iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    fusedwasi_p2_stream *st = fusedwasi_p2__std(g, (int)which);
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        int result = st->write(g->state, st, fusedwasi_p1__mem(g) + base, len);
        if (result != FUSEDWASI_P2_STREAM_OK) {
            if (total == 0) return fusedwasi_p1__stream_status(result, st);
            break;
        }
        total += len;
    }
    return fusedwasi_p1__put_u32(g, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

static inline uint32_t fusedwasi_p1__read_std(fusedwasi_p2_guest *g, uint32_t iovs, uint32_t count, uint32_t nread) {
    {
        uint32_t bad = fusedwasi_p1__iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    uint32_t base = 0, len = 0;
    for (uint32_t i = 0; i < count; i++) {  /* the first buffer that is not empty: a short read is allowed */
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (len) break;
    }
    if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
    uint64_t got = 0;
    if (len) {
        fusedwasi_p2_stream *st = fusedwasi_p2__std(g, 0);
        for (;;) {  /* a stream of 0.2 gives nothing when nothing is ready; a read of 0.1 waits for a byte or the end */
            int result = st->read(g->state, st, fusedwasi_p1__mem(g) + base, len, &got);
            if (result == FUSEDWASI_P2_STREAM_FAILED) return fusedwasi_p1__stream_status(result, st);
            if (result == FUSEDWASI_P2_STREAM_CLOSED) {
                got = 0;
                break;
            }
            if (got > len) return FUSEDWASI_P1_E_IO;
            if (got > 0) break;
            g->wait(g);
        }
    }
    return fusedwasi_p1__put_u32(g, nread, (uint32_t)got) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* ---- from a code of 0.2 to an errno of WASI -------------------------------------------------------------------------------------- */

/* the errno of WASI for a code of 0.2 (the host of Wasmtime does the same for 0.1 over 0.2); a way out of the directory (FS_P1) is not a capability the program has */
static inline uint32_t fusedwasi_p1__of_code(int code) {
    switch (code) {
    case FUSEDWASI_P2__OK: return FUSEDWASI_P1_E_SUCCESS;
    case FUSEDWASI_P2__E_ESCAPE: return FUSEDWASI_P1_E_NOTCAPABLE;
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

/* the answer of a function that called an operation of 0.2: the clock that cannot be read is what 0.2 traps for */
static inline uint32_t fusedwasi_p1__rc(fusedwasi_p2_guest *g, int code) {
    if (code == FUSEDWASI_P2__E_NO_CLOCK) g->trap(g, "the clock cannot be read");
    return fusedwasi_p1__of_code(code);
}

/* the type of a file of 0.2 as the filetype of WASI */
static inline uint8_t fusedwasi_p1__filetype_of_type(uint8_t type) {
    switch (type) {
    case FUSEDWASI_P2__T_DIRECTORY: return FUSEDWASI_P1_FT_DIR;
    case FUSEDWASI_P2__T_REGULAR: return FUSEDWASI_P1_FT_REGULAR;
    case FUSEDWASI_P2__T_SYMLINK: return FUSEDWASI_P1_FT_SYMLINK;
    case FUSEDWASI_P2__T_CHAR: return FUSEDWASI_P1_FT_CHAR;
    case FUSEDWASI_P2__T_BLOCK: return FUSEDWASI_P1_FT_BLOCK;
    case FUSEDWASI_P2__T_SOCKET: return FUSEDWASI_P1_FT_SOCKET_STREAM;
    default: return FUSEDWASI_P1_FT_UNKNOWN;
    }
}

/* a time of a file in nanoseconds since 1970 as WASI has it (unsigned): before 1970 is 0 */
static inline uint64_t fusedwasi_p1__time_of(int64_t ns) { return ns < 0 ? 0 : (uint64_t)ns; }

/* what the specification calls filestat, the same for both ways of getting it */
typedef struct {
    uint64_t dev, ino, nlink, size, atim, mtim, ctim;
    uint8_t filetype;
} fusedwasi_p1__stat;

static inline void fusedwasi_p1__stat_of_p2(const fusedwasi_p2__stat *st, fusedwasi_p1__stat *out) {
    out->dev = st->dev;
    out->ino = st->ino;
    out->nlink = st->nlink;
    out->size = st->size;
    out->atim = fusedwasi_p1__time_of(st->atime_ns);
    out->mtim = fusedwasi_p1__time_of(st->mtime_ns);
    out->ctim = fusedwasi_p1__time_of(st->ctime_ns);
    out->filetype = fusedwasi_p1__filetype_of_type(st->type);
}

/* the filestat of the first snapshot has a 32-bit link count and 56 bytes */
static inline uint32_t fusedwasi_p1__put_filestat(fusedwasi_p2_guest *g, uint32_t ptr, const fusedwasi_p1__stat *st, int legacy) {
    size_t total = legacy ? 56 : 64;
    if (!fusedwasi_p1__in_memory(g, ptr, total)) return FUSEDWASI_P1_E_FAULT;
    memset(fusedwasi_p1__mem(g) + ptr, 0, total);
    fusedwasi_p1__put_u64(g, ptr, st->dev);
    fusedwasi_p1__put_u64(g, ptr + 8, st->ino);
    fusedwasi_p1__put_u8(g, ptr + 16, st->filetype);
    if (legacy) {
        fusedwasi_p1__put_u32(g, ptr + 20, (uint32_t)st->nlink);
        fusedwasi_p1__put_u64(g, ptr + 24, st->size);
        fusedwasi_p1__put_u64(g, ptr + 32, st->atim);
        fusedwasi_p1__put_u64(g, ptr + 40, st->mtim);
        fusedwasi_p1__put_u64(g, ptr + 48, st->ctim);
    } else {
        fusedwasi_p1__put_u64(g, ptr + 24, st->nlink);
        fusedwasi_p1__put_u64(g, ptr + 32, st->size);
        fusedwasi_p1__put_u64(g, ptr + 40, st->atim);
        fusedwasi_p1__put_u64(g, ptr + 48, st->mtim);
        fusedwasi_p1__put_u64(g, ptr + 56, st->ctim);
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the fdflags as flags of the system (the ones it has); append is the adapter's own */
static inline int fusedwasi_p1__native_fdflags(uint32_t fdflags) {
    (void)fdflags; /* unused where no flag is taken over (Windows without O_DSYNC and O_RSYNC) */
    int flags = 0;
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

/* A standard stream that is the descriptor of the process (the state gave no stream for it): it is what the system says it is. wasi-libc takes a character
   device without the right to seek for a terminal, so a stream that was redirected to a file or a pipe must not say it is one (found by comparing with
   uvwasi, which asks the descriptor). A stream that goes through a hook (Python's `sys.stdout`) has no descriptor to ask and stays a character device. */
static inline int fusedwasi_p1__std_is_real(fusedwasi_p2_guest *g, const fusedwasi_p1__entry *e) {
#if FUSEDWASI_P1__NATIVE_POLL
    return e->kind == FUSEDWASI_P1__K_STD && g->state->std[e->handle] == NULL;
#else
    (void)g, (void)e;
    return 0;
#endif
}

static inline uint8_t fusedwasi_p1__std_filetype(fusedwasi_p2_guest *g, const fusedwasi_p1__entry *e) {
#if FUSEDWASI_P1__NATIVE_POLL
    struct stat st;
    if (fusedwasi_p1__std_is_real(g, e) && fstat((int)e->handle, &st) == 0) return fusedwasi_p1__filetype_of_type(fusedwasi_p2__type_of(st.st_mode)); /* a pipe is unknown: not a terminal */
#endif
    (void)g, (void)e;
    return FUSEDWASI_P1_FT_CHAR;
}

/* ---- reading, writing, seeking, closing ------------------------------------------------------------------------------------- */

/* A file that cannot be read or written at an offset (a pipe, a FIFO, a terminal opened by a path) is read and written at the position of the system, as before. */
static inline int fusedwasi_p1__unseekable(int code) { return code == FUSEDWASI_P2_E_INVALID_SEEK; }

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_write(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nwritten) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL || e->kind == FUSEDWASI_P1__K_DIR) return e == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_ISDIR;
    if (e->kind == FUSEDWASI_P1__K_STD) return e->handle == 0 ? FUSEDWASI_P1_E_BADF : fusedwasi_p1__write_std(g, e->handle, iovs, count, nwritten);
    if (!(e->rights & FUSEDWASI_P1_R_FD_WRITE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1__iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        uint64_t at = e->position, done = 0;
        int code = FUSEDWASI_P2__OK;
        fusedwasi_p2__stat st;
        if ((e->flags & FUSEDWASI_P1_FD_APPEND) && fusedwasi_p2__fs_stat(d, &st) == FUSEDWASI_P2__OK) at = st.size;  /* the write goes to the end of the file */
        code = fusedwasi_p2__fs_write(d, fusedwasi_p1__mem(g) + base, len, at, &done, FUSEDWASI_P1__OPTS);
        if (fusedwasi_p1__unseekable(code)) {
            long long n = FUSEDWASI_P1__WRITE(d->fd, fusedwasi_p1__mem(g) + base, len);
            if (n < 0) {
                if (total) break;
                return fusedwasi_p1_errno(errno);
            }
            total += (uint64_t)n;
            if ((uint32_t)n < len) break;
            continue;
        }
        e->position = at + done;
        total += done;
        if (code != FUSEDWASI_P2__OK) {
            if (total) break;
            return fusedwasi_p1__of_code(code);
        }
    }
    return fusedwasi_p1__put_u32(g, nwritten, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_read(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t nread) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_STD) return e->handle == 0 ? fusedwasi_p1__read_std(g, iovs, count, nread) : FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READ)) return FUSEDWASI_P1_E_NOTCAPABLE;
    {
        uint32_t bad = fusedwasi_p1__iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        uint64_t got = 0;
        int code = fusedwasi_p2__fs_read(d, fusedwasi_p1__mem(g) + base, len, e->position, &got, FUSEDWASI_P1__OPTS);  /* straight into the memory of the module */
        if (fusedwasi_p1__unseekable(code)) {
            long long n = FUSEDWASI_P1__READ(d->fd, fusedwasi_p1__mem(g) + base, len);
            if (n < 0) {
                if (total) break;  /* what was read is reported; the error comes with the next call */
                return fusedwasi_p1_errno(errno);
            }
            got = (uint64_t)n;
        } else if (code != FUSEDWASI_P2__OK) {
            if (total) break;
            return fusedwasi_p1__of_code(code);
        } else {
            e->position += got;
        }
        total += got;
        if (got < len) break;
        /* A pipe, a FIFO, a terminal: when the first buffer is full, a read into the next one may wait for more although something has been read, and a
           short read is allowed (found by comparing with uvwasi, which reads all the buffers with one readv). Only a regular file goes on. */
        if (got > 0 && i + 1 < count && e->filetype != FUSEDWASI_P1_FT_REGULAR) break;
    }
    return fusedwasi_p1__put_u32(g, nread, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* what fd_seek asks of the descriptor: its right, or (a seek by 0 bytes from the current position is a tell) the right to tell */
#define FUSEDWASI_P1__SEEK_RIGHT(offset, native) \
    (FUSEDWASI_P1_R_FD_SEEK | ((offset) == 0 && (native) == SEEK_CUR ? FUSEDWASI_P1_R_FD_TELL : 0))

/* The position is the adapter's. A file that is not a regular one (a pipe, a device) is asked of the system, which says what it says. */
static inline uint32_t fusedwasi_p1__seek_native(fusedwasi_p2_guest *g, uint32_t fd, int64_t offset, int native, uint32_t ptr, uint64_t right) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind != FUSEDWASI_P1__K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    uint64_t result;
    if (e->filetype != FUSEDWASI_P1_FT_REGULAR) {
        long long pos = FUSEDWASI_P1__LSEEK(d->fd, offset, native);
        if (pos < 0) return fusedwasi_p1_errno(errno);
        result = (uint64_t)pos;
    } else {
        int64_t from = 0;
        if (native == SEEK_CUR) {
            from = (int64_t)e->position;
        } else if (native == SEEK_END) {
            fusedwasi_p2__stat st;
            int code = fusedwasi_p2__fs_stat(d, &st);
            if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
            from = (int64_t)st.size;
        }
        if ((offset > 0 && from > INT64_MAX - offset) || (offset < 0 && from + offset < 0)) return FUSEDWASI_P1_E_INVAL;
        result = (uint64_t)(from + offset);
        e->position = result;
    }
    return fusedwasi_p1__put_u64(g, ptr, result) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* the order of whence in this snapshot: set, cur, end */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_seek(fusedwasi_p2_guest *g, uint32_t fd, uint64_t offset, uint32_t whence, uint32_t ptr) {
    static const int native[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
    if (whence > 2) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1__seek_native(g, fd, (int64_t)offset, native[whence], ptr, FUSEDWASI_P1__SEEK_RIGHT((int64_t)offset, native[whence]));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_tell(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) {
    return fusedwasi_p1__seek_native(g, fd, 0, SEEK_CUR, ptr, FUSEDWASI_P1_R_FD_TELL);
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_close(fusedwasi_p2_guest *g, uint32_t fd) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_STD) {  /* the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF */
        memset(e, 0, sizeof *e);
        return FUSEDWASI_P1_E_SUCCESS;
    }
    fusedwasi_p1__release(g->state, e);  /* the resource is dropped from the table of the state, and closed */
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_renumber(fusedwasi_p2_guest *g, uint32_t from, uint32_t to) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *a = fusedwasi_p1__entry_of(fds, from), *b = fusedwasi_p1__entry_of(fds, to);
    if (a == NULL || b == NULL) return FUSEDWASI_P1_E_BADF;
    if (from == to) return FUSEDWASI_P1_E_SUCCESS;
    fusedwasi_p1__release(g->state, b);
    *b = *a;
    memset(a, 0, sizeof *a);
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_advise(fusedwasi_p2_guest *g, uint32_t fd, uint64_t offset, uint64_t length, uint32_t advice) {
    (void)offset;
    (void)length;
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL || e->kind != FUSEDWASI_P1__K_FILE) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & FUSEDWASI_P1_R_FD_ADVISE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (advice > 5) return FUSEDWASI_P1_E_INVAL;  /* the advice is one of six */
    return FUSEDWASI_P1_E_SUCCESS;  /* a hint (descriptor.advise of 0.2 does nothing either) */
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_fdstat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    uint8_t filetype = e->kind == FUSEDWASI_P1__K_STD ? fusedwasi_p1__std_filetype(g, e) : e->filetype;
    uint64_t base = e->kind == FUSEDWASI_P1__K_STD ? (e->handle == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT) : e->rights;
    if (!fusedwasi_p1__in_memory(g, ptr, 24)) return FUSEDWASI_P1_E_FAULT;
    memset(fusedwasi_p1__mem(g) + ptr, 0, 24);
    fusedwasi_p1__put_u8(g, ptr, filetype);
    fusedwasi_p1__put_u16(g, ptr + 2, e->flags);
    fusedwasi_p1__put_u64(g, ptr + 8, base);
    fusedwasi_p1__put_u64(g, ptr + 16, e->inheriting);
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_prestat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;  /* the end of the list the program walks */
    if (!fusedwasi_p1__in_memory(g, ptr, 8)) return FUSEDWASI_P1_E_FAULT;
    memset(fusedwasi_p1__mem(g) + ptr, 0, 8);  /* the type of a preopen: a directory, 0 */
    fusedwasi_p1__put_u32(g, ptr + 4, (uint32_t)e->preopen_len);
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_prestat_dir_name(fusedwasi_p2_guest *g, uint32_t fd, uint32_t path, uint32_t size) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL || e->preopen == NULL) return FUSEDWASI_P1_E_BADF;
    if (size < e->preopen_len) return FUSEDWASI_P1_E_NAMETOOLONG;
    if (!fusedwasi_p1__in_memory(g, path, e->preopen_len)) return FUSEDWASI_P1_E_FAULT;
    memcpy(fusedwasi_p1__mem(g) + path, e->preopen, e->preopen_len);
    return FUSEDWASI_P1_E_SUCCESS;
}

/* ---- what a file or a directory is ---------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1__filestat_of_fd(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr, int legacy) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    fusedwasi_p1__stat st;
    fusedwasi_p2__stat raw;
    if (e->kind == FUSEDWASI_P1__K_STD) {
#if FUSEDWASI_P1__NATIVE_POLL
        struct stat sys;
        if (fusedwasi_p1__std_is_real(g, e) && fstat((int)e->handle, &sys) == 0) {  /* what it is */
            fusedwasi_p2__stat_from(&sys, &raw);
            fusedwasi_p1__stat_of_p2(&raw, &st);
            return fusedwasi_p1__put_filestat(g, ptr, &st, legacy);
        }
#endif
        memset(&st, 0, sizeof st);  /* a stream of the host: nothing to say but that it is a character device (with one name) */
        st.filetype = FUSEDWASI_P1_FT_CHAR;
        st.nlink = 1;
        return fusedwasi_p1__put_filestat(g, ptr, &st, legacy);
    }
    if (!(e->rights & FUSEDWASI_P1_R_FD_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    int code = fusedwasi_p2__fs_stat(d, &raw);
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
    fusedwasi_p1__stat_of_p2(&raw, &st);
    return fusedwasi_p1__put_filestat(g, ptr, &st, legacy);
}

/* the directory of a call on a path: it has to be one, to have the right and to be there; its resource of 0.2 comes with it */
static inline uint32_t fusedwasi_p1__dir_with(fusedwasi_p2_guest *g, fusedwasi_p1__fds *fds, uint32_t fd, uint64_t right, fusedwasi_p2__descriptor **out) {
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind != FUSEDWASI_P1__K_STD && !(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (e->kind != FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    *out = fusedwasi_p1__desc(g, e);
    return *out ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_BADF;
}

static inline uint32_t fusedwasi_p1__filestat_of_path(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len, uint32_t ptr, int legacy) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (!(e->rights & FUSEDWASI_P1_R_PATH_FILESTAT_GET)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (e->kind != FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    fusedwasi_p2__stat raw;
    fusedwasi_p1__stat st;
    int code = fusedwasi_p2__fs_stat_at(d, fusedwasi_p1__mem(g) + path, len, flags & FUSEDWASI_P1_LOOKUP_FOLLOW ? FUSEDWASI_P2__SYMLINK_FOLLOW : 0, &raw, FUSEDWASI_P1__OPTS);
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
    fusedwasi_p1__stat_of_p2(&raw, &st);
    return fusedwasi_p1__put_filestat(g, ptr, &st, legacy);
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_filestat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) { return fusedwasi_p1__filestat_of_fd(g, fd, ptr, 0); }

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_filestat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len, uint32_t ptr) {
    return fusedwasi_p1__filestat_of_path(g, fd, flags, path, len, ptr, 0);
}

/* ---- the directory listing, as the specification wants it: "." and "..", then the names in order ------------------------ */

static inline int fusedwasi_p1__dirent_order(const void *a, const void *b) {
    const fusedwasi_p1__dirent *x = (const fusedwasi_p1__dirent *)a, *y = (const fusedwasi_p1__dirent *)b;
    int rx = strcmp(x->name, ".") == 0 ? 0 : strcmp(x->name, "..") == 0 ? 1 : 2;
    int ry = strcmp(y->name, ".") == 0 ? 0 : strcmp(y->name, "..") == 0 ? 1 : 2;
    return rx != ry ? rx - ry : strcmp(x->name, y->name);
}

static inline int fusedwasi_p1__dirlist_add(fusedwasi_p1__dirlist *list, const char *name, uint64_t ino, uint8_t type) {
    if (list->count == list->capacity) {  /* the room doubles: a directory of many names is not copied again and again */
        size_t room = list->capacity ? list->capacity * 2 : 128;
        fusedwasi_p1__dirent *bigger = (fusedwasi_p1__dirent *)realloc(list->items, room * sizeof(fusedwasi_p1__dirent));
        if (bigger == NULL) return -1;
        list->items = bigger;
        list->capacity = room;
    }
    char *copy = fusedwasi_p1__copy(name, strlen(name));
    if (copy == NULL) return -1;
    fusedwasi_p1__dirent *item = &list->items[list->count++];
    item->name = copy;
    item->namelen = (uint32_t)strlen(name);
    item->ino = ino;
    item->type = type;
    return 0;
}

/* the names of the directory (descriptor.read-directory), with "." and ".." first, the inode of each from a stat of the name */
static inline uint32_t fusedwasi_p1__list(const fusedwasi_p2__descriptor *d, fusedwasi_p1__dirlist *list) {
    char **names;
    uint8_t *types;
    uint32_t count;
    int code = fusedwasi_p2__fs_read_directory(d, &names, &types, &count);
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    for (uint32_t i = 0; i < count + 2 && rc == FUSEDWASI_P1_E_SUCCESS; i++) {  /* "." and "..", then the names */
        const char *name = i == 0 ? "." : i == 1 ? ".." : names[i - 2];
        uint8_t type = i < 2 ? FUSEDWASI_P1_FT_DIR : fusedwasi_p1__filetype_of_type(types[i - 2]);
        fusedwasi_p2__stat st;
        uint64_t ino = 0;
        if (fusedwasi_p2__fs_stat_entry(d, name, &st) == FUSEDWASI_P2__OK) ino = st.ino;
        if (fusedwasi_p1__dirlist_add(list, name, ino, type) < 0) rc = FUSEDWASI_P1_E_NOMEM;
    }
    for (uint32_t i = 0; i < count; i++) free(names[i]);
    free(names);
    free(types);
    return rc;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_readdir(fusedwasi_p2_guest *g, uint32_t fd, uint32_t buf, uint32_t size, uint64_t cookie, uint32_t used) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind != FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!(e->rights & FUSEDWASI_P1_R_FD_READDIR)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (!fusedwasi_p1__in_memory(g, buf, size)) return FUSEDWASI_P1_E_FAULT;
    if (cookie == 0 || e->list == NULL) {  /* the listing is made again when the program starts from the beginning */
        fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
        if (d == NULL) return FUSEDWASI_P1_E_BADF;
        fusedwasi_p1__dirlist *list = (fusedwasi_p1__dirlist *)calloc(1, sizeof(fusedwasi_p1__dirlist));
        if (list == NULL) return FUSEDWASI_P1_E_NOMEM;
        uint32_t rc = fusedwasi_p1__list(d, list);
        if (rc) {
            fusedwasi_p1__dirlist_free(list);
            return rc;
        }
        if (list->count > 1) qsort(list->items, list->count, sizeof(fusedwasi_p1__dirent), fusedwasi_p1__dirent_order);  /* the same order for both ways */
        fusedwasi_p1__dirlist_free(e->list);
        e->list = list;
    }
    uint32_t at = 0;
    for (uint64_t i = cookie; i < e->list->count && at < size; i++) {
        const fusedwasi_p1__dirent *item = &e->list->items[i];
        unsigned char head[24];
        memset(head, 0, sizeof head);
        uint64_t next = i + 1;
        for (int k = 0; k < 8; k++) head[k] = (unsigned char)(next >> (8 * k));
        for (int k = 0; k < 8; k++) head[8 + k] = (unsigned char)(item->ino >> (8 * k));
        for (int k = 0; k < 4; k++) head[16 + k] = (unsigned char)(item->namelen >> (8 * k));
        head[20] = item->type;
        size_t room = size - at, part = room < sizeof head ? room : sizeof head;  /* the last entry may be cut short */
        memcpy(fusedwasi_p1__mem(g) + buf + at, head, part);
        at += (uint32_t)part;
        if (part < sizeof head) break;
        room = size - at;
        part = room < item->namelen ? room : item->namelen;
        memcpy(fusedwasi_p1__mem(g) + buf + at, item->name, part);
        at += (uint32_t)part;
    }
    return fusedwasi_p1__put_u32(g, used, at) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* ---- opening, creating, removing ------------------------------------------------------------------------------------------ */

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_open(fusedwasi_p2_guest *g, uint32_t fd, uint32_t dirflags, uint32_t path, uint32_t len, uint32_t oflags, uint64_t base, uint64_t inheriting,
                                              uint32_t fdflags, uint32_t out) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *dir = fusedwasi_p1__entry_of(fds, fd);
    if (dir == NULL) return FUSEDWASI_P1_E_BADF;
    if (dir->kind != FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if ((oflags >> 4) != 0 || (fdflags >> 5) != 0) return FUSEDWASI_P1_E_INVAL;
    if (!(dir->rights & FUSEDWASI_P1_R_PATH_OPEN)) return FUSEDWASI_P1_E_NOTCAPABLE;
    base &= dir->inheriting & FUSEDWASI_P1_R_ALL;
    inheriting &= dir->inheriting & FUSEDWASI_P1_R_ALL;
    if (oflags & FUSEDWASI_P1_O_CREAT) {
        if (oflags & FUSEDWASI_P1_O_DIRECTORY) return FUSEDWASI_P1_E_INVAL;
        if (!(dir->rights & FUSEDWASI_P1_R_PATH_CREATE_FILE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    }
    if ((oflags & FUSEDWASI_P1_O_TRUNC) && !(dir->rights & FUSEDWASI_P1_R_PATH_FILESTAT_SET_SIZE)) return FUSEDWASI_P1_E_NOTCAPABLE;
    fusedwasi_p2__descriptor *parent = fusedwasi_p1__desc(g, dir);
    if (parent == NULL) return FUSEDWASI_P1_E_BADF;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    /* writing is what the rights say: to write, to sync the data, to allocate, to set the size (uvwasi; wasi-libc leaves these out of a read-only open) */
    int writable = (base & (FUSEDWASI_P1_R_FD_WRITE | FUSEDWASI_P1_R_FD_DATASYNC | FUSEDWASI_P1_R_FD_ALLOCATE | FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE)) != 0 || (oflags & FUSEDWASI_P1_O_TRUNC) != 0,
        readable = (base & FUSEDWASI_P1_R_FD_READ) != 0;
    uint32_t flags = (writable ? FUSEDWASI_P2__WRITE : 0) | (readable || !writable ? FUSEDWASI_P2__READ : 0);
    if (fdflags & (FUSEDWASI_P1_FD_SYNC | FUSEDWASI_P1_FD_RSYNC)) flags |= FUSEDWASI_P2__FILE_SYNC;
    if (fdflags & FUSEDWASI_P1_FD_DSYNC) flags |= FUSEDWASI_P2__DATA_SYNC;
    uint32_t open_flags = ((oflags & FUSEDWASI_P1_O_CREAT) ? FUSEDWASI_P2__CREATE : 0) | ((oflags & FUSEDWASI_P1_O_DIRECTORY) ? FUSEDWASI_P2__DIRECTORY : 0) |
                          ((oflags & FUSEDWASI_P1_O_EXCL) ? FUSEDWASI_P2__EXCLUSIVE : 0) | ((oflags & FUSEDWASI_P1_O_TRUNC) ? FUSEDWASI_P2__TRUNCATE : 0);
    uint32_t follow = (dirflags & FUSEDWASI_P1_LOOKUP_FOLLOW) ? FUSEDWASI_P2__SYMLINK_FOLLOW : 0;
    fusedwasi_p2__descriptor made;
    memset(&made, 0, sizeof made);
    made.fd = -1;
    int code = fusedwasi_p2__fs_open_at(parent, fusedwasi_p1__mem(g) + path, len, follow, open_flags, flags, &made, FUSEDWASI_P1__OPTS);
    if (code == FUSEDWASI_P2_E_IS_DIRECTORY && (base & FUSEDWASI_P1_R_OF_DIRECTORY) && !(oflags & (FUSEDWASI_P1_O_CREAT | FUSEDWASI_P1_O_TRUNC)))
        code = fusedwasi_p2__fs_open_at(parent, fusedwasi_p1__mem(g) + path, len, follow, FUSEDWASI_P2__DIRECTORY, FUSEDWASI_P2__READ, &made, FUSEDWASI_P1__OPTS);  /* it was a directory, and asked for as one */
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
    fusedwasi_p2__stat st;
    code = fusedwasi_p2__fs_stat(&made, &st);
    if (code != FUSEDWASI_P2__OK) {
        fusedwasi_p2__sys_close(&made);
        return fusedwasi_p1__of_code(code);
    }
#ifndef _WIN32  /* what the open of 0.2 does not take: not blocking (append is the adapter's own: fd_write goes to the end) */
    if (made.fd >= 0 && (fdflags & FUSEDWASI_P1_FD_NONBLOCK) && fcntl(made.fd, F_SETFL, fcntl(made.fd, F_GETFL) | fusedwasi_p1__native_fdflags(FUSEDWASI_P1_FD_NONBLOCK)) < 0) {
        int saved = errno;
        fusedwasi_p2__sys_close(&made);
        return fusedwasi_p1_errno(saved);
    }
#endif
    uint32_t handle = fusedwasi_p2__descriptor_new(g->state, &made, made.flags);
    if (!handle) {
        fusedwasi_p2__sys_close(&made);
        return FUSEDWASI_P1_E_NOMEM;
    }
    int64_t number = fusedwasi_p1__new_fd(fds);
    if (number < 0) {
        fusedwasi_p2__slot *slot = fusedwasi_p2__table_find(g->state, handle, FUSEDWASI_P2_KIND_DESCRIPTOR);
        slot->live = 0;
        slot->destroy(g->state, slot->obj);
        return FUSEDWASI_P1_E_NOMEM;
    }
    fusedwasi_p1__entry *e = &fds->entries[number];
    memset(e, 0, sizeof *e);
    e->kind = made.is_dir ? FUSEDWASI_P1__K_DIR : FUSEDWASI_P1__K_FILE;
    e->filetype = fusedwasi_p1__filetype_of_type(st.type);
    e->handle = handle;
    e->rights = base;
    if (e->kind == FUSEDWASI_P1__K_DIR) e->rights &= ~(FUSEDWASI_P1_R_FD_SEEK | FUSEDWASI_P1_R_FD_TELL);  /* a directory is not seeked */
    e->inheriting = inheriting;
    e->flags = (uint16_t)fdflags;
    if (!fusedwasi_p1__put_u32(g, out, (uint32_t)number)) {
        fusedwasi_p1__release(g->state, e);
        return FUSEDWASI_P1_E_FAULT;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the entry of a file descriptor that has the right, or an errno of WASI */
static inline uint32_t fusedwasi_p1__entry_with(fusedwasi_p1__fds *fds, uint32_t fd, uint64_t right, fusedwasi_p1__entry **out) {
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    *out = e;
    if (e->kind != FUSEDWASI_P1__K_STD && !(e->rights & right)) return FUSEDWASI_P1_E_NOTCAPABLE;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1__sync_entry(fusedwasi_p2_guest *g, uint32_t fd, uint64_t right, int data_only) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e;
    uint32_t rc = fusedwasi_p1__entry_with(fds, fd, right, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1__K_STD) return FUSEDWASI_P1_E_SUCCESS;  /* the stream is the host's: it flushes when it wants */
#if FUSEDWASI_P1__DIRS_ARE_PATHS
    if (e->kind == FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_SUCCESS;  /* not an open descriptor: nothing to flush */
#endif
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_sync(d, data_only));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_datasync(fusedwasi_p2_guest *g, uint32_t fd) { return fusedwasi_p1__sync_entry(g, fd, FUSEDWASI_P1_R_FD_DATASYNC, 1); }

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_sync(fusedwasi_p2_guest *g, uint32_t fd) { return fusedwasi_p1__sync_entry(g, fd, FUSEDWASI_P1_R_FD_SYNC, 0); }

static inline uint32_t fusedwasi_p1__set_size(fusedwasi_p2_guest *g, fusedwasi_p1__entry *e, uint64_t size) {
    if (e->kind == FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (e->kind != FUSEDWASI_P1__K_FILE) return FUSEDWASI_P1_E_BADF;
    if (size > (uint64_t)INT64_MAX) return FUSEDWASI_P1_E_FBIG;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_set_size(d, size, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_filestat_set_size(fusedwasi_p2_guest *g, uint32_t fd, uint64_t size) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e;
    uint32_t rc = fusedwasi_p1__entry_with(fds, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_SIZE, &e);
    return rc ? rc : fusedwasi_p1__set_size(g, e, size);
}

/* makes the file at least offset + length long (the file system may not reserve the room, but it is as long) */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_allocate(fusedwasi_p2_guest *g, uint32_t fd, uint64_t offset, uint64_t length) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e;
    uint32_t rc = fusedwasi_p1__entry_with(fds, fd, FUSEDWASI_P1_R_FD_ALLOCATE, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_ISDIR;
    if (e->kind != FUSEDWASI_P1__K_FILE) return FUSEDWASI_P1_E_BADF;
    if (offset > (uint64_t)INT64_MAX || length > (uint64_t)INT64_MAX - offset) return FUSEDWASI_P1_E_FBIG;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
#if FUSEDWASI_P1__NATIVE_POLL && (defined(__linux__) || defined(__FreeBSD__))
    {  /* no operation of 0.2 reserves room: it is reserved here (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE) */
        int err = posix_fallocate(d->fd, (off_t)offset, (off_t)length);
        if (err == 0) return FUSEDWASI_P1_E_SUCCESS;
        if (err != EINVAL && err != EOPNOTSUPP && err != ENODEV) return fusedwasi_p1_errno(err);  /* a file system that cannot do it gets the old way */
    }
#endif
    fusedwasi_p2__stat st;
    int code = fusedwasi_p2__fs_stat(d, &st);
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);
    if (fusedwasi_p1__filetype_of_type(st.type) != FUSEDWASI_P1_FT_REGULAR) return FUSEDWASI_P1_E_SPIPE; /* a pipe: macOS would truncate it without a word */
    return st.size >= offset + length ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1__set_size(g, e, offset + length);
}

static inline uint32_t fusedwasi_p1__times_flags_ok(uint32_t flags) {
    return flags > 15 || ((flags & FUSEDWASI_P1_T_ATIM) && (flags & FUSEDWASI_P1_T_ATIM_NOW)) || ((flags & FUSEDWASI_P1_T_MTIM) && (flags & FUSEDWASI_P1_T_MTIM_NOW))
               ? FUSEDWASI_P1_E_INVAL
               : FUSEDWASI_P1_E_SUCCESS;
}

/* the flags of 0.1 (a value, now, none) as the new-timestamps of 0.2 */
static inline void fusedwasi_p1__times(fusedwasi_p2__times *t, uint64_t atim, uint64_t mtim, uint32_t flags) {
    t->a_tag = (flags & FUSEDWASI_P1_T_ATIM) ? 2 : (flags & FUSEDWASI_P1_T_ATIM_NOW) ? 1 : 0;
    t->m_tag = (flags & FUSEDWASI_P1_T_MTIM) ? 2 : (flags & FUSEDWASI_P1_T_MTIM_NOW) ? 1 : 0;
    t->a_ns = (int64_t)atim;
    t->m_ns = (int64_t)mtim;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_filestat_set_times(fusedwasi_p2_guest *g, uint32_t fd, uint64_t atim, uint64_t mtim, uint32_t flags) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e;
    uint32_t rc = fusedwasi_p1__entry_with(fds, fd, FUSEDWASI_P1_R_FD_FILESTAT_SET_TIMES, &e);
    if (rc) return rc;
    if (e->kind == FUSEDWASI_P1__K_STD) return FUSEDWASI_P1_E_BADF;
    rc = fusedwasi_p1__times_flags_ok(flags);
    if (rc) return rc;
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    fusedwasi_p2__times t;
    fusedwasi_p1__times(&t, atim, mtim, flags);
    return fusedwasi_p1__rc(g, fusedwasi_p2__fs_set_times(g->state, d, &t, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_filestat_set_times(fusedwasi_p2_guest *g, uint32_t fd, uint32_t lookup, uint32_t path, uint32_t len, uint64_t atim, uint64_t mtim, uint32_t flags) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1_R_PATH_FILESTAT_SET_TIMES, &d);
    if (rc) return rc;
    rc = fusedwasi_p1__times_flags_ok(flags);
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    fusedwasi_p2__times t;
    fusedwasi_p1__times(&t, atim, mtim, flags);
    return fusedwasi_p1__rc(g, fusedwasi_p2__fs_set_times_at(g->state, d, fusedwasi_p1__mem(g) + path, len, lookup & FUSEDWASI_P1_LOOKUP_FOLLOW ? FUSEDWASI_P2__SYMLINK_FOLLOW : 0, &t, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_fdstat_set_flags(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e;
    uint32_t rc = fusedwasi_p1__entry_with(fds, fd, FUSEDWASI_P1_R_FD_FDSTAT_SET_FLAGS, &e);
    if (rc) return rc;
    if (flags > 31) return FUSEDWASI_P1_E_INVAL;
    if (e->kind != FUSEDWASI_P1__K_FILE) return e->kind == FUSEDWASI_P1__K_STD ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_BADF;
#ifndef _WIN32  /* append is kept here and done by fd_write; no operation of 0.2 sets the others after the open, so they are set on the system descriptor */
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    int current = fcntl(d->fd, F_GETFL);
    if (current < 0) return fusedwasi_p1_errno(errno);
    int all = fusedwasi_p1__native_fdflags(FUSEDWASI_P1_FD_DSYNC | FUSEDWASI_P1_FD_NONBLOCK | FUSEDWASI_P1_FD_RSYNC | FUSEDWASI_P1_FD_SYNC);
    if (fcntl(d->fd, F_SETFL, (current & ~all) | fusedwasi_p1__native_fdflags(flags)) < 0) return fusedwasi_p1_errno(errno);
#endif
    e->flags = (uint16_t)flags;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the rights can only be given up, never gained */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_fdstat_set_rights(fusedwasi_p2_guest *g, uint32_t fd, uint64_t base, uint64_t inheriting) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_STD) {  /* the rights of a standard stream are fixed: giving some up (or none) is fine, getting more is not (the narrowing is not kept) */
        uint64_t have = e->handle == 0 ? FUSEDWASI_P1_RIGHTS_STDIN : FUSEDWASI_P1_RIGHTS_STDOUT;
        return (base & ~have) != 0 || inheriting != 0 ? FUSEDWASI_P1_E_NOTCAPABLE : FUSEDWASI_P1_E_SUCCESS;
    }
    if ((base & ~e->rights) != 0 || (inheriting & ~e->inheriting) != 0) return FUSEDWASI_P1_E_NOTCAPABLE;
    e->rights = base;
    e->inheriting = inheriting;
    return FUSEDWASI_P1_E_SUCCESS;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_create_directory(fusedwasi_p2_guest *g, uint32_t fd, uint32_t path, uint32_t len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1_R_PATH_CREATE_DIRECTORY, &d);
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_create_directory_at(d, fusedwasi_p1__mem(g) + path, len, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_remove_directory(fusedwasi_p2_guest *g, uint32_t fd, uint32_t path, uint32_t len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1_R_PATH_REMOVE_DIRECTORY, &d);
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_remove_directory_at(d, fusedwasi_p1__mem(g) + path, len, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_unlink_file(fusedwasi_p2_guest *g, uint32_t fd, uint32_t path, uint32_t len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1_R_PATH_UNLINK_FILE, &d);
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_unlink_file_at(d, fusedwasi_p1__mem(g) + path, len, FUSEDWASI_P1__OPTS));
}

/* ---- positions, renaming, links ----------------------------------------------------------------------------------------------- */

/* read or write at a position, leaving the position of the descriptor where it was */
static inline uint32_t fusedwasi_p1__fd_at(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t out, int writing) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, fd);
    if (e == NULL) return FUSEDWASI_P1_E_BADF;
    if (e->kind == FUSEDWASI_P1__K_STD) return FUSEDWASI_P1_E_SPIPE;
    if (e->kind == FUSEDWASI_P1__K_DIR) return FUSEDWASI_P1_E_ISDIR;
    /* the specification asks for fd_seek as well; wasi-libc opens a file for writing without it and calls pwrite all the same */
    uint64_t needed = writing ? FUSEDWASI_P1_R_FD_WRITE : FUSEDWASI_P1_R_FD_READ;
    if (!(e->rights & needed)) return FUSEDWASI_P1_E_NOTCAPABLE;
    if (offset > (uint64_t)INT64_MAX) return FUSEDWASI_P1_E_INVAL;
    {
        uint32_t bad = fusedwasi_p1__iovs_valid(g, iovs, count);
        if (bad) return bad;
    }
    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
    if (d == NULL) return FUSEDWASI_P1_E_BADF;
    uint64_t total = 0;
#ifndef _WIN32  /* a descriptor opened for appending: the system writes at the end whatever the offset (as the descriptor did before 0.1 went over 0.2) */
    if (writing && (e->flags & FUSEDWASI_P1_FD_APPEND)) {
        fusedwasi_p2__stat st;
        if (fusedwasi_p2__fs_stat(d, &st) == FUSEDWASI_P2__OK) offset = st.size;
    }
#endif
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, len = 0;
        fusedwasi_p1__get_u32(g, iovs + 8 * i, &base);
        fusedwasi_p1__get_u32(g, iovs + 8 * i + 4, &len);
        if (!fusedwasi_p1__in_memory(g, base, len)) return FUSEDWASI_P1_E_FAULT;
        if (len == 0) continue;
        uint64_t n = 0;
        int code = writing ? fusedwasi_p2__fs_write(d, fusedwasi_p1__mem(g) + base, len, offset + total, &n, FUSEDWASI_P1__OPTS)
                           : fusedwasi_p2__fs_read(d, fusedwasi_p1__mem(g) + base, len, offset + total, &n, FUSEDWASI_P1__OPTS);
        total += n;
        if (code != FUSEDWASI_P2__OK) {
            if (total) break;
            return fusedwasi_p1__of_code(code);
        }
        if (n < len) break;
    }
    return fusedwasi_p1__put_u32(g, out, (uint32_t)total) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_pread(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nread) {
    return fusedwasi_p1__fd_at(g, fd, iovs, count, offset, nread, 0);
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_fd_pwrite(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint64_t offset, uint32_t nwritten) {
    return fusedwasi_p1__fd_at(g, fd, iovs, count, offset, nwritten, 1);
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_rename(fusedwasi_p2_guest *g, uint32_t fd, uint32_t old, uint32_t old_len, uint32_t new_fd, uint32_t new_, uint32_t new_len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *from, *to;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1__R(16), &from);  /* path_rename_source */
    if (rc) return rc;
    rc = fusedwasi_p1__dir_with(g, fds, new_fd, FUSEDWASI_P1__R(17), &to);  /* path_rename_target */
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, old, old_len) || !fusedwasi_p1__in_memory(g, new_, new_len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_rename_at(from, fusedwasi_p1__mem(g) + old, old_len, to, fusedwasi_p1__mem(g) + new_, new_len, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_link(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags, uint32_t old, uint32_t old_len, uint32_t new_fd, uint32_t new_, uint32_t new_len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *from, *to;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1__R(11), &from);  /* path_link_source */
    if (rc) return rc;
    rc = fusedwasi_p1__dir_with(g, fds, new_fd, FUSEDWASI_P1__R(12), &to);  /* path_link_target */
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, old, old_len) || !fusedwasi_p1__in_memory(g, new_, new_len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_link_at(from, flags & FUSEDWASI_P1_LOOKUP_FOLLOW ? FUSEDWASI_P2__SYMLINK_FOLLOW : 0, fusedwasi_p1__mem(g) + old, old_len, to,
                                                         fusedwasi_p1__mem(g) + new_, new_len, FUSEDWASI_P1__OPTS));
}

/* A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made. */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_symlink(fusedwasi_p2_guest *g, uint32_t target, uint32_t target_len, uint32_t fd, uint32_t path, uint32_t len) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1__R(24), &d);  /* path_symlink */
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, target, target_len)) return FUSEDWASI_P1_E_FAULT;
    if (target_len == 0) return FUSEDWASI_P1_E_NOENT;
    if (memchr(fusedwasi_p1__mem(g) + target, 0, target_len) != NULL) return FUSEDWASI_P1_E_INVAL;
    if (fusedwasi_p1__mem(g)[target] == '/') return FUSEDWASI_P1_E_NOTCAPABLE;
    char text[FUSEDWASI_P2__PATH_MAX];
    if (target_len >= sizeof text) return FUSEDWASI_P1_E_NAMETOOLONG;
    memcpy(text, fusedwasi_p1__mem(g) + target, target_len);
    text[target_len] = '\0';
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    return fusedwasi_p1__of_code(fusedwasi_p2__fs_symlink_at(d, text, fusedwasi_p1__mem(g) + path, len, FUSEDWASI_P1__OPTS));
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_path_readlink(fusedwasi_p2_guest *g, uint32_t fd, uint32_t path, uint32_t len, uint32_t buf, uint32_t buf_len, uint32_t used) {
    FUSEDWASI_P1__FDS(g);
    fusedwasi_p2__descriptor *d;
    uint32_t rc = fusedwasi_p1__dir_with(g, fds, fd, FUSEDWASI_P1__R(15), &d);  /* path_readlink */
    if (rc) return rc;
    if (!fusedwasi_p1__in_memory(g, buf, buf_len)) return FUSEDWASI_P1_E_FAULT;
    if (!fusedwasi_p1__in_memory(g, path, len)) return FUSEDWASI_P1_E_FAULT;
    char text[FUSEDWASI_P1__LINK_MAX + 2];  /* the whole target: 0.1 cuts it to the room it has */
    int code = fusedwasi_p2__fs_readlink_at(d, fusedwasi_p1__mem(g) + path, len, text, sizeof text, FUSEDWASI_P1__OPTS);
    if (code != FUSEDWASI_P2__OK) return fusedwasi_p1__of_code(code);  /* (a rooted target is not-permitted: PERM) */
    size_t n = strlen(text), room = buf_len < FUSEDWASI_P1__LINK_MAX ? buf_len : FUSEDWASI_P1__LINK_MAX;
    size_t shown = buf_len ? (n < room ? n : room) : 0;  /* a buffer of 0 bytes still says whether this is a link */
    memcpy(fusedwasi_p1__mem(g) + buf, text, shown);
    return fusedwasi_p1__put_u32(g, used, (uint32_t)shown) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
}

/* ---- poll_oneoff ----------------------------------------------------------------------------------------------------------------- */

/* A clock subscription is arithmetic on the clock of the state (and, when only clocks are waited for, the hook `sleep`). What a descriptor waits for is a pollable of 0.2: the readiness of
   the stream of a standard input that the state gave (`std[0]`), or, for the standard input of the process and for a file that is a pipe, a FIFO or a terminal, the system's own poll
   with no wait; the program waits between two looks with the `wait` of the guest. A regular file, a directory and a standard output are always ready. `legacy`: the first snapshot,
   whose clock subscription starts with an identifier. */

/* how long a clock subscription still has to wait (0: it is due), or an errno of WASI */
static inline uint32_t fusedwasi_p1__subscription_wait(fusedwasi_p2_guest *g, uint32_t sub, int legacy, uint64_t entry_now[2], int have[2], uint64_t *wait) {
    uint32_t shift = legacy ? 8 : 0;
    uint32_t id = 0, flags16 = 0;
    uint64_t timeout = 0;
    fusedwasi_p1__get_u32(g, sub + 16 + shift, &id);
    uint32_t lo = 0, hi = 0;
    fusedwasi_p1__get_u32(g, sub + 24 + shift, &lo);
    fusedwasi_p1__get_u32(g, sub + 28 + shift, &hi);
    timeout = ((uint64_t)hi << 32) | lo;
    flags16 = fusedwasi_p1__mem(g)[sub + 40 + shift] | ((uint32_t)fusedwasi_p1__mem(g)[sub + 41 + shift] << 8);
    if (id > 1) return FUSEDWASI_P1_E_INVAL;  /* the clocks of the process and the thread are not something to wait for */
    if (flags16 & ~1u) return FUSEDWASI_P1_E_INVAL;  /* subclockflags has one flag, abstime */
    if (!(flags16 & 1)) {
        *wait = timeout;
        return FUSEDWASI_P1_E_SUCCESS;
    }
    if (!have[id]) {
        uint32_t rc = fusedwasi_p1__clock_ns(g, id, &entry_now[id]);
        if (rc) return rc;
        have[id] = 1;
    }
    *wait = timeout > entry_now[id] ? timeout - entry_now[id] : 0;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* What a subscription to a descriptor waits for. `native_fd` >= 0: the system's poll on it; else `stream`: the stream of a state (a pollable of 0.2 on it); neither: it is answered at
   once (a bad descriptor, a right that is missing, a regular file, a directory, a standard output). `*error` is the error of the event (a right that is missing is NOTCAPABLE, as the
   descriptor is not one to wait for). */
typedef struct {
    int native_fd;
    fusedwasi_p2_stream *stream;
    short events, revents;
    int ready;
} fusedwasi_p1__waiter;

static inline int fusedwasi_p1__poll_kind(fusedwasi_p2_guest *g, fusedwasi_p1__fds *fds, uint32_t sub, uint8_t tag, uint32_t *error, fusedwasi_p1__waiter *w) {
    uint32_t number = 0;
    fusedwasi_p1__get_u32(g, sub + 16, &number);
    fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, number);
    *error = FUSEDWASI_P1_E_SUCCESS;
    memset(w, 0, sizeof *w);
    w->native_fd = -1;
    if (e == NULL) {
        *error = FUSEDWASI_P1_E_BADF;
        return 0;
    }
    uint64_t need = FUSEDWASI_P1_R_POLL_FD_READWRITE | (tag == 1 ? FUSEDWASI_P1_R_FD_READ : FUSEDWASI_P1_R_FD_WRITE);
    if (e->kind == FUSEDWASI_P1__K_STD) {  /* the rights of the standard streams are fixed and include polling */
#if FUSEDWASI_P1__NATIVE_POLL
        if (tag == 1 && e->handle == 0 && g->state->std[0] == NULL) {  /* the standard input of the process */
            w->native_fd = 0;
            w->events = POLLIN;
            return 1;
        }
#endif
        if (tag == 1 && e->handle == 0 && g->state->std[0] != NULL && g->state->std[0]->ready != NULL) {  /* a stream of the state that can say whether it is ready */
            w->stream = g->state->std[0];
            return 1;
        }
        return 0;
    }
    if ((e->rights & need) != need) {
        *error = FUSEDWASI_P1_E_NOTCAPABLE;
        return 0;
    }
#if FUSEDWASI_P1__NATIVE_POLL
    if (e->kind == FUSEDWASI_P1__K_FILE && e->filetype != FUSEDWASI_P1_FT_REGULAR) {
        fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
        if (d != NULL && d->fd >= 0) {
            w->native_fd = d->fd;
            w->events = tag == 1 ? POLLIN : POLLOUT;
            return 1;
        }
    }
#endif
    return 0;
}

/* asks the waiter once, without waiting */
static inline void fusedwasi_p1__waiter_ask(fusedwasi_p2_state *state, fusedwasi_p1__waiter *w) {
#if FUSEDWASI_P1__NATIVE_POLL
    if (w->native_fd >= 0) {
        struct pollfd p;
        p.fd = w->native_fd;
        p.events = w->events;
        p.revents = 0;
        int r;
        do r = poll(&p, 1, 0);
        while (r < 0 && errno == EINTR);
        w->revents = r > 0 ? p.revents : 0;
        w->ready = (w->revents & (POLLIN | POLLOUT | POLLHUP | POLLERR | POLLNVAL)) != 0;
        return;
    }
#endif
    fusedwasi_p2_pollable p;
    memset(&p, 0, sizeof p);
    p.ctx = w->stream;
    w->ready = w->stream != NULL && fusedwasi_p2__stream_pollable_ready(state, &p);
}

static inline uint32_t fusedwasi_p1__poll(fusedwasi_p2_guest *g, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents, int legacy) {
    FUSEDWASI_P1__FDS(g);
    uint32_t stride = legacy ? 56 : 48;
    if (count == 0) return FUSEDWASI_P1_E_INVAL;
    if (!fusedwasi_p1__in_memory(g, in, (uint64_t)count * stride) || !fusedwasi_p1__in_memory(g, out, (uint64_t)count * 32)) return FUSEDWASI_P1_E_FAULT;
    uint64_t entry_now[2] = {0, 0}, shortest = 0;
    int have[2] = {0, 0}, immediate = 0, timed = 0;  /* timed: a clock was subscribed, so `shortest` is a time (it may be UINT64_MAX: forever) */
    uint32_t waiting = 0;                             /* how many descriptors have to be waited for */
    fusedwasi_p1__waiter *waiters = (fusedwasi_p1__waiter *)calloc(count, sizeof *waiters);
    int *slot = (int *)malloc(count * sizeof *slot);
    if (waiters == NULL || slot == NULL) {
        free(waiters);
        free(slot);
        return FUSEDWASI_P1_E_NOMEM;
    }
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {  /* first, what is ready and how long the clocks wait */
        uint32_t sub = in + i * stride;
        uint8_t tag = fusedwasi_p1__mem(g)[sub + 8];
        slot[i] = -1;
        if (tag == 0) {
            uint64_t wait;
            uint32_t r = fusedwasi_p1__subscription_wait(g, sub, legacy, entry_now, have, &wait);
            if (r == FUSEDWASI_P1_E_SUCCESS && (!timed || wait < shortest)) {
                shortest = wait;
                timed = 1;
            }
            if (r == FUSEDWASI_P1_E_INVAL) {
                immediate = 1;
            } else if (r) {
                rc = r;
                goto done;
            }
        } else if (tag == 1 || tag == 2) {
            uint32_t error;
            if (fusedwasi_p1__poll_kind(g, fds, sub, tag, &error, &waiters[waiting])) slot[i] = (int)waiting++;
            else immediate = 1;
        } else {
            rc = FUSEDWASI_P1_E_INVAL;
            goto done;
        }
    }
    uint64_t waited = 0;
    if (waiting) {
        /* ask the pollables; when none is ready, wait (the `wait` of the guest) until one is or the clocks are due */
        uint64_t started = 0;
        int have_start = g->state->clock(g->state, 1, &started) == 0;
        int ready = 0;
        for (;;) {
            for (uint32_t k = 0; k < waiting; k++) {
                fusedwasi_p1__waiter_ask(g->state, &waiters[k]);
                if (waiters[k].ready) ready++;
            }
            if (ready || immediate) break;
            if (timed) {
                uint64_t spent = 0, now;
                if (have_start && g->state->clock(g->state, 1, &now) == 0) spent = now - started;
                if (spent >= shortest) break;
            }
            g->wait(g);
        }
        if (ready <= 0 && !immediate && timed) {
            waited = shortest;  /* the wait is over: the clocks are due */
        } else if (ready > 0 && have_start) {
            uint64_t now;
            if (g->state->clock(g->state, 1, &now) == 0) waited = now - started;
        }
    } else if (!immediate && timed && shortest > 0) {
        if (g->state->sleep(g->state, shortest) != 0) {
            rc = FUSEDWASI_P1_E_NOTSUP;
            goto done;
        }
        waited = shortest;
    }
    uint32_t made = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sub = in + i * stride, event = out + made * 32;
        uint8_t tag = fusedwasi_p1__mem(g)[sub + 8];
        uint32_t error = FUSEDWASI_P1_E_SUCCESS;
        uint64_t nbytes = 0;
        uint16_t flags = 0;
        if (tag == 0) {
            uint64_t wait;
            error = fusedwasi_p1__subscription_wait(g, sub, legacy, entry_now, have, &wait);
            if (error == FUSEDWASI_P1_E_SUCCESS && wait > waited) continue;  /* not yet */
        } else {
            uint32_t number = 0;
            fusedwasi_p1__get_u32(g, sub + 16, &number);
            fusedwasi_p1__entry *e = fusedwasi_p1__entry_of(fds, number);
            if (slot[i] >= 0) {  /* the descriptor was waited for */
                fusedwasi_p1__waiter *w = &waiters[slot[i]];
                if (!w->ready) continue;  /* not ready */
#if FUSEDWASI_P1__NATIVE_POLL
                if (w->native_fd >= 0) {
                    short r = w->revents;
                    if (r & POLLNVAL) error = FUSEDWASI_P1_E_BADF;
                    else if (r & (POLLHUP | POLLERR) && !(r & (POLLIN | POLLOUT))) flags = 1;  /* fd_readwrite_hangup */
                    if (tag == 1 && !(r & POLLNVAL)) {
                        int pending = 0;
                        if (ioctl(w->native_fd, FIONREAD, &pending) == 0 && pending > 0) nbytes = (uint64_t)pending;
                    }
                }
#endif
            } else {
                uint32_t answer;
                fusedwasi_p1__waiter w;
                if (!fusedwasi_p1__poll_kind(g, fds, sub, tag, &answer, &w) && answer != FUSEDWASI_P1_E_SUCCESS) {
                    error = answer; /* a bad descriptor or a right that is missing: told in the event */
                } else if (e != NULL && e->kind == FUSEDWASI_P1__K_FILE && tag == 1) {
                    fusedwasi_p2__descriptor *d = fusedwasi_p1__desc(g, e);
                    fusedwasi_p2__stat st;
                    if (d != NULL && fusedwasi_p2__fs_stat(d, &st) == FUSEDWASI_P2__OK && st.size > e->position) nbytes = st.size - e->position;
                }
            }
        }
        memset(fusedwasi_p1__mem(g) + event, 0, 32);
        memcpy(fusedwasi_p1__mem(g) + event, fusedwasi_p1__mem(g) + sub, 8);  /* userdata */
        fusedwasi_p1__put_u16(g, event + 8, (uint16_t)error);
        fusedwasi_p1__mem(g)[event + 10] = tag;
        if (tag != 0) {
            fusedwasi_p1__put_u64(g, event + 16, nbytes);
            fusedwasi_p1__put_u16(g, event + 24, flags);
        }
        made++;
    }
    rc = fusedwasi_p1__put_u32(g, nevents, made) ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_FAULT;
done:
    free(waiters);
    free(slot);
    return rc;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_poll_oneoff(fusedwasi_p2_guest *g, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents) { return fusedwasi_p1__poll(g, in, out, count, nevents, 0); }

/* ---- sockets: a program cannot make one, so there is none to speak of ------------------------------------------------------------ */

/* a descriptor is never a socket (a program cannot make one): the call says so, or that there is no such descriptor */
static inline uint32_t fusedwasi_p1__not_a_socket(fusedwasi_p2_guest *g, uint32_t fd) {
    FUSEDWASI_P1__FDS(g);
    return fusedwasi_p1__entry_of(fds, fd) == NULL ? FUSEDWASI_P1_E_BADF : FUSEDWASI_P1_E_NOTSOCK;
}

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_sock_accept(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags, uint32_t result) {
    (void)flags, (void)result;
    return fusedwasi_p1__not_a_socket(g, fd);
}
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_sock_recv(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t flags, uint32_t nread, uint32_t roflags) {
    (void)iovs, (void)count, (void)flags, (void)nread, (void)roflags;
    return fusedwasi_p1__not_a_socket(g, fd);
}
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_sock_send(fusedwasi_p2_guest *g, uint32_t fd, uint32_t iovs, uint32_t count, uint32_t flags, uint32_t nsent) {
    (void)iovs, (void)count, (void)flags, (void)nsent;
    return fusedwasi_p1__not_a_socket(g, fd);
}
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_sock_shutdown(fusedwasi_p2_guest *g, uint32_t fd, uint32_t how) {
    (void)how;
    return fusedwasi_p1__not_a_socket(g, fd);
}

#endif
