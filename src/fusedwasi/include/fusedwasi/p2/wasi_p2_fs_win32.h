/* The system calls of wasi_p2_filesystem.h on Windows, in Win32 and the C library (the twin of the Windows branch of p2/wasi_p2_filesystem.py, where it is described).
 * Included by that file. There is no openat: a directory is its path (UTF-8, with the \\?\ prefix so that the system changes no name), a file is a C-library descriptor with
 * its path, and the walk goes below the path by name; a link is a symbolic link or a junction. The check and the use are two steps, so another process that changes the
 * tree in between is not stopped. Link with nothing else (bcrypt is for the random bytes of the core).
 *
 * Host API (docs/public_api.md): no function of an import; no other name is public.
 * Every other name in this file is private: a double underscore after the prefix (`fusedwasi_p2__x`, `FUSEDWASI_P2__X`), and a private field ends with two underscores (`x__`). */
#ifndef FUSEDWASI_P2__FS_WIN32_H
#define FUSEDWASI_P2__FS_WIN32_H

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <winioctl.h>

typedef const char *fusedwasi_p2__dirref; /* the path of the directory */
#define FUSEDWASI_P2__DIRREF_OF(d) ((d)->path)

#define FUSEDWASI_P2__REPARSE_SYMLINK 0xA000000CL
#define FUSEDWASI_P2__REPARSE_MOUNT_POINT 0xA0000003L
#define FUSEDWASI_P2__SYMLINK_DIRECTORY 0x1
#define FUSEDWASI_P2__SYMLINK_ALLOW_UNPRIVILEGED 0x2
#define FUSEDWASI_P2__SHARE (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define FUSEDWASI_P2__EPOCH 116444736000000000ULL /* FILETIME (100 ns since 1601) of 1970 */

/* copy a string that is known to fit (the callers check the size first), and add one: without the functions the CRT of Microsoft warns about (C4996) */
FUSEDWASI_P2_FN void fusedwasi_p2__str_set(char *dst, const char *src) { memcpy(dst, src, strlen(src) + 1); }

/* out = a + b + c, or name-too-long when it does not fit in `cap` bytes with its NUL (a bounded copy whose result the compiler can see is checked) */
FUSEDWASI_P2_FN int fusedwasi_p2__cat3(char *out, size_t cap, const char *a, const char *b, const char *c) {
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    if (la + lb + lc >= cap) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    memcpy(out, a, la), memcpy(out + la, b, lb), memcpy(out + la + lb, c, lc);
    out[la + lb + lc] = '\0';
    return FUSEDWASI_P2__OK;
}
FUSEDWASI_P2_FN void fusedwasi_p2__str_add(char *dst, const char *src) { memcpy(dst + strlen(dst), src, strlen(src) + 1); }

/* UTF-8 and UTF-16 */

FUSEDWASI_P2_FN wchar_t *fusedwasi_p2__wide(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof *w);
    if (w && MultiByteToWideChar(CP_UTF8, 0, text, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

/* UTF-16 of `n` units to UTF-8 in `out` (NUL-terminated); returns 0 if it does not fit */
FUSEDWASI_P2_FN int fusedwasi_p2__utf8(const wchar_t *w, int n, char *out, size_t cap) {
    int k = WideCharToMultiByte(CP_UTF8, 0, w, n, out, (int)cap - 1, NULL, NULL);
    if (k <= 0 && n > 0) return 0;
    out[k] = '\0';
    return 1;
}

/* the error-code of a Win32 error */
FUSEDWASI_P2_FN int fusedwasi_p2__win_code(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE: case ERROR_BAD_NETPATH: case ERROR_BAD_NET_NAME: return FUSEDWASI_P2_E_NO_ENTRY;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: case ERROR_NETWORK_ACCESS_DENIED: return FUSEDWASI_P2_E_ACCESS;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return FUSEDWASI_P2_E_EXIST;
    case ERROR_DIR_NOT_EMPTY: return FUSEDWASI_P2_E_NOT_EMPTY;
    case ERROR_DIRECTORY: return FUSEDWASI_P2_E_NOT_DIRECTORY;
    case ERROR_NOT_SAME_DEVICE: return FUSEDWASI_P2_E_CROSS_DEVICE;
    case ERROR_PRIVILEGE_NOT_HELD: return FUSEDWASI_P2_E_NOT_PERMITTED;
    case ERROR_TOO_MANY_LINKS: return FUSEDWASI_P2_E_TOO_MANY_LINKS;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return FUSEDWASI_P2_E_INSUFFICIENT_SPACE;
    case ERROR_FILENAME_EXCED_RANGE: return FUSEDWASI_P2_E_NAME_TOO_LONG;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    case ERROR_INVALID_HANDLE: return FUSEDWASI_P2_E_BAD_DESCRIPTOR;
    case ERROR_WRITE_PROTECT: return FUSEDWASI_P2_E_READ_ONLY;
    case ERROR_CANT_RESOLVE_FILENAME: return FUSEDWASI_P2_E_LOOP;
    case ERROR_NOT_A_REPARSE_POINT: case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME: case ERROR_INVALID_PARAMETER: case ERROR_NO_UNICODE_TRANSLATION:
        return FUSEDWASI_P2_E_INVALID;
    case ERROR_BROKEN_PIPE: case ERROR_NO_DATA: return FUSEDWASI_P2_E_PIPE;
    default: return FUSEDWASI_P2_E_IO;
    }
}

FUSEDWASI_P2_FN int fusedwasi_p2__win_last(void) { return fusedwasi_p2__win_code(GetLastError()); }

FUSEDWASI_P2_FN int64_t fusedwasi_p2__ft_ns(FILETIME t) {
    uint64_t ticks = ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;
    return ticks > FUSEDWASI_P2__EPOCH ? (int64_t)((ticks - FUSEDWASI_P2__EPOCH) * 100u) : 0;
}

FUSEDWASI_P2_FN FILETIME fusedwasi_p2__ns_ft(int64_t ns) {
    uint64_t ticks = (uint64_t)(ns < 0 ? 0 : ns) / 100u + FUSEDWASI_P2__EPOCH;
    FILETIME t;
    t.dwLowDateTime = (DWORD)(ticks & 0xffffffffu);
    t.dwHighDateTime = (DWORD)(ticks >> 32);
    return t;
}

/* paths */

/* a name that is one name: no separator and no colon (a drive, or a stream of a file) */
FUSEDWASI_P2_FN int fusedwasi_p2__name_ok(const char *name) { return name[0] != '\0' && !strpbrk(name, "\\/:"); }

/* the place a name stands for: the directory itself for ".", with no trailing slash; a slash after something that is not a directory is `not-directory` */
FUSEDWASI_P2_FN int fusedwasi_p2__win_stat_path(const char *path, int follow, fusedwasi_p2__stat *out);

FUSEDWASI_P2_FN int fusedwasi_p2__target(const char *dir, const char *leaf, char *out, size_t cap) {
    size_t n = strlen(leaf);
    int slash = 0;
    while (n && leaf[n - 1] == '/') n--, slash = 1;
    if (n == 0 || (n == 1 && leaf[0] == '.')) {
        if (strlen(dir) >= cap) return FUSEDWASI_P2_E_NAME_TOO_LONG;
        fusedwasi_p2__str_set(out, dir);
    } else {
        char bare[FUSEDWASI_P2__NAME_MAX + 2];
        if (n > FUSEDWASI_P2__NAME_MAX) return FUSEDWASI_P2_E_NAME_TOO_LONG;
        memcpy(bare, leaf, n);
        bare[n] = '\0';
        if (!fusedwasi_p2__name_ok(bare)) return FUSEDWASI_P2_E_INVALID;
        int joined = fusedwasi_p2__cat3(out, cap, dir, "\\", bare);
        if (joined != FUSEDWASI_P2__OK) return joined;
    }
    if (slash) {
        fusedwasi_p2__stat st;
        memset(&st, 0, sizeof st);
        if (fusedwasi_p2__win_stat_path(out, 1, &st) == FUSEDWASI_P2__OK && st.type != FUSEDWASI_P2__T_DIRECTORY) return FUSEDWASI_P2_E_NOT_DIRECTORY;
    }
    return FUSEDWASI_P2__OK;
}

/* stat */

FUSEDWASI_P2_FN int fusedwasi_p2__win_stat_handle(HANDLE h, fusedwasi_p2__stat *out) {
    BY_HANDLE_FILE_INFORMATION bi;
    FILE_ATTRIBUTE_TAG_INFO ti;
    if (!GetFileInformationByHandle(h, &bi)) return fusedwasi_p2__win_last();
    ti.FileAttributes = 0, ti.ReparseTag = 0;
    GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof ti);
    if ((bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && ti.ReparseTag == FUSEDWASI_P2__REPARSE_SYMLINK) {
        out->type = FUSEDWASI_P2__T_SYMLINK;
    } else if (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        out->type = FUSEDWASI_P2__T_DIRECTORY;
    } else {
        switch (GetFileType(h)) {
        case FILE_TYPE_DISK: out->type = FUSEDWASI_P2__T_REGULAR; break;
        case FILE_TYPE_CHAR: out->type = FUSEDWASI_P2__T_CHAR; break;
        case FILE_TYPE_PIPE: out->type = FUSEDWASI_P2__T_FIFO; break;
        default: out->type = FUSEDWASI_P2__T_UNKNOWN; break;
        }
    }
    out->nlink = bi.nNumberOfLinks;
    out->size = ((uint64_t)bi.nFileSizeHigh << 32) | bi.nFileSizeLow;
    out->dev = bi.dwVolumeSerialNumber;
    out->ino = ((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
    out->atime_ns = fusedwasi_p2__ft_ns(bi.ftLastAccessTime);
    out->mtime_ns = fusedwasi_p2__ft_ns(bi.ftLastWriteTime);
    out->ctime_ns = fusedwasi_p2__ft_ns(bi.ftCreationTime);
    return FUSEDWASI_P2__OK;
}

FUSEDWASI_P2_FN HANDLE fusedwasi_p2__win_open(const char *path, DWORD access, int follow, int *rc) {
    wchar_t *w = fusedwasi_p2__wide(path);
    if (!w) {
        *rc = FUSEDWASI_P2_E_INVALID;
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateFileW(w, access, FUSEDWASI_P2__SHARE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
    if (h == INVALID_HANDLE_VALUE) *rc = fusedwasi_p2__win_last();
    free(w);
    return h;
}

FUSEDWASI_P2_FN int fusedwasi_p2__win_stat_path(const char *path, int follow, fusedwasi_p2__stat *out) {
    int rc = FUSEDWASI_P2__OK;
    HANDLE h = fusedwasi_p2__win_open(path, FILE_READ_ATTRIBUTES, follow, &rc);
    if (h == INVALID_HANDLE_VALUE) return rc;
    rc = fusedwasi_p2__win_stat_handle(h, out);
    CloseHandle(h);
    return rc;
}

/* a symbolic link or a junction */
FUSEDWASI_P2_FN int fusedwasi_p2__win_is_link(const char *path) {
    int rc = FUSEDWASI_P2__OK;
    HANDLE h = fusedwasi_p2__win_open(path, FILE_READ_ATTRIBUTES, 0, &rc);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILE_ATTRIBUTE_TAG_INFO ti;
    ti.FileAttributes = 0, ti.ReparseTag = 0;
    int link = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof ti) && (ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
               (ti.ReparseTag == FUSEDWASI_P2__REPARSE_SYMLINK || ti.ReparseTag == FUSEDWASI_P2__REPARSE_MOUNT_POINT);
    CloseHandle(h);
    return link;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_lstat(fusedwasi_p2__dirref dir, const char *leaf, fusedwasi_p2__stat *out) {
    char full[FUSEDWASI_P2__PATH_MAX];
    memset(out, 0, sizeof *out); /* as the POSIX one: the struct is whole whatever happens */
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    return rc != FUSEDWASI_P2__OK ? rc : fusedwasi_p2__win_stat_path(full, 0, out);
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_fstat(const fusedwasi_p2__descriptor *d, fusedwasi_p2__stat *out) {
    memset(out, 0, sizeof *out);
    if (d->fd >= 0) {
        HANDLE h = (HANDLE)_get_osfhandle(d->fd);
        return h == INVALID_HANDLE_VALUE ? FUSEDWASI_P2_E_BAD_DESCRIPTOR : fusedwasi_p2__win_stat_handle(h, out);
    }
    return fusedwasi_p2__win_stat_path(d->path, 1, out);
}

/* links */

typedef struct {
    ULONG tag;
    USHORT data_length, reserved;
    USHORT substitute_offset, substitute_length, print_offset, print_length;
    union {
        struct {
            ULONG flags;
            WCHAR path[1];
        } symlink;
        struct {
            WCHAR path[1];
        } mount;
    } u;
} fusedwasi_p2__reparse;

/* the target of a link as the Python twin shows it: '/' for separators, a leading '/' for a drive or a rooted target (which the callers refuse) */
FUSEDWASI_P2_FN int fusedwasi_p2__sys_readlink(fusedwasi_p2__dirref dir, const char *leaf, char *out, size_t cap) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    HANDLE h = fusedwasi_p2__win_open(full, FILE_READ_ATTRIBUTES, 0, &rc);
    if (h == INVALID_HANDLE_VALUE) return rc;
    union {
        fusedwasi_p2__reparse r;
        BYTE raw[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    } buf;
    DWORD got = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buf.raw, sizeof buf.raw, &got, NULL);
    CloseHandle(h);
    if (!ok) return fusedwasi_p2__win_last();
    const WCHAR *path;
    if (buf.r.tag == FUSEDWASI_P2__REPARSE_SYMLINK) path = buf.r.u.symlink.path;
    else if (buf.r.tag == FUSEDWASI_P2__REPARSE_MOUNT_POINT) path = buf.r.u.mount.path;
    else return FUSEDWASI_P2_E_INVALID;
    const WCHAR *name = path + buf.r.substitute_offset / sizeof(WCHAR);
    int n = buf.r.substitute_length / (int)sizeof(WCHAR);
    if (n >= 4 && name[0] == L'\\' && name[1] == L'?' && name[2] == L'?' && name[3] == L'\\') name += 4, n -= 4;
    char text[FUSEDWASI_P2__PATH_MAX];
    if (!fusedwasi_p2__utf8(name, n, text, sizeof text)) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    for (char *p = text; *p; p++)
        if (*p == '\\') *p = '/';
    int drive = text[0] && text[1] == ':' && ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'));
    return fusedwasi_p2__cat3(out, cap, drive ? "/" : "", text, "");
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_mkdir(fusedwasi_p2__dirref dir, const char *leaf) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    wchar_t *w = fusedwasi_p2__wide(full);
    if (!w) return FUSEDWASI_P2_E_INVALID;
    rc = CreateDirectoryW(w, NULL) ? FUSEDWASI_P2__OK : fusedwasi_p2__win_last();
    free(w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_rmdir(fusedwasi_p2__dirref dir, const char *leaf) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    wchar_t *w = fusedwasi_p2__wide(full);
    if (!w) return FUSEDWASI_P2_E_INVALID;
    rc = RemoveDirectoryW(w) ? FUSEDWASI_P2__OK : fusedwasi_p2__win_last();
    free(w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_unlink(fusedwasi_p2__dirref dir, const char *leaf) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    wchar_t *w = fusedwasi_p2__wide(full);
    if (!w) return FUSEDWASI_P2_E_INVALID;
    if (DeleteFileW(w)) {
        rc = FUSEDWASI_P2__OK;
    } else {
        DWORD e = GetLastError();
        rc = fusedwasi_p2__win_code(e);
        if (e == ERROR_ACCESS_DENIED) {
            DWORD attrs = GetFileAttributesW(w);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) && RemoveDirectoryW(w)) rc = FUSEDWASI_P2__OK; /* a link to a directory is removed as a file is */
                else if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) rc = FUSEDWASI_P2_E_IS_DIRECTORY;
            }
        }
    }
    free(w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_rename(fusedwasi_p2__dirref d1, const char *l1, fusedwasi_p2__dirref d2, const char *l2) {
    char a[FUSEDWASI_P2__PATH_MAX], b[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(d1, l1, a, sizeof a);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__target(d2, l2, b, sizeof b);
    if (rc != FUSEDWASI_P2__OK) return rc;
    wchar_t *wa = fusedwasi_p2__wide(a), *wb = fusedwasi_p2__wide(b);
    if (!wa || !wb) {
        rc = FUSEDWASI_P2_E_INVALID;
    } else {
        /* POSIX replaces an empty directory by a directory; Windows does not, so it is taken away first (it says not-empty if it is not) */
        DWORD from = GetFileAttributesW(wa), to = GetFileAttributesW(wb);
        int dirs = from != INVALID_FILE_ATTRIBUTES && to != INVALID_FILE_ATTRIBUTES && (from & FILE_ATTRIBUTE_DIRECTORY) && (to & FILE_ATTRIBUTE_DIRECTORY) &&
                   !(from & FILE_ATTRIBUTE_REPARSE_POINT) && !(to & FILE_ATTRIBUTE_REPARSE_POINT) && _wcsicmp(wa, wb) != 0;
        if (dirs && !RemoveDirectoryW(wb)) rc = fusedwasi_p2__win_last();
        else rc = MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING) ? FUSEDWASI_P2__OK : fusedwasi_p2__win_last();
    }
    free(wa), free(wb);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_link(fusedwasi_p2__dirref d1, const char *l1, fusedwasi_p2__dirref d2, const char *l2) {
    char a[FUSEDWASI_P2__PATH_MAX], b[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(d1, l1, a, sizeof a);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__target(d2, l2, b, sizeof b);
    if (rc != FUSEDWASI_P2__OK) return rc;
    wchar_t *wa = fusedwasi_p2__wide(a), *wb = fusedwasi_p2__wide(b);
    rc = wa && wb ? (CreateHardLinkW(wb, wa, NULL) ? FUSEDWASI_P2__OK : fusedwasi_p2__win_last()) : FUSEDWASI_P2_E_INVALID;
    free(wa), free(wb);
    return rc;
}

/* where `target` (with '\') leads, below `dir`, as a path without . and ..: to tell a link to a directory from a link to a file */
FUSEDWASI_P2_FN int fusedwasi_p2__target_is_dir(const char *dir, const char *target) {
    char joined[FUSEDWASI_P2__PATH_MAX], out[FUSEDWASI_P2__PATH_MAX];
    const char *base = strncmp(dir, "\\\\?\\", 4) == 0 ? dir + 4 : dir;
    if (fusedwasi_p2__cat3(joined, sizeof joined, base, "\\", target) != FUSEDWASI_P2__OK) return 0;
    size_t len = 0;
    char *save = NULL;
    (void)save;
    /* normalize: split at '\' and drop "." and ".." */
    const char *p = joined;
    size_t root = 0;
    if (p[0] && p[1] == ':') {
        out[0] = p[0], out[1] = ':', out[2] = '\0';
        len = root = 2;
        p += 2;
    }
    while (*p) {
        while (*p == '\\') p++;
        const char *e = p;
        while (*e && *e != '\\') e++;
        size_t n = (size_t)(e - p);
        if (n == 0) break;
        if (n == 1 && p[0] == '.') {
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (len > root && out[len - 1] != '\\') len--;
            if (len > root) len--;
            out[len] = '\0';
        } else if (len + 1 + n < sizeof out) {
            out[len++] = '\\';
            memcpy(out + len, p, n);
            len += n;
            out[len] = '\0';
        }
        p = e;
    }
    char full[FUSEDWASI_P2__PATH_MAX + 8];  /* the prefix and what is in `out` */
    snprintf(full, sizeof full, "\\\\?\\%s", out);
    wchar_t *w = fusedwasi_p2__wide(full);
    DWORD attrs = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_symlink(const char *target, fusedwasi_p2__dirref dir, const char *leaf) {
    char full[FUSEDWASI_P2__PATH_MAX], text[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    if (strlen(target) >= sizeof text) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    fusedwasi_p2__str_set(text, target);
    for (char *p = text; *p; p++)
        if (*p == '/') *p = '\\';
    wchar_t *wf = fusedwasi_p2__wide(full), *wt = fusedwasi_p2__wide(text);
    DWORD flags = FUSEDWASI_P2__SYMLINK_ALLOW_UNPRIVILEGED | (fusedwasi_p2__target_is_dir(dir, text) ? FUSEDWASI_P2__SYMLINK_DIRECTORY : 0);
    rc = FUSEDWASI_P2__OK;
    if (!wf || !wt) rc = FUSEDWASI_P2_E_INVALID;
    else if (!CreateSymbolicLinkW(wf, wt, flags)) {
        rc = fusedwasi_p2__win_last();
        if (GetFileAttributesW(wf) != INVALID_FILE_ATTRIBUTES) rc = FUSEDWASI_P2_E_EXIST; /* a name that is taken (a directory says access denied) */
    }
    free(wf), free(wt);
    return rc;
}

/* times */

FUSEDWASI_P2_FN int fusedwasi_p2__win_set_times_path(const char *path, int64_t atime_ns, int64_t mtime_ns, int follow) {
    int rc = FUSEDWASI_P2__OK;
    HANDLE h = fusedwasi_p2__win_open(path, FILE_WRITE_ATTRIBUTES, follow, &rc);
    if (h == INVALID_HANDLE_VALUE) return rc;
    FILETIME a = fusedwasi_p2__ns_ft(atime_ns), m = fusedwasi_p2__ns_ft(mtime_ns);
    rc = SetFileTime(h, NULL, &a, &m) ? FUSEDWASI_P2__OK : fusedwasi_p2__win_last();
    CloseHandle(h);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_set_times(fusedwasi_p2__dirref dir, const char *leaf, int64_t atime_ns, int64_t mtime_ns) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    return rc != FUSEDWASI_P2__OK ? rc : fusedwasi_p2__win_set_times_path(full, atime_ns, mtime_ns, 0); /* a link at the end is the one that is touched (the caller walked it if it was to be followed) */
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_set_times_fd(const fusedwasi_p2__descriptor *d, int64_t atime_ns, int64_t mtime_ns) {
    return fusedwasi_p2__win_set_times_path(d->path, atime_ns, mtime_ns, 1);
}

/* open */

FUSEDWASI_P2_FN int fusedwasi_p2__sys_open_at(fusedwasi_p2__dirref dir, const char *leaf, uint32_t open_flags, uint32_t flags, fusedwasi_p2__descriptor *out) {
    char full[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__target(dir, leaf, full, sizeof full);
    if (rc != FUSEDWASI_P2__OK) return rc;
    fusedwasi_p2__stat st;
    memset(&st, 0, sizeof st);
    int have = fusedwasi_p2__win_stat_path(full, 0, &st);
    if (have != FUSEDWASI_P2__OK && have != FUSEDWASI_P2_E_NO_ENTRY) return have;
    int exists = have == FUSEDWASI_P2__OK;
    size_t leaf_len = strlen(leaf);
    if (exists && (st.type == FUSEDWASI_P2__T_SYMLINK || fusedwasi_p2__win_is_link(full))) return FUSEDWASI_P2_E_LOOP; /* links are not followed here (O_NOFOLLOW) */
    if (exists && st.type == FUSEDWASI_P2__T_DIRECTORY) {
        if ((open_flags & FUSEDWASI_P2__CREATE) && (open_flags & FUSEDWASI_P2__EXCLUSIVE)) return FUSEDWASI_P2_E_EXIST;
        if ((flags & FUSEDWASI_P2__WRITE) || (open_flags & (FUSEDWASI_P2__CREATE | FUSEDWASI_P2__TRUNCATE))) return FUSEDWASI_P2_E_IS_DIRECTORY;
        out->fd = -1, out->is_dir = 1;
        out->path = (char *)malloc(strlen(full) + 1);
        if (!out->path) return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
        fusedwasi_p2__str_set(out->path, full);
        return FUSEDWASI_P2__OK;
    }
    if ((open_flags & FUSEDWASI_P2__DIRECTORY) || (leaf_len && leaf[leaf_len - 1] == '/')) return exists ? FUSEDWASI_P2_E_NOT_DIRECTORY : FUSEDWASI_P2_E_NO_ENTRY;
    int oflags = _O_BINARY;
    oflags |= (flags & FUSEDWASI_P2__READ) && (flags & FUSEDWASI_P2__WRITE) ? _O_RDWR : (flags & FUSEDWASI_P2__WRITE) ? _O_WRONLY : _O_RDONLY;
    if (open_flags & FUSEDWASI_P2__CREATE) oflags |= _O_CREAT;
    if (open_flags & FUSEDWASI_P2__EXCLUSIVE) oflags |= _O_EXCL;
    if (open_flags & FUSEDWASI_P2__TRUNCATE) oflags |= _O_TRUNC;
    wchar_t *w = fusedwasi_p2__wide(full);
    if (!w) return FUSEDWASI_P2_E_INVALID;
#ifdef _MSC_VER /* _wopen is deprecated there; _wsopen_s is what it calls with _SH_DENYNO */
    int fd = -1;
    if (_wsopen_s(&fd, w, oflags, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) fd = -1;
#else
    int fd = _wopen(w, oflags, _S_IREAD | _S_IWRITE);
#endif
    free(w);
    if (fd < 0) return fusedwasi_p2__fail();
    out->fd = fd, out->is_dir = 0;
    out->path = (char *)malloc(strlen(full) + 1);
    if (!out->path) {
        _close(fd);
        return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    }
    fusedwasi_p2__str_set(out->path, full);
    return FUSEDWASI_P2__OK;
}

/* the real path of a directory, as UTF-8 with the \\?\ prefix and no trailing separator */
FUSEDWASI_P2_FN int fusedwasi_p2__sys_open_directory(const char *path, fusedwasi_p2__descriptor *out) {
    int rc = FUSEDWASI_P2__OK;
    HANDLE h = fusedwasi_p2__win_open(path, FILE_READ_ATTRIBUTES, 1, &rc);
    if (h == INVALID_HANDLE_VALUE) return rc;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        rc = fusedwasi_p2__win_last();
        CloseHandle(h);
        return rc;
    }
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { /* O_DIRECTORY of the other systems */
        CloseHandle(h);
        return FUSEDWASI_P2_E_NOT_DIRECTORY;
    }
    wchar_t wide[FUSEDWASI_P2__PATH_MAX];
    DWORD n = GetFinalPathNameByHandleW(h, wide, FUSEDWASI_P2__PATH_MAX, VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= FUSEDWASI_P2__PATH_MAX) return n ? FUSEDWASI_P2_E_NAME_TOO_LONG : fusedwasi_p2__win_last();
    while (n > 3 && (wide[n - 1] == L'\\' || wide[n - 1] == L'/')) n--;
    char text[FUSEDWASI_P2__PATH_MAX];
    if (!fusedwasi_p2__utf8(wide, (int)n, text, sizeof text)) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    out->path = (char *)malloc(strlen(text) + 1);
    if (!out->path) return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    fusedwasi_p2__str_set(out->path, text);
    out->fd = -1, out->is_dir = 1;
    return FUSEDWASI_P2__OK;
}

FUSEDWASI_P2_FN void fusedwasi_p2__sys_close(fusedwasi_p2__descriptor *d) {
    if (d->fd >= 0) _close(d->fd);
    d->fd = -1;
    free(d->path);
    d->path = NULL;
}

/* file contents by offset */

FUSEDWASI_P2_FN int64_t fusedwasi_p2__sys_pread(int fd, void *buf, size_t n, uint64_t off, int *rc) {
    if (fd < 0) {
        *rc = FUSEDWASI_P2_E_IS_DIRECTORY;
        return -1;
    }
    if (_lseeki64(fd, (__int64)off, SEEK_SET) < 0) {
        *rc = fusedwasi_p2__fail();
        return -1;
    }
    int r = _read(fd, buf, (unsigned)(n > 0x40000000u ? 0x40000000u : n));
    if (r < 0) *rc = fusedwasi_p2__fail();
    return r;
}

FUSEDWASI_P2_FN int64_t fusedwasi_p2__sys_pwrite(int fd, const void *buf, size_t n, uint64_t off, int *rc) {
    if (fd < 0) {
        *rc = FUSEDWASI_P2_E_IS_DIRECTORY;
        return -1;
    }
    if (_lseeki64(fd, (__int64)off, SEEK_SET) < 0) {
        *rc = fusedwasi_p2__fail();
        return -1;
    }
    int r = _write(fd, buf, (unsigned)(n > 0x40000000u ? 0x40000000u : n));
    if (r < 0) *rc = fusedwasi_p2__fail();
    return r;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_size(int fd, uint64_t *size) {
    __int64 n = fd < 0 ? -1 : _filelengthi64(fd);
    if (n < 0) return fd < 0 ? FUSEDWASI_P2_E_BAD_DESCRIPTOR : fusedwasi_p2__fail();
    *size = (uint64_t)n;
    return FUSEDWASI_P2__OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2__sys_dup(int fd) { return _dup(fd); }
FUSEDWASI_P2_FN void fusedwasi_p2__sys_closefd(int fd) {
    if (fd >= 0) _close(fd);
}
FUSEDWASI_P2_FN int fusedwasi_p2__sys_ftruncate(int fd, uint64_t size) {
    if (fd < 0) return FUSEDWASI_P2_E_BAD_DESCRIPTOR;
    errno_t e = _chsize_s(fd, (__int64)size);
    return e ? fusedwasi_p2__errno_code((int)e) : FUSEDWASI_P2__OK;
}
FUSEDWASI_P2_FN int fusedwasi_p2__sys_fsync(const fusedwasi_p2__descriptor *d, int data_only) {
    (void)data_only;
    return d->fd < 0 || _commit(d->fd) == 0 ? FUSEDWASI_P2__OK : fusedwasi_p2__fail(); /* a directory on Windows has nothing to flush */
}

FUSEDWASI_P2_FN int fusedwasi_p2__name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

FUSEDWASI_P2_FN void fusedwasi_p2__names_free(char **names, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) free(names[i]);
    free(names);
}

/* the names of a directory, sorted, without . and .., with their types; the caller frees them. No memory for the list is FUSEDWASI_P2_E_INSUFFICIENT_MEMORY (nothing is kept). */
FUSEDWASI_P2_FN int fusedwasi_p2__sys_list(const fusedwasi_p2__descriptor *d, char ***names_out, uint8_t **types_out, uint32_t *count_out) {
    char pattern[FUSEDWASI_P2__PATH_MAX];
    if (strlen(d->path) + 3 >= sizeof pattern) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    snprintf(pattern, sizeof pattern, "%s\\*", d->path);
    wchar_t *w = fusedwasi_p2__wide(pattern);
    if (!w) return FUSEDWASI_P2_E_INVALID;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(w, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return fusedwasi_p2__win_last();
    uint32_t count = 0, capacity = 16;
    char **names = (char **)malloc(capacity * sizeof *names);
    if (!names) {
        FindClose(h);
        return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char text[FUSEDWASI_P2__NAME_MAX * 4 + 4];
        if (!fusedwasi_p2__utf8(fd.cFileName, (int)wcslen(fd.cFileName), text, sizeof text)) continue;
        if (count == capacity) {
            capacity *= 2;
            char **bigger = (char **)realloc(names, capacity * sizeof *names);
            if (!bigger) {
                fusedwasi_p2__names_free(names, count);
                FindClose(h);
                return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
            }
            names = bigger;
        }
        names[count] = (char *)malloc(strlen(text) + 1);
        if (!names[count]) {
            fusedwasi_p2__names_free(names, count);
            FindClose(h);
            return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
        }
        fusedwasi_p2__str_set(names[count++], text);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(names, count, sizeof *names, fusedwasi_p2__name_cmp);
    uint8_t *types = (uint8_t *)malloc(count ? count : 1);
    if (!types) {
        fusedwasi_p2__names_free(names, count);
        return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        fusedwasi_p2__stat st;
        if (fusedwasi_p2__sys_lstat(d->path, names[i], &st) != FUSEDWASI_P2__OK) { /* gone since the listing */
            free(names[i]);
            continue;
        }
        names[kept] = names[i];
        types[kept++] = st.type;
    }
    *names_out = names, *types_out = types, *count_out = kept;
    return FUSEDWASI_P2__OK;
}

/* the walk of a path, confined below a directory (see the Python twin): `dir` and `leaf` are the path of the directory that holds the last name and that name ("." for none) */
typedef struct {
    const char *dir; /* points into buf */
    char leaf[FUSEDWASI_P2__NAME_MAX + 2];
    char buf[FUSEDWASI_P2__PATH_MAX];
    size_t lens[FUSEDWASI_P2__MAX_DEPTH + 2]; /* the length of the path at each depth */
    int depth;
} fusedwasi_p2__walk;

FUSEDWASI_P2_FN void fusedwasi_p2__walk_close(fusedwasi_p2__walk *w) { w->depth = 0; }

/* `link_slash_not_dir`: a link with a slash after it, where links are not followed, is not a directory (WASI 0.1: uvwasi says so); else the walk follows it, below the base (Wasmtime) */
FUSEDWASI_P2_FN int fusedwasi_p2__walk_path_with(fusedwasi_p2__walk *w, const fusedwasi_p2__descriptor *base_d, const uint8_t *path, uint32_t len, int follow, int link_slash_not_dir) {
    char work[FUSEDWASI_P2__PATH_MAX], name[FUSEDWASI_P2__NAME_MAX + 2], target[FUSEDWASI_P2__PATH_MAX];
    int links = 0;
    w->depth = 0;
    w->dir = w->buf;
    if (strlen(base_d->path) >= sizeof w->buf) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    fusedwasi_p2__str_set(w->buf, base_d->path);
    w->lens[0] = strlen(w->buf);
    if (memchr(path, 0, len)) return FUSEDWASI_P2_E_INVALID;
    if (len == 0) return FUSEDWASI_P2_E_NO_ENTRY; /* an empty name is no name (POSIX; Wasmtime says the same) */
    if (path[0] == '/') return FUSEDWASI_P2_E_NOT_PERMITTED;
    if (len >= sizeof work) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    memcpy(work, path, len);
    work[len] = '\0';
    char *cur = work;
    for (;;) {
        while (*cur == '/') cur++;
        if (*cur == '\0') {
            fusedwasi_p2__str_set(w->leaf, ".");
            break;
        }
        char *end = strchr(cur, '/');
        size_t clen = end ? (size_t)(end - cur) : strlen(cur);
        char *rest = cur + clen;
        while (*rest == '/') rest++;
        int last = (*rest == '\0');
        int slash = last && end != NULL;
        if (clen > FUSEDWASI_P2__NAME_MAX) return FUSEDWASI_P2_E_NAME_TOO_LONG;
        memcpy(name, cur, clen);
        name[clen] = '\0';
        if (clen == 1 && name[0] == '.') {
            if (last) {
                fusedwasi_p2__str_set(w->leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        if (clen == 2 && name[0] == '.' && name[1] == '.') {
            if (w->depth == 0) return FUSEDWASI_P2_E_NOT_PERMITTED;
            w->depth--;
            w->buf[w->lens[w->depth]] = '\0';
            if (last) {
                fusedwasi_p2__str_set(w->leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        if (!fusedwasi_p2__name_ok(name)) return FUSEDWASI_P2_E_INVALID;
        if (last) {
            if (!follow) {
                /* a slash after a name makes a link there followed: the walk follows it itself, below the base */
                /* (the whole target: this readlink says "too long" and not a cut, as readlinkat does) */
                int is_link = slash && fusedwasi_p2__sys_readlink(w->buf, name, target, sizeof target) == FUSEDWASI_P2__OK;
                if (is_link && link_slash_not_dir) return FUSEDWASI_P2_E_NOT_DIRECTORY;
                if (!is_link) {
                    fusedwasi_p2__str_set(w->leaf, name);
                    if (slash) fusedwasi_p2__str_add(w->leaf, "/");
                    break;
                }
            }
        } else {
            if (w->depth > FUSEDWASI_P2__MAX_DEPTH) return FUSEDWASI_P2_E_NAME_TOO_LONG;
            char full[FUSEDWASI_P2__PATH_MAX];
            int rc = fusedwasi_p2__target(w->buf, name, full, sizeof full);
            if (rc != FUSEDWASI_P2__OK) return rc;
            fusedwasi_p2__stat st;
            rc = fusedwasi_p2__win_stat_path(full, 0, &st);
            if (rc != FUSEDWASI_P2__OK) return rc; /* it is not there, or cannot be seen */
            if (st.type == FUSEDWASI_P2__T_DIRECTORY && !fusedwasi_p2__win_is_link(full)) {
                size_t n = strlen(full);
                if (n >= sizeof w->buf) return FUSEDWASI_P2_E_NAME_TOO_LONG;
                memcpy(w->buf, full, n + 1);
                w->lens[++w->depth] = n;
                cur = rest;
                continue;
            }
            /* not a directory we can enter: perhaps a link, which is read below */
        }
        int rc = fusedwasi_p2__sys_readlink(w->buf, name, target, sizeof target);
        if (rc != FUSEDWASI_P2__OK) {
            if (last) { /* not a link: this is the last name */
                fusedwasi_p2__str_set(w->leaf, name);
                if (slash) fusedwasi_p2__str_add(w->leaf, "/");
                break;
            }
            return FUSEDWASI_P2_E_NOT_DIRECTORY;
        }
        size_t n = strlen(target);
        if (++links > FUSEDWASI_P2__MAX_LINKS) return FUSEDWASI_P2_E_LOOP;
        if (target[0] == '/') return FUSEDWASI_P2_E_NOT_PERMITTED;
        size_t rest_len = strlen(rest);
        if (n + 1 + rest_len >= sizeof work) return FUSEDWASI_P2_E_NAME_TOO_LONG;
        memmove(work + n + 1, rest, rest_len + 1); /* rest may lie inside work */
        memcpy(work, target, n);
        work[n] = '/';
        if (rest_len == 0 && !slash) work[n] = '\0';
        cur = work;
    }
    return FUSEDWASI_P2__OK;
}

#endif /* FUSEDWASI_P2__FS_WIN32_H */
