/* The file backend of the core of WASI for Windows, in Win32 and nothing else (no Python, no POSIX layer): the twelve `fusedwasi_p1_be_*`
   functions that wasi_p1_core.h declares, written for the documented wide-character API (CreateFileW, GetFileInformationByHandle,
   FindFirstFileExW, GetFinalPathNameByHandleW, MoveFileExW, CreateSymbolicLinkW and the like). It is included by wasi_p1_core.h on
   Windows. Item 9 and stage 3a of the plan in DESIGN.md.

   Windows has no `openat` and cannot open a directory with `open`, so a directory is what it is in the Python backend: a path (`root`,
   the real path of the directory it is under, and `rel`, its place below it, with '/' between names), and a file is an
   open C-library descriptor (so that read, write, seek, truncate and sync of the core are the same as before) with its path kept in `root`.
   A name is resolved by hand below the directory (no "..", no absolute name), and a link that points out of the directory is
   refused by comparing the final paths of what is used and of the directory. The check and the use are two steps, so another
   process that changes the directory in between is not stopped: that is what this backend cannot do and `openat` can (the `nt` variant
   of stage 3b, with `NtCreateFile` relative to a handle, closes it).

   Paths go to Win32 as UTF-16 with the \\?\ prefix, so they are not limited to 260 characters and not changed by the system. */
#ifndef FUSEDWASI_P1_FILES_WIN32_H
#define FUSEDWASI_P1_FILES_WIN32_H

#include <winioctl.h>
#include <wchar.h>
#define FUSEDWASI_P1_SYMLINK_ALLOW_UNPRIVILEGED 0x2  /* Windows 10 1703 with the developer mode; older systems refuse the flag */

/* ---- from Win32 to WASI ------------------------------------------------------------------------------------------------ */

static inline uint32_t fusedwasi_p1_w_errno(DWORD e) {
    switch (e) {
    case ERROR_SUCCESS: return FUSEDWASI_P1_E_SUCCESS;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME: return FUSEDWASI_P1_E_NOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_NETWORK_ACCESS_DENIED: return FUSEDWASI_P1_E_ACCES;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: return FUSEDWASI_P1_E_EXIST;
    case ERROR_DIR_NOT_EMPTY: return FUSEDWASI_P1_E_NOTEMPTY;
    case ERROR_DIRECTORY: return FUSEDWASI_P1_E_NOTDIR;
    case ERROR_NOT_SAME_DEVICE: return FUSEDWASI_P1_E_XDEV;
    case ERROR_PRIVILEGE_NOT_HELD: return FUSEDWASI_P1_E_PERM;
    case ERROR_TOO_MANY_LINKS: return FUSEDWASI_P1_E_MLINK;
    case ERROR_TOO_MANY_OPEN_FILES: return FUSEDWASI_P1_E_MFILE;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: return FUSEDWASI_P1_E_NOSPC;
    case ERROR_FILENAME_EXCED_RANGE: return FUSEDWASI_P1_E_NAMETOOLONG;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return FUSEDWASI_P1_E_NOMEM;
    case ERROR_INVALID_HANDLE: return FUSEDWASI_P1_E_BADF;
    case ERROR_WRITE_PROTECT: return FUSEDWASI_P1_E_ROFS;
    case ERROR_CANT_RESOLVE_FILENAME: return FUSEDWASI_P1_E_LOOP;
    case ERROR_NOT_A_REPARSE_POINT:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_NO_UNICODE_TRANSLATION: return FUSEDWASI_P1_E_INVAL;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA: return FUSEDWASI_P1_E_PIPE;
    default: return FUSEDWASI_P1_E_IO;
    }
}

static inline uint32_t fusedwasi_p1_w_last(void) { return fusedwasi_p1_w_errno(GetLastError()); }

/* FILETIME (100 ns since 1601) and nanoseconds since 1970 */
#define FUSEDWASI_P1_W_EPOCH 116444736000000000ull

static inline uint64_t fusedwasi_p1_w_ns(uint64_t ticks) { return ticks > FUSEDWASI_P1_W_EPOCH ? (ticks - FUSEDWASI_P1_W_EPOCH) * 100u : 0; }

static inline uint64_t fusedwasi_p1_w_ns_of(FILETIME t) { return fusedwasi_p1_w_ns(((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime); }

static inline FILETIME fusedwasi_p1_w_filetime(uint64_t ns) {
    uint64_t ticks = ns / 100u + FUSEDWASI_P1_W_EPOCH;
    FILETIME t;
    t.dwLowDateTime = (DWORD)(ticks & 0xffffffffu);
    t.dwHighDateTime = (DWORD)(ticks >> 32);
    return t;
}

/* ---- paths ------------------------------------------------------------------------------------------------------------- */

/* UTF-8 to UTF-16, new memory (NUL ended); NULL with the last error set */
static inline wchar_t *fusedwasi_p1_w_wide(const char *text, size_t len) {
    int n = len ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)len, NULL, 0) : 0;
    if (len && n <= 0) {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return NULL;
    }
    wchar_t *out = (wchar_t *)FUSEDWASI_P1_MALLOC(((size_t)n + 1) * sizeof(wchar_t));
    if (out == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)len, out, n);
    out[n] = L'\0';
    return out;
}

/* UTF-16 to UTF-8, new memory */
static inline char *fusedwasi_p1_w_narrow(const wchar_t *text, size_t len) {
    int n = len ? WideCharToMultiByte(CP_UTF8, 0, text, (int)len, NULL, 0, NULL, NULL) : 0;
    char *out = (char *)FUSEDWASI_P1_MALLOC((size_t)n + 1);
    if (out == NULL) return NULL;
    if (n) WideCharToMultiByte(CP_UTF8, 0, text, (int)len, out, n, NULL, NULL);
    out[n] = '\0';
    return out;
}

/* A path of the host (UTF-8, '/' or '\\' between names, no "." or "..") as Win32 wants it: backslashes, no slash at the end, and the
   prefix that turns the system's own interpretation off (\\?\C:\... , \\?\UNC\server\share\...). New memory; NULL with the error set. */
static inline wchar_t *fusedwasi_p1_w_path(const char *path) {
    wchar_t *w = fusedwasi_p1_w_wide(path, strlen(path));
    if (w == NULL) return NULL;
    size_t n = wcslen(w);
    for (size_t i = 0; i < n; i++)
        if (w[i] == L'/') w[i] = L'\\';
    const wchar_t *prefix = L"";
    size_t skip = 0;
    if (n >= 4 && w[0] == L'\\' && w[1] == L'\\' && w[2] == L'?' && w[3] == L'\\') {
        prefix = L"";  /* given so already */
    } else if (n >= 2 && w[0] == L'\\' && w[1] == L'\\') {
        prefix = L"\\\\?\\UNC\\";
        skip = 2;
    } else if (n >= 2 && w[1] == L':') {
        prefix = L"\\\\?\\";
    }
    size_t pl = wcslen(prefix);
    wchar_t *out = (wchar_t *)FUSEDWASI_P1_MALLOC((pl + n - skip + 1) * sizeof(wchar_t));
    if (out == NULL) {
        FUSEDWASI_P1_FREE(w);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    wmemcpy(out, prefix, pl);
    wmemcpy(out + pl, w + skip, n - skip + 1);
    FUSEDWASI_P1_FREE(w);
    size_t total = pl + n - skip;
    /* no slash at the end, except of the root of a drive ("\\?\C:\") */
    while (total > 0 && out[total - 1] == L'\\' && !(total == 7 && out[2] == L'?' && out[5] == L':') && !(total == 3 && out[1] == L':'))
        out[--total] = L'\0';
    return out;
}

/* the path of an object that exists, as the system itself names it (links followed, the case it has on disk); new memory */
static inline wchar_t *fusedwasi_p1_w_final(const wchar_t *path) {
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD cap = 512, got;
    wchar_t *buf = NULL;
    for (;;) {
        wchar_t *bigger = (wchar_t *)FUSEDWASI_P1_REALLOC(buf, (size_t)cap * sizeof(wchar_t));
        if (bigger == NULL) {
            FUSEDWASI_P1_FREE(buf);
            CloseHandle(h);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        buf = bigger;
        got = GetFinalPathNameByHandleW(h, buf, cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (got == 0) {
            DWORD error = GetLastError();
            FUSEDWASI_P1_FREE(buf);
            CloseHandle(h);
            SetLastError(error);
            return NULL;
        }
        if (got < cap) break;
        cap = got + 1;
    }
    CloseHandle(h);
    return buf;
}

/* whether `real` (from fusedwasi_p1_w_final) is `root` or below it; Windows does not tell the case of letters apart */
static inline int fusedwasi_p1_w_inside(const wchar_t *root, const wchar_t *real) {
    size_t la = wcslen(root), lb = wcslen(real);
    while (la > 0 && root[la - 1] == L'\\') la--;
    if (lb < la) return 0;
    if (CompareStringOrdinal(root, (int)la, real, (int)la, TRUE) != CSTR_EQUAL) return 0;
    return lb == la || real[la] == L'\\';
}

/* ---- what an object is -------------------------------------------------------------------------------------------------- */

static inline HANDLE fusedwasi_p1_w_handle(const wchar_t *path, DWORD access, int follow) {
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
}

static inline uint32_t fusedwasi_p1_w_stat_handle(HANDLE h, fusedwasi_p1_stat *out) {
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO basic;
    FILE_ATTRIBUTE_TAG_INFO tag;
    memset(out, 0, sizeof *out);
    if (GetFileType(h) == FILE_TYPE_CHAR) {
        out->filetype = FUSEDWASI_P1_FT_CHAR;
        out->nlink = 1;
        return FUSEDWASI_P1_E_SUCCESS;
    }
    if (!GetFileInformationByHandle(h, &info)) return fusedwasi_p1_w_last();
    out->dev = info.dwVolumeSerialNumber;
    out->ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    out->nlink = info.nNumberOfLinks;
    out->size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    out->atim = fusedwasi_p1_w_ns_of(info.ftLastAccessTime);
    out->mtim = fusedwasi_p1_w_ns_of(info.ftLastWriteTime);
    out->ctim = GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) ? fusedwasi_p1_w_ns((uint64_t)basic.ChangeTime.QuadPart)
                                                                                    : fusedwasi_p1_w_ns_of(info.ftCreationTime);
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof tag) && tag.ReparseTag == IO_REPARSE_TAG_SYMLINK)
        out->filetype = FUSEDWASI_P1_FT_SYMLINK;
    else
        out->filetype = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FUSEDWASI_P1_FT_DIR : FUSEDWASI_P1_FT_REGULAR;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the attributes of what a path names (`follow`: of what a link leads to); `full` is UTF-8 */
static inline uint32_t fusedwasi_p1_w_stat(const char *full, int follow, fusedwasi_p1_stat *out) {
    wchar_t *wp = fusedwasi_p1_w_path(full);
    if (wp == NULL) return fusedwasi_p1_w_last();
    HANDLE h = fusedwasi_p1_w_handle(wp, FILE_READ_ATTRIBUTES, follow);
    FUSEDWASI_P1_FREE(wp);
    if (h == INVALID_HANDLE_VALUE) return fusedwasi_p1_w_last();
    uint32_t rc = fusedwasi_p1_w_stat_handle(h, out);
    CloseHandle(h);
    return rc;
}

/* ---- the path the program gave, below a directory ---------------------------------------------------------------------- */

/* The last name of a path below `root` taken off ('/' between names); false when only the root is left */
static inline int fusedwasi_p1_w_parent(char *path, size_t root_len) {
    size_t n = strlen(path);
    if (n <= root_len) return 0;
    while (n > root_len && path[n - 1] != '/') n--;
    if (n > root_len) n--;
    path[n < root_len ? root_len : n] = '\0';
    return 1;
}

/* whether what `path` names is inside the directory `root`, links followed (`follow`: the last name too); a name that does not exist yet is
   judged by the directory that holds it; a link that leads nowhere is not followed to a place to be made, since where it would lead is not known (open() refuses it) */
static inline int fusedwasi_p1_w_contained(const char *root, const char *path, int follow) {
    wchar_t *wroot = fusedwasi_p1_w_path(root), *froot = wroot ? fusedwasi_p1_w_final(wroot) : NULL;
    FUSEDWASI_P1_FREE(wroot);
    if (froot == NULL) return 0;
    char *check = fusedwasi_p1_copy(path, strlen(path));
    size_t root_len = strlen(root);
    int inside = 0;
    if (check != NULL) {
        if (!follow) fusedwasi_p1_w_parent(check, root_len);
        for (;;) {
            wchar_t *wp = fusedwasi_p1_w_path(check), *real = wp ? fusedwasi_p1_w_final(wp) : NULL;
            FUSEDWASI_P1_FREE(wp);
            if (real != NULL) {
                inside = fusedwasi_p1_w_inside(froot, real);
                FUSEDWASI_P1_FREE(real);
                break;
            }
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) break;  /* refused */
            fusedwasi_p1_stat info;
            if (fusedwasi_p1_w_stat(check, 0, &info) == FUSEDWASI_P1_E_SUCCESS && info.filetype == FUSEDWASI_P1_FT_SYMLINK) {
                inside = 1;  /* a link that leads nowhere leads nowhere out: what follows it fails, and open() will not create through it */
                break;
            }
            if (!fusedwasi_p1_w_parent(check, root_len)) {
                inside = 1;
                break;
            }
        }
    }
    FUSEDWASI_P1_FREE(check);
    FUSEDWASI_P1_FREE(froot);
    return inside;
}

#define FUSEDWASI_P1_W_NAME_MAX 255

/* The path the program gave, below the directory `dir`: its place in the real file system. 0, with `*full` and `*newrel` in new
   memory (the caller frees them), or an errno of WASI: a name that leaves the directory ("..", an absolute name, a symbolic link
   that points out) is NOTCAPABLE. `*slash` is whether the name ended with a slash, which asks for a directory. */
static inline uint32_t fusedwasi_p1_w_resolve(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t ptr, uint32_t len, int follow, char **full,
                                      char **newrel, int *slash) {
    *full = NULL;
    *newrel = NULL;
    *slash = 0;
    if (dir->kind != FUSEDWASI_P1_K_DIR) return FUSEDWASI_P1_E_NOTDIR;
    if (!fusedwasi_p1_in_memory(w, ptr, len)) return FUSEDWASI_P1_E_FAULT;
    const unsigned char *given = FUSEDWASI_P1_MEM(w) + ptr;
    if (len == 0) return FUSEDWASI_P1_E_NOENT;
    if (memchr(given, 0, len) != NULL) return FUSEDWASI_P1_E_INVAL;
    if (given[0] == '/' || given[0] == '\\') return FUSEDWASI_P1_E_NOTCAPABLE;
    if (len >= 2 && given[1] == ':') return FUSEDWASI_P1_E_NOTCAPABLE;  /* a drive: not a name below a directory */
    size_t rel_len = strlen(dir->rel);
    char *joined = (char *)FUSEDWASI_P1_MALLOC(rel_len + 1 + len + 1);
    char *out = (char *)FUSEDWASI_P1_MALLOC(rel_len + 1 + len + 1);
    if (joined == NULL || out == NULL) {
        FUSEDWASI_P1_FREE(joined);
        FUSEDWASI_P1_FREE(out);
        return FUSEDWASI_P1_E_NOMEM;
    }
    memcpy(joined, dir->rel, rel_len);
    joined[rel_len] = '/';
    memcpy(joined + rel_len + 1, given, len);
    joined[rel_len + 1 + len] = '\0';
    for (char *c = joined; *c; c++) {
        if (*c == '\\') *c = '/';  /* a backslash is a separator for Windows; the program means a name that has none */
    }
    size_t n = 0;
    char *cursor = joined;
    int outside = 0;
    for (;;) {
        char *end = strchr(cursor, '/');
        size_t tlen = end ? (size_t)(end - cursor) : strlen(cursor);
        if (tlen == 2 && cursor[0] == '.' && cursor[1] == '.') {
            if (n == 0) {
                outside = 1;
                break;
            }
            while (n > 0 && out[n - 1] != '/') n--;
            if (n > 0) n--;
        } else if (tlen > 0 && !(tlen == 1 && cursor[0] == '.')) {
            if (tlen > FUSEDWASI_P1_W_NAME_MAX) {  /* a name of a directory is at most 255 bytes, as on POSIX (the system's own error would come too late: it is NOTCAPABLE for a name that does not exist) */
                FUSEDWASI_P1_FREE(joined);
                FUSEDWASI_P1_FREE(out);
                return FUSEDWASI_P1_E_NAMETOOLONG;
            }
            if (n) out[n++] = '/';
            memcpy(out + n, cursor, tlen);
            n += tlen;
        }
        if (end == NULL) break;
        cursor = end + 1;
    }
    FUSEDWASI_P1_FREE(joined);
    if (outside) {
        FUSEDWASI_P1_FREE(out);
        return FUSEDWASI_P1_E_NOTCAPABLE;
    }
    out[n] = '\0';
    char *path = fusedwasi_p1_join(dir->root, out);
    if (path == NULL) {
        FUSEDWASI_P1_FREE(out);
        return FUSEDWASI_P1_E_NOMEM;
    }
    if (n > 0 && !fusedwasi_p1_w_contained(dir->root, path, follow)) {
        FUSEDWASI_P1_FREE(out);
        FUSEDWASI_P1_FREE(path);
        return FUSEDWASI_P1_E_NOTCAPABLE;
    }
    *slash = n > 0 && given[len - 1] == '/';
    *full = path;
    *newrel = out;
    return FUSEDWASI_P1_E_SUCCESS;
}

/* A name below `dir` for a call that changes the directory: its path, to free. NULL, with `*rc` the errno, for what cannot be the
   object of the call (the directory itself). */
static inline char *fusedwasi_p1_w_name(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint32_t *rc, int *slash) {
    char *full, *rel;
    *rc = fusedwasi_p1_w_resolve(w, dir, path, len, follow, &full, &rel, slash);
    if (*rc) return NULL;
    int itself = rel[0] == '\0';
    FUSEDWASI_P1_FREE(rel);
    if (itself) {
        FUSEDWASI_P1_FREE(full);
        *rc = FUSEDWASI_P1_E_INVAL;
        return NULL;
    }
    return full;
}

/* ---- the file backend ----------------------------------------------------------------------------------------------------- */

static inline uint32_t fusedwasi_p1_be_stat_fd(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, fusedwasi_p1_stat *out, int *trapped) {
    (void)w;
    (void)trapped;
    if (e->kind == FUSEDWASI_P1_K_FILE) {
        HANDLE h = (HANDLE)_get_osfhandle(e->osfd);
        if (h == INVALID_HANDLE_VALUE) return FUSEDWASI_P1_E_BADF;
        return fusedwasi_p1_w_stat_handle(h, out);
    }
    char *path = fusedwasi_p1_join(e->root, e->rel);
    if (path == NULL) return FUSEDWASI_P1_E_NOMEM;
    uint32_t rc = fusedwasi_p1_w_stat(path, 1, out);
    FUSEDWASI_P1_FREE(path);
    return rc;
}

static inline uint32_t fusedwasi_p1_be_stat_path(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, fusedwasi_p1_stat *out,
                                         int *trapped) {
    (void)trapped;
    char *full, *rel;
    int slash;
    uint32_t rc = fusedwasi_p1_w_resolve(w, dir, path, len, follow, &full, &rel, &slash);
    if (rc) return rc;
    FUSEDWASI_P1_FREE(rel);
    rc = fusedwasi_p1_w_stat(full, follow, out);
    FUSEDWASI_P1_FREE(full);
    if (rc == FUSEDWASI_P1_E_SUCCESS && slash && out->filetype != FUSEDWASI_P1_FT_DIR) return FUSEDWASI_P1_E_NOTDIR;  /* a name with a slash after it is a directory */
    return rc;
}

/* opens what the path names, into `e` (a new entry): a file (a descriptor of the C library) or a directory (a path) */
static inline uint32_t fusedwasi_p1_be_open(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint32_t oflags,
                                    uint64_t base, uint32_t fdflags, fusedwasi_p1_entry *e, int *trapped) {
    (void)trapped;
    char *full, *rel;
    int slash;
    uint32_t rc = fusedwasi_p1_w_resolve(w, dir, path, len, follow, &full, &rel, &slash);
    if (rc) return rc;
    FUSEDWASI_P1_FREE(rel);
    fusedwasi_p1_stat info;
    int exists = 1;
    rc = fusedwasi_p1_w_stat(full, follow, &info);
    if (rc == FUSEDWASI_P1_E_NOENT && (oflags & FUSEDWASI_P1_O_CREAT)) {
        fusedwasi_p1_stat link;
        if (follow && fusedwasi_p1_w_stat(full, 0, &link) == FUSEDWASI_P1_E_SUCCESS && link.filetype == FUSEDWASI_P1_FT_SYMLINK) {
            FUSEDWASI_P1_FREE(full);  /* a file would be made where the link leads, which is not known to be inside the directory */
            return FUSEDWASI_P1_E_NOTCAPABLE;
        }
        exists = 0;
        rc = FUSEDWASI_P1_E_SUCCESS;
    }
    if (rc) {
        FUSEDWASI_P1_FREE(full);
        return rc;
    }
    int flags = fusedwasi_p1_open_flags(base, oflags, fdflags);
    if (exists && !follow && info.filetype == FUSEDWASI_P1_FT_SYMLINK) {  /* what O_NOFOLLOW says */
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_LOOP;
    }
    if (exists && (oflags & FUSEDWASI_P1_O_CREAT) && (oflags & FUSEDWASI_P1_O_EXCL)) {
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_EXIST;
    }
    if (slash && exists && info.filetype != FUSEDWASI_P1_FT_DIR) {
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_NOTDIR;
    }
    if (slash && !exists) {  /* a file cannot be made with a slash after its name */
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_ISDIR;
    }
    if (exists && info.filetype == FUSEDWASI_P1_FT_DIR) {  /* a directory is its own root: it cannot reach what is above it, as with openat */
        int as_directory = (base & FUSEDWASI_P1_R_OF_DIRECTORY) != 0 && !(oflags & (FUSEDWASI_P1_O_CREAT | FUSEDWASI_P1_O_TRUNC));
        if (!as_directory && ((flags & (O_WRONLY | O_RDWR | O_TRUNC)) != 0 || (oflags & FUSEDWASI_P1_O_CREAT))) {
            FUSEDWASI_P1_FREE(full);
            return FUSEDWASI_P1_E_ISDIR;
        }
        e->kind = FUSEDWASI_P1_K_DIR;
        e->filetype = FUSEDWASI_P1_FT_DIR;
        e->root = full;
        e->rel = fusedwasi_p1_copy("", 0);
        return e->rel == NULL ? FUSEDWASI_P1_E_NOMEM : FUSEDWASI_P1_E_SUCCESS;
    }
    if (oflags & FUSEDWASI_P1_O_DIRECTORY) {
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_NOTDIR;
    }
    DWORD access = (flags & 3) == O_RDWR ? (GENERIC_READ | GENERIC_WRITE) : (flags & 3) == O_WRONLY ? GENERIC_WRITE : GENERIC_READ;
    DWORD disposition;
    if (flags & O_CREAT) disposition = (flags & O_EXCL) ? CREATE_NEW : (flags & O_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS;
    else disposition = (flags & O_TRUNC) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    wchar_t *wp = fusedwasi_p1_w_path(full);
    if (wp == NULL) {
        rc = fusedwasi_p1_w_last();
        FUSEDWASI_P1_FREE(full);
        return rc;
    }
    /* the same sharing as the C library gives (read and write, not delete), so that a file that is open cannot be taken from under it */
    HANDLE h = CreateFileW(wp, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    FUSEDWASI_P1_FREE(wp);
    if (h == INVALID_HANDLE_VALUE) {
        rc = fusedwasi_p1_w_last();
        FUSEDWASI_P1_FREE(full);
        return rc;
    }
    int fd = _open_osfhandle((intptr_t)h, _O_BINARY | ((flags & 3) == O_RDONLY ? _O_RDONLY : 0));
    if (fd < 0) {
        CloseHandle(h);
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_MFILE;
    }
    e->root = full;  /* the path stays with the file: the times of an open file are set through it when the handle may not */
    e->kind = FUSEDWASI_P1_K_FILE;
    e->osfd = fd;
    e->owns_fd = 1;
    e->filetype = exists ? info.filetype : FUSEDWASI_P1_FT_REGULAR;
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_mkdir(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int *trapped) {
    (void)trapped;
    uint32_t rc;
    int slash;
    char *full = fusedwasi_p1_w_name(w, dir, path, len, 0, &rc, &slash);
    if (full == NULL) return rc == FUSEDWASI_P1_E_INVAL ? FUSEDWASI_P1_E_EXIST : rc;
    wchar_t *wp = fusedwasi_p1_w_path(full);
    FUSEDWASI_P1_FREE(full);
    if (wp == NULL) return fusedwasi_p1_w_last();
    BOOL ok = CreateDirectoryW(wp, NULL);
    FUSEDWASI_P1_FREE(wp);
    return ok ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_w_last();
}

static inline uint32_t fusedwasi_p1_be_remove(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int isdir, int *trapped) {
    (void)trapped;
    uint32_t rc;
    int slash;
    char *full = fusedwasi_p1_w_name(w, dir, path, len, 0, &rc, &slash);
    if (full == NULL) return rc;
    fusedwasi_p1_stat info;
    rc = fusedwasi_p1_w_stat(full, 0, &info);
    if (rc) {
        FUSEDWASI_P1_FREE(full);
        return rc;
    }
    wchar_t *wp = fusedwasi_p1_w_path(full);
    FUSEDWASI_P1_FREE(full);
    if (wp == NULL) return fusedwasi_p1_w_last();
    BOOL ok;
    if (isdir) {  /* rmdir: a directory and nothing else (a link to one is a link) */
        if (info.filetype != FUSEDWASI_P1_FT_DIR) {
            FUSEDWASI_P1_FREE(wp);
            return FUSEDWASI_P1_E_NOTDIR;
        }
        ok = RemoveDirectoryW(wp);
    } else {  /* unlink: the systems differ in what they say to a directory */
        if (info.filetype == FUSEDWASI_P1_FT_DIR) {
            FUSEDWASI_P1_FREE(wp);
            return FUSEDWASI_P1_E_ISDIR;
        }
        if (slash) {
            FUSEDWASI_P1_FREE(wp);
            return FUSEDWASI_P1_E_NOTDIR;
        }
        DWORD attributes = GetFileAttributesW(wp);
        if (info.filetype == FUSEDWASI_P1_FT_SYMLINK && attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            ok = RemoveDirectoryW(wp);  /* a link to a directory is removed as one */
        else
            ok = DeleteFileW(wp);
    }
    FUSEDWASI_P1_FREE(wp);
    return ok ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_w_last();
}

/* the times of an object that exists: only those that are set (the others stay), `now` where asked for */
static inline uint32_t fusedwasi_p1_w_set_times(HANDLE h, uint64_t atim, uint64_t mtim, uint32_t flags) {
    FILETIME now, a, m;
    GetSystemTimePreciseAsFileTime(&now);
    const FILETIME *pa = NULL, *pm = NULL;
    if (flags & FUSEDWASI_P1_T_ATIM_NOW) pa = &now;
    else if (flags & FUSEDWASI_P1_T_ATIM) {
        a = fusedwasi_p1_w_filetime(atim);
        pa = &a;
    }
    if (flags & FUSEDWASI_P1_T_MTIM_NOW) pm = &now;
    else if (flags & FUSEDWASI_P1_T_MTIM) {
        m = fusedwasi_p1_w_filetime(mtim);
        pm = &m;
    }
    return SetFileTime(h, NULL, pa, pm) ? FUSEDWASI_P1_E_SUCCESS : fusedwasi_p1_w_last();
}

static inline uint32_t fusedwasi_p1_w_times_path(const char *full, int follow, uint64_t atim, uint64_t mtim, uint32_t flags) {
    wchar_t *wp = fusedwasi_p1_w_path(full);
    if (wp == NULL) return fusedwasi_p1_w_last();
    HANDLE h = fusedwasi_p1_w_handle(wp, FILE_WRITE_ATTRIBUTES, follow);
    FUSEDWASI_P1_FREE(wp);
    if (h == INVALID_HANDLE_VALUE) return fusedwasi_p1_w_last();
    uint32_t rc = fusedwasi_p1_w_set_times(h, atim, mtim, flags);
    CloseHandle(h);
    return rc;
}

static inline uint32_t fusedwasi_p1_be_times_fd(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, uint64_t atim, uint64_t mtim, uint32_t flags, int *trapped) {
    (void)w;
    (void)trapped;
    if (e->kind == FUSEDWASI_P1_K_FILE) {
        HANDLE h = (HANDLE)_get_osfhandle(e->osfd);
        uint32_t rc = h == INVALID_HANDLE_VALUE ? FUSEDWASI_P1_E_BADF : fusedwasi_p1_w_set_times(h, atim, mtim, flags);
        if (rc == FUSEDWASI_P1_E_ACCES && e->root != NULL) rc = fusedwasi_p1_w_times_path(e->root, 1, atim, mtim, flags);  /* opened for reading only */
        return rc;
    }
    char *path = fusedwasi_p1_join(e->root, e->rel);
    if (path == NULL) return FUSEDWASI_P1_E_NOMEM;
    uint32_t rc = fusedwasi_p1_w_times_path(path, 1, atim, mtim, flags);
    FUSEDWASI_P1_FREE(path);
    return rc;
}

static inline uint32_t fusedwasi_p1_be_times_path(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, int follow, uint64_t atim,
                                          uint64_t mtim, uint32_t flags, int *trapped) {
    (void)trapped;
    char *full, *rel;
    int slash;
    uint32_t rc = fusedwasi_p1_w_resolve(w, dir, path, len, follow, &full, &rel, &slash);
    if (rc) return rc;
    FUSEDWASI_P1_FREE(rel);
    rc = fusedwasi_p1_w_times_path(full, follow, atim, mtim, flags);
    FUSEDWASI_P1_FREE(full);
    return rc;
}

/* A name that ends with a slash and is to be made (a link): there is nothing to make it a directory of. EEXIST when a directory is
   there, ENOTDIR when a file is, else ENOENT, whatever the system says to it. */
static inline uint32_t fusedwasi_p1_w_dest_with_slash(const char *full) {
    fusedwasi_p1_stat info;
    uint32_t rc = fusedwasi_p1_w_stat(full, 0, &info);
    if (rc) return rc == FUSEDWASI_P1_E_NOENT ? FUSEDWASI_P1_E_NOENT : rc;
    return info.filetype == FUSEDWASI_P1_FT_DIR ? FUSEDWASI_P1_E_EXIST : FUSEDWASI_P1_E_NOTDIR;
}

/* two names below two directories for a call that links or renames them: both paths, to free */
static inline uint32_t fusedwasi_p1_w_two_names(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow1, fusedwasi_p1_entry *d2,
                                        uint32_t p2, uint32_t l2, char **first, char **second, int *slash1, int *slash2) {
    uint32_t rc;
    *first = fusedwasi_p1_w_name(w, d1, p1, l1, follow1, &rc, slash1);
    if (*first == NULL) return rc;
    *second = fusedwasi_p1_w_name(w, d2, p2, l2, 0, &rc, slash2);
    if (*second == NULL) {
        FUSEDWASI_P1_FREE(*first);
        return rc;
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

static inline uint32_t fusedwasi_p1_be_rename(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, fusedwasi_p1_entry *d2, uint32_t p2,
                                      uint32_t l2, int *trapped) {
    (void)trapped;
    char *first = NULL, *second = NULL;
    int slash1, slash2;
    uint32_t rc = fusedwasi_p1_w_two_names(w, d1, p1, l1, 0, d2, p2, l2, &first, &second, &slash1, &slash2);
    if (rc) return rc;
    fusedwasi_p1_stat from, to;
    uint32_t have_from = fusedwasi_p1_w_stat(first, 0, &from), have_to = fusedwasi_p1_w_stat(second, 0, &to);
    if (have_from) {
        rc = have_from;
    } else if ((slash1 || slash2) && from.filetype != FUSEDWASI_P1_FT_DIR) {
        rc = FUSEDWASI_P1_E_NOTDIR;  /* a slash after the name asks for a directory */
    } else if (have_to == FUSEDWASI_P1_E_SUCCESS && slash2 && to.filetype != FUSEDWASI_P1_FT_DIR) {
        rc = FUSEDWASI_P1_E_NOTDIR;
    } else if (have_to == FUSEDWASI_P1_E_SUCCESS && from.filetype == FUSEDWASI_P1_FT_DIR && to.filetype != FUSEDWASI_P1_FT_DIR) {
        rc = FUSEDWASI_P1_E_NOTDIR;
    } else if (have_to == FUSEDWASI_P1_E_SUCCESS && from.filetype != FUSEDWASI_P1_FT_DIR && to.filetype == FUSEDWASI_P1_FT_DIR) {
        rc = FUSEDWASI_P1_E_ISDIR;
    }
    if (rc == FUSEDWASI_P1_E_SUCCESS) {
        wchar_t *wa = fusedwasi_p1_w_path(first), *wb = fusedwasi_p1_w_path(second);
        if (wa == NULL || wb == NULL) {
            rc = fusedwasi_p1_w_last();
        } else {
            /* POSIX replaces an empty directory by a directory; Windows does not, so it is taken away first (it says NOTEMPTY if it is not) */
            if (have_to == FUSEDWASI_P1_E_SUCCESS && to.filetype == FUSEDWASI_P1_FT_DIR && from.filetype == FUSEDWASI_P1_FT_DIR &&
                !(from.dev == to.dev && from.ino == to.ino) && !RemoveDirectoryW(wb))
                rc = fusedwasi_p1_w_last();
            else if (!MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING))
                rc = fusedwasi_p1_w_last();
        }
        FUSEDWASI_P1_FREE(wa);
        FUSEDWASI_P1_FREE(wb);
    }
    FUSEDWASI_P1_FREE(first);
    FUSEDWASI_P1_FREE(second);
    return rc;
}

static inline uint32_t fusedwasi_p1_be_link(fusedwasi_p1_state *w, fusedwasi_p1_entry *d1, uint32_t p1, uint32_t l1, int follow, fusedwasi_p1_entry *d2,
                                    uint32_t p2, uint32_t l2, int *trapped) {
    (void)trapped;
    char *first = NULL, *second = NULL;
    int slash1, slash2;
    uint32_t rc = fusedwasi_p1_w_two_names(w, d1, p1, l1, follow, d2, p2, l2, &first, &second, &slash1, &slash2);
    if (rc) return rc;
    fusedwasi_p1_stat info;
    rc = fusedwasi_p1_w_stat(first, follow, &info);  /* a link at the end is followed: it has to lead somewhere */
    if (rc == FUSEDWASI_P1_E_SUCCESS && info.filetype == FUSEDWASI_P1_FT_DIR) rc = FUSEDWASI_P1_E_PERM;  /* a hard link to a directory is not made */
    if (rc == FUSEDWASI_P1_E_SUCCESS && slash2) rc = fusedwasi_p1_w_dest_with_slash(second);
    if (rc == FUSEDWASI_P1_E_SUCCESS) {
        wchar_t *wa = fusedwasi_p1_w_path(first), *wb = fusedwasi_p1_w_path(second);
        if (wa == NULL || wb == NULL) rc = fusedwasi_p1_w_last();
        else if (!CreateHardLinkW(wb, wa, NULL)) rc = fusedwasi_p1_w_last();
        FUSEDWASI_P1_FREE(wa);
        FUSEDWASI_P1_FREE(wb);
    }
    FUSEDWASI_P1_FREE(first);
    FUSEDWASI_P1_FREE(second);
    return rc;
}

/* a symbolic link `new` that holds `target` (the text, never looked at) */
static inline uint32_t fusedwasi_p1_be_symlink(fusedwasi_p1_state *w, const unsigned char *target, uint32_t tlen, fusedwasi_p1_entry *dir, uint32_t path,
                                       uint32_t len, int *trapped) {
    (void)trapped;
    uint32_t rc;
    int slash;
    char *full = fusedwasi_p1_w_name(w, dir, path, len, 0, &rc, &slash);
    if (full == NULL) return rc == FUSEDWASI_P1_E_INVAL ? FUSEDWASI_P1_E_EXIST : rc;
    fusedwasi_p1_stat info;
    if (fusedwasi_p1_w_stat(full, 0, &info) == FUSEDWASI_P1_E_SUCCESS) {  /* the name is taken */
        FUSEDWASI_P1_FREE(full);
        return slash ? (info.filetype == FUSEDWASI_P1_FT_DIR ? FUSEDWASI_P1_E_EXIST : FUSEDWASI_P1_E_NOTDIR) : FUSEDWASI_P1_E_EXIST;
    }
    if (slash) {
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_NOENT;
    }
    if (tlen == 0) {
        FUSEDWASI_P1_FREE(full);
        return FUSEDWASI_P1_E_NOENT;
    }
    wchar_t *text = fusedwasi_p1_w_wide((const char *)target, tlen), *wp = fusedwasi_p1_w_path(full);
    if (text == NULL || wp == NULL) {
        rc = fusedwasi_p1_w_last();
    } else {
        for (wchar_t *c = text; *c; c++) {
            if (*c == L'/') *c = L'\\';  /* a link made with a slash does not work on Windows */
        }
        /* a link to a directory has to be made as one: the target is looked for from the directory of the link */
        DWORD flags = FUSEDWASI_P1_SYMLINK_ALLOW_UNPRIVILEGED;
        char *dirpart = fusedwasi_p1_copy(full, strlen(full)), *plain = fusedwasi_p1_copy((const char *)target, tlen);
        if (dirpart != NULL && plain != NULL) {
            fusedwasi_p1_w_parent(dirpart, 0);
            char *joined = fusedwasi_p1_join(dirpart, plain);
            fusedwasi_p1_stat target_info;
            if (joined != NULL && plain[0] != '/' && fusedwasi_p1_w_stat(joined, 1, &target_info) == FUSEDWASI_P1_E_SUCCESS &&
                target_info.filetype == FUSEDWASI_P1_FT_DIR)
                flags |= 0x1; /* SYMBOLIC_LINK_FLAG_DIRECTORY */
            FUSEDWASI_P1_FREE(joined);
        }
        FUSEDWASI_P1_FREE(dirpart);
        FUSEDWASI_P1_FREE(plain);
        if (!CreateSymbolicLinkW(wp, text, flags)) {
            DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER &&  /* a Windows that does not know the flag */
                CreateSymbolicLinkW(wp, text, flags & ~(DWORD)FUSEDWASI_P1_SYMLINK_ALLOW_UNPRIVILEGED))
                rc = FUSEDWASI_P1_E_SUCCESS;
            else
                rc = fusedwasi_p1_w_errno(error);
        }
    }
    FUSEDWASI_P1_FREE(text);
    FUSEDWASI_P1_FREE(wp);
    FUSEDWASI_P1_FREE(full);
    return rc;
}

/* The reparse data of a symbolic link (winnt.h has the tag and the control code, not this structure). */
typedef struct {
    ULONG tag;
    USHORT data_length, reserved;
    USHORT substitute_offset, substitute_length, print_offset, print_length;
    ULONG flags;
    WCHAR path_buffer[1];
} fusedwasi_p1_w_reparse;

/* what a symbolic link holds, at most `cap` bytes of it */
static inline uint32_t fusedwasi_p1_be_readlink(fusedwasi_p1_state *w, fusedwasi_p1_entry *dir, uint32_t path, uint32_t len, char *out, size_t cap,
                                        size_t *used, int *trapped) {
    (void)trapped;
    uint32_t rc;
    int slash;
    char *full = fusedwasi_p1_w_name(w, dir, path, len, 0, &rc, &slash);
    if (full == NULL) return rc;
    wchar_t *wp = fusedwasi_p1_w_path(full);
    FUSEDWASI_P1_FREE(full);
    if (wp == NULL) return fusedwasi_p1_w_last();
    HANDLE h = fusedwasi_p1_w_handle(wp, 0, 0);
    FUSEDWASI_P1_FREE(wp);
    if (h == INVALID_HANDLE_VALUE) return fusedwasi_p1_w_last();
    BYTE *buffer = (BYTE *)FUSEDWASI_P1_MALLOC(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD got = 0;
    if (buffer == NULL) {
        CloseHandle(h);
        return FUSEDWASI_P1_E_NOMEM;
    }
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &got, NULL)) {
        rc = fusedwasi_p1_w_last();
        CloseHandle(h);
        FUSEDWASI_P1_FREE(buffer);
        return rc;
    }
    CloseHandle(h);
    const fusedwasi_p1_w_reparse *r = (const fusedwasi_p1_w_reparse *)buffer;
    if (r->tag != IO_REPARSE_TAG_SYMLINK) {
        FUSEDWASI_P1_FREE(buffer);
        return FUSEDWASI_P1_E_INVAL;
    }
    const wchar_t *name = r->path_buffer + r->print_offset / sizeof(WCHAR);
    size_t chars = r->print_length / sizeof(WCHAR);
    if (chars == 0) {  /* no name to print: the one the system uses, without its \??\ */
        name = r->path_buffer + r->substitute_offset / sizeof(WCHAR);
        chars = r->substitute_length / sizeof(WCHAR);
        if (chars >= 4 && name[0] == L'\\' && name[1] == L'?' && name[2] == L'?' && name[3] == L'\\') {
            name += 4;
            chars -= 4;
        }
    }
    char *text = fusedwasi_p1_w_narrow(name, chars);
    FUSEDWASI_P1_FREE(buffer);
    if (text == NULL) return FUSEDWASI_P1_E_NOMEM;
    size_t n = strlen(text);
    *used = n < cap ? n : cap;
    memcpy(out, text, *used);
    FUSEDWASI_P1_FREE(text);
    for (size_t i = 0; i < *used; i++) {
        if (out[i] == '\\') out[i] = '/';
    }
    return FUSEDWASI_P1_E_SUCCESS;
}

/* the entries of a directory */
static inline uint32_t fusedwasi_p1_be_list(fusedwasi_p1_state *w, fusedwasi_p1_entry *e, fusedwasi_p1_dirlist *list, int *trapped) {
    (void)w;
    (void)trapped;
    char *path = fusedwasi_p1_join(e->root, e->rel);
    if (path == NULL) return FUSEDWASI_P1_E_NOMEM;
    uint64_t ino_here = 0, ino_up = 0;  /* of "." and ".." (what fd_filestat_get says of the directory and of the one above it) */
    fusedwasi_p1_stat info;
    if (fusedwasi_p1_w_stat(path, 1, &info) == FUSEDWASI_P1_E_SUCCESS) ino_here = info.ino;
    {
        char *up = fusedwasi_p1_copy(path, strlen(path));
        if (up != NULL) {
            fusedwasi_p1_w_parent(up, 2);
            if (strlen(up) == 2 && up[1] == ':') {  /* the root of a drive */
                up[2] = '/';
                up[3] = '\0';
            }
            if (fusedwasi_p1_w_stat(up, 1, &info) == FUSEDWASI_P1_E_SUCCESS) ino_up = info.ino;
            FUSEDWASI_P1_FREE(up);
        }
    }
    wchar_t *wp = fusedwasi_p1_w_path(path);
    if (wp == NULL) {
        uint32_t rc = fusedwasi_p1_w_last();
        FUSEDWASI_P1_FREE(path);
        return rc;
    }
    size_t wlen = wcslen(wp);
    wchar_t *pattern = (wchar_t *)FUSEDWASI_P1_MALLOC((wlen + 3) * sizeof(wchar_t));
    if (pattern == NULL) {
        FUSEDWASI_P1_FREE(wp);
        FUSEDWASI_P1_FREE(path);
        return FUSEDWASI_P1_E_NOMEM;
    }
    wmemcpy(pattern, wp, wlen);
    if (wlen > 0 && wp[wlen - 1] != L'\\') pattern[wlen++] = L'\\';
    pattern[wlen++] = L'*';
    pattern[wlen] = L'\0';
    FUSEDWASI_P1_FREE(wp);
    uint32_t rc = FUSEDWASI_P1_E_SUCCESS;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    FUSEDWASI_P1_FREE(pattern);
    if (find == INVALID_HANDLE_VALUE) {
        rc = fusedwasi_p1_w_last();
        FUSEDWASI_P1_FREE(path);
        return rc;
    }
    if (fusedwasi_p1_dirlist_add(list, ".", ino_here, FUSEDWASI_P1_FT_DIR) < 0 || fusedwasi_p1_dirlist_add(list, "..", ino_up, FUSEDWASI_P1_FT_DIR) < 0) rc = FUSEDWASI_P1_E_NOMEM;
    do {
        if (rc != FUSEDWASI_P1_E_SUCCESS) break;
        if ((data.cFileName[0] == L'.' && data.cFileName[1] == L'\0') ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))
            continue;
        char *name = fusedwasi_p1_w_narrow(data.cFileName, wcslen(data.cFileName));
        if (name == NULL) {
            rc = FUSEDWASI_P1_E_NOMEM;
            break;
        }
        char *full = fusedwasi_p1_join(path, name);
        uint8_t type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FUSEDWASI_P1_FT_DIR : FUSEDWASI_P1_FT_REGULAR;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && data.dwReserved0 == IO_REPARSE_TAG_SYMLINK) type = FUSEDWASI_P1_FT_SYMLINK;
        uint64_t ino = 0;
        if (full != NULL && fusedwasi_p1_w_stat(full, 0, &info) == FUSEDWASI_P1_E_SUCCESS) {  /* gone since the listing was made: it is left out */
            ino = info.ino;
            type = info.filetype;
            if (fusedwasi_p1_dirlist_add(list, name, ino, type) < 0) rc = FUSEDWASI_P1_E_NOMEM;
        } else if (full == NULL) {
            rc = FUSEDWASI_P1_E_NOMEM;
        }
        FUSEDWASI_P1_FREE(full);
        FUSEDWASI_P1_FREE(name);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    FUSEDWASI_P1_FREE(path);
    return rc;
}

#endif
