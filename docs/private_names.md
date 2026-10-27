# Names that became private (step 5 of the checklist, 2026-10-27)

One line for each name that a user may have been calling and that is now private. The rule is in `design_model.md`, section 5: a private name carries a mark, a double underscore after the prefix in C and a leading underscore in Python. Nothing in this list is public any more; the names that are public are in `public_api.md`. If a user needs one of them, it is a reason to list it as public (and to say what it is for), not to call it as it is.

Written once, by `git diff` of the commit that marked them; the tool `tools/public_api.py mark` does the same for any name added later (those were never public, so they are not listed here).

## C: names

| Before | Now |
| --- | --- |
| `FUSEDWASI_P1_CLOSE` | `FUSEDWASI_P1__CLOSE` |
| `FUSEDWASI_P1_CORE_H` | `FUSEDWASI_P1__CORE_H` |
| `FUSEDWASI_P1_DIRS_ARE_PATHS` | `FUSEDWASI_P1__DIRS_ARE_PATHS` |
| `FUSEDWASI_P1_FDS` | `FUSEDWASI_P1__FDS` |
| `FUSEDWASI_P1_K_DIR` | `FUSEDWASI_P1__K_DIR` |
| `FUSEDWASI_P1_K_FILE` | `FUSEDWASI_P1__K_FILE` |
| `FUSEDWASI_P1_K_NONE` | `FUSEDWASI_P1__K_NONE` |
| `FUSEDWASI_P1_K_STD` | `FUSEDWASI_P1__K_STD` |
| `FUSEDWASI_P1_LINK_MAX` | `FUSEDWASI_P1__LINK_MAX` |
| `FUSEDWASI_P1_LSEEK` | `FUSEDWASI_P1__LSEEK` |
| `FUSEDWASI_P1_NATIVE_POLL` | `FUSEDWASI_P1__NATIVE_POLL` |
| `FUSEDWASI_P1_OPTS` | `FUSEDWASI_P1__OPTS` |
| `FUSEDWASI_P1_PREVIEW1_H` | `FUSEDWASI_P1__PREVIEW1_H` |
| `FUSEDWASI_P1_R` | `FUSEDWASI_P1__R` |
| `FUSEDWASI_P1_READ` | `FUSEDWASI_P1__READ` |
| `FUSEDWASI_P1_SEEK_RIGHT` | `FUSEDWASI_P1__SEEK_RIGHT` |
| `FUSEDWASI_P1_SYNC` | `FUSEDWASI_P1__SYNC` |
| `FUSEDWASI_P1_UNSTABLE_H` | `FUSEDWASI_P1__UNSTABLE_H` |
| `FUSEDWASI_P1_WRITE` | `FUSEDWASI_P1__WRITE` |
| `FUSEDWASI_P2_ACCEPT` | `FUSEDWASI_P2__ACCEPT` |
| `FUSEDWASI_P2_ATIME` | `FUSEDWASI_P2__ATIME` |
| `FUSEDWASI_P2_BACKLOG_MAX` | `FUSEDWASI_P2__BACKLOG_MAX` |
| `FUSEDWASI_P2_CHECK_SEND_PERMIT` | `FUSEDWASI_P2__CHECK_SEND_PERMIT` |
| `FUSEDWASI_P2_CLI_H` | `FUSEDWASI_P2__CLI_H` |
| `FUSEDWASI_P2_CLOCKS_H` | `FUSEDWASI_P2__CLOCKS_H` |
| `FUSEDWASI_P2_CLOEXEC` | `FUSEDWASI_P2__CLOEXEC` |
| `FUSEDWASI_P2_CLOSE` | `FUSEDWASI_P2__CLOSE` |
| `FUSEDWASI_P2_CORE_H` | `FUSEDWASI_P2__CORE_H` |
| `FUSEDWASI_P2_CREATE` | `FUSEDWASI_P2__CREATE` |
| `FUSEDWASI_P2_CTIME` | `FUSEDWASI_P2__CTIME` |
| `FUSEDWASI_P2_DATA_SYNC` | `FUSEDWASI_P2__DATA_SYNC` |
| `FUSEDWASI_P2_DIRECTORY` | `FUSEDWASI_P2__DIRECTORY` |
| `FUSEDWASI_P2_DIRREF_OF` | `FUSEDWASI_P2__DIRREF_OF` |
| `FUSEDWASI_P2_EPOCH` | `FUSEDWASI_P2__EPOCH` |
| `FUSEDWASI_P2_EXCLUSIVE` | `FUSEDWASI_P2__EXCLUSIVE` |
| `FUSEDWASI_P2_E_ESCAPE` | `FUSEDWASI_P2__E_ESCAPE` |
| `FUSEDWASI_P2_E_NO_CLOCK` | `FUSEDWASI_P2__E_NO_CLOCK` |
| `FUSEDWASI_P2_FILESYSTEM_H` | `FUSEDWASI_P2__FILESYSTEM_H` |
| `FUSEDWASI_P2_FILE_SYNC` | `FUSEDWASI_P2__FILE_SYNC` |
| `FUSEDWASI_P2_FS_P1` | `FUSEDWASI_P2__FS_P1` |
| `FUSEDWASI_P2_FS_POSIX_H` | `FUSEDWASI_P2__FS_POSIX_H` |
| `FUSEDWASI_P2_FS_WIN32_H` | `FUSEDWASI_P2__FS_WIN32_H` |
| `FUSEDWASI_P2_GENERATION_MASK` | `FUSEDWASI_P2__GENERATION_MASK` |
| `FUSEDWASI_P2_GETSOCKOPT` | `FUSEDWASI_P2__GETSOCKOPT` |
| `FUSEDWASI_P2_HAVE_GETENTROPY` | `FUSEDWASI_P2__HAVE_GETENTROPY` |
| `FUSEDWASI_P2_HE_FORBIDDEN` | `FUSEDWASI_P2__HE_FORBIDDEN` |
| `FUSEDWASI_P2_HE_IMMUTABLE` | `FUSEDWASI_P2__HE_IMMUTABLE` |
| `FUSEDWASI_P2_HE_INVALID_SYNTAX` | `FUSEDWASI_P2__HE_INVALID_SYNTAX` |
| `FUSEDWASI_P2_HE_NONE` | `FUSEDWASI_P2__HE_NONE` |
| `FUSEDWASI_P2_HK_N16` | `FUSEDWASI_P2__HK_N16` |
| `FUSEDWASI_P2_HK_N32` | `FUSEDWASI_P2__HK_N32` |
| `FUSEDWASI_P2_HK_N64` | `FUSEDWASI_P2__HK_N64` |
| `FUSEDWASI_P2_HK_N8` | `FUSEDWASI_P2__HK_N8` |
| `FUSEDWASI_P2_HK_NONE` | `FUSEDWASI_P2__HK_NONE` |
| `FUSEDWASI_P2_HK_STR` | `FUSEDWASI_P2__HK_STR` |
| `FUSEDWASI_P2_HTTP_CASES` | `FUSEDWASI_P2__HTTP_CASES` |
| `FUSEDWASI_P2_HTTP_DEFAULT_TIMEOUT_NS` | `FUSEDWASI_P2__HTTP_DEFAULT_TIMEOUT_NS` |
| `FUSEDWASI_P2_HTTP_H` | `FUSEDWASI_P2__HTTP_H` |
| `FUSEDWASI_P2_HTTP_OOM` | `FUSEDWASI_P2__HTTP_OOM` |
| `FUSEDWASI_P2_INDEX_BITS` | `FUSEDWASI_P2__INDEX_BITS` |
| `FUSEDWASI_P2_INDEX_MASK` | `FUSEDWASI_P2__INDEX_MASK` |
| `FUSEDWASI_P2_IO_H` | `FUSEDWASI_P2__IO_H` |
| `FUSEDWASI_P2_KEEPIDLE` | `FUSEDWASI_P2__KEEPIDLE` |
| `FUSEDWASI_P2_KIND_P1_FDS` | `FUSEDWASI_P2__KIND_P1_FDS` |
| `FUSEDWASI_P2_MAX_DEPTH` | `FUSEDWASI_P2__MAX_DEPTH` |
| `FUSEDWASI_P2_MAX_LINKS` | `FUSEDWASI_P2__MAX_LINKS` |
| `FUSEDWASI_P2_MSG_NOSIGNAL` | `FUSEDWASI_P2__MSG_NOSIGNAL` |
| `FUSEDWASI_P2_MTIME` | `FUSEDWASI_P2__MTIME` |
| `FUSEDWASI_P2_MUTATE` | `FUSEDWASI_P2__MUTATE` |
| `FUSEDWASI_P2_NAME_MAX` | `FUSEDWASI_P2__NAME_MAX` |
| `FUSEDWASI_P2_NAME_MKDIR` | `FUSEDWASI_P2__NAME_MKDIR` |
| `FUSEDWASI_P2_NAME_RMDIR` | `FUSEDWASI_P2__NAME_RMDIR` |
| `FUSEDWASI_P2_NAME_SYMLINK` | `FUSEDWASI_P2__NAME_SYMLINK` |
| `FUSEDWASI_P2_NAME_UNLINK` | `FUSEDWASI_P2__NAME_UNLINK` |
| `FUSEDWASI_P2_NET_H` | `FUSEDWASI_P2__NET_H` |
| `FUSEDWASI_P2_NOK` | `FUSEDWASI_P2__NOK` |
| `FUSEDWASI_P2_OK` | `FUSEDWASI_P2__OK` |
| `FUSEDWASI_P2_OPT_CNT` | `FUSEDWASI_P2__OPT_CNT` |
| `FUSEDWASI_P2_OPT_IDLE` | `FUSEDWASI_P2__OPT_IDLE` |
| `FUSEDWASI_P2_OPT_INTVL` | `FUSEDWASI_P2__OPT_INTVL` |
| `FUSEDWASI_P2_PATH_MAX` | `FUSEDWASI_P2__PATH_MAX` |
| `FUSEDWASI_P2_POLL` | `FUSEDWASI_P2__POLL` |
| `FUSEDWASI_P2_PUT` | `FUSEDWASI_P2__PUT` |
| `FUSEDWASI_P2_PUTS` | `FUSEDWASI_P2__PUTS` |
| `FUSEDWASI_P2_RANDOM_H` | `FUSEDWASI_P2__RANDOM_H` |
| `FUSEDWASI_P2_RANDOM_MAX` | `FUSEDWASI_P2__RANDOM_MAX` |
| `FUSEDWASI_P2_READ` | `FUSEDWASI_P2__READ` |
| `FUSEDWASI_P2_READ_CAP` | `FUSEDWASI_P2__READ_CAP` |
| `FUSEDWASI_P2_RECV` | `FUSEDWASI_P2__RECV` |
| `FUSEDWASI_P2_RECVFROM` | `FUSEDWASI_P2__RECVFROM` |
| `FUSEDWASI_P2_REPARSE_MOUNT_POINT` | `FUSEDWASI_P2__REPARSE_MOUNT_POINT` |
| `FUSEDWASI_P2_REPARSE_SYMLINK` | `FUSEDWASI_P2__REPARSE_SYMLINK` |
| `FUSEDWASI_P2_REQUESTED_SYNC` | `FUSEDWASI_P2__REQUESTED_SYNC` |
| `FUSEDWASI_P2_SEND` | `FUSEDWASI_P2__SEND` |
| `FUSEDWASI_P2_SENDTO` | `FUSEDWASI_P2__SENDTO` |
| `FUSEDWASI_P2_SETSOCKOPT` | `FUSEDWASI_P2__SETSOCKOPT` |
| `FUSEDWASI_P2_SHARE` | `FUSEDWASI_P2__SHARE` |
| `FUSEDWASI_P2_SHUT_RD` | `FUSEDWASI_P2__SHUT_RD` |
| `FUSEDWASI_P2_SHUT_RDWR` | `FUSEDWASI_P2__SHUT_RDWR` |
| `FUSEDWASI_P2_SHUT_WR` | `FUSEDWASI_P2__SHUT_WR` |
| `FUSEDWASI_P2_SIO_UDP_CONNRESET` | `FUSEDWASI_P2__SIO_UDP_CONNRESET` |
| `FUSEDWASI_P2_SOCKET` | `FUSEDWASI_P2__SOCKET` |
| `FUSEDWASI_P2_SOCKETS_H` | `FUSEDWASI_P2__SOCKETS_H` |
| `FUSEDWASI_P2_SOCK_CHUNK` | `FUSEDWASI_P2__SOCK_CHUNK` |
| `FUSEDWASI_P2_SS_BIND_IN_PROGRESS` | `FUSEDWASI_P2__SS_BIND_IN_PROGRESS` |
| `FUSEDWASI_P2_SS_BOUND` | `FUSEDWASI_P2__SS_BOUND` |
| `FUSEDWASI_P2_SS_CLOSED` | `FUSEDWASI_P2__SS_CLOSED` |
| `FUSEDWASI_P2_SS_CONNECTED` | `FUSEDWASI_P2__SS_CONNECTED` |
| `FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS` | `FUSEDWASI_P2__SS_CONNECT_IN_PROGRESS` |
| `FUSEDWASI_P2_SS_LISTENING` | `FUSEDWASI_P2__SS_LISTENING` |
| `FUSEDWASI_P2_SS_LISTEN_IN_PROGRESS` | `FUSEDWASI_P2__SS_LISTEN_IN_PROGRESS` |
| `FUSEDWASI_P2_SS_UNBOUND` | `FUSEDWASI_P2__SS_UNBOUND` |
| `FUSEDWASI_P2_STD_CHUNK` | `FUSEDWASI_P2__STD_CHUNK` |
| `FUSEDWASI_P2_STREAM_CHUNK` | `FUSEDWASI_P2__STREAM_CHUNK` |
| `FUSEDWASI_P2_SYMLINK_ALLOW_UNPRIVILEGED` | `FUSEDWASI_P2__SYMLINK_ALLOW_UNPRIVILEGED` |
| `FUSEDWASI_P2_SYMLINK_DIRECTORY` | `FUSEDWASI_P2__SYMLINK_DIRECTORY` |
| `FUSEDWASI_P2_SYMLINK_FOLLOW` | `FUSEDWASI_P2__SYMLINK_FOLLOW` |
| `FUSEDWASI_P2_TRUNCATE` | `FUSEDWASI_P2__TRUNCATE` |
| `FUSEDWASI_P2_T_BLOCK` | `FUSEDWASI_P2__T_BLOCK` |
| `FUSEDWASI_P2_T_CHAR` | `FUSEDWASI_P2__T_CHAR` |
| `FUSEDWASI_P2_T_DIRECTORY` | `FUSEDWASI_P2__T_DIRECTORY` |
| `FUSEDWASI_P2_T_FIFO` | `FUSEDWASI_P2__T_FIFO` |
| `FUSEDWASI_P2_T_REGULAR` | `FUSEDWASI_P2__T_REGULAR` |
| `FUSEDWASI_P2_T_SOCKET` | `FUSEDWASI_P2__T_SOCKET` |
| `FUSEDWASI_P2_T_SYMLINK` | `FUSEDWASI_P2__T_SYMLINK` |
| `FUSEDWASI_P2_T_UNKNOWN` | `FUSEDWASI_P2__T_UNKNOWN` |
| `FUSEDWASI_P2_WRITE` | `FUSEDWASI_P2__WRITE` |
| `FUSEDWASI_H` | `FUSEDWASI__H` |
| `fusedwasi_p1_clock_ns` | `fusedwasi_p1__clock_ns` |
| `fusedwasi_p1_copy` | `fusedwasi_p1__copy` |
| `fusedwasi_p1_desc` | `fusedwasi_p1__desc` |
| `fusedwasi_p1_dir_with` | `fusedwasi_p1__dir_with` |
| `fusedwasi_p1_dirent` | `fusedwasi_p1__dirent` |
| `fusedwasi_p1_dirent_order` | `fusedwasi_p1__dirent_order` |
| `fusedwasi_p1_dirlist` | `fusedwasi_p1__dirlist` |
| `fusedwasi_p1_dirlist_add` | `fusedwasi_p1__dirlist_add` |
| `fusedwasi_p1_dirlist_free` | `fusedwasi_p1__dirlist_free` |
| `fusedwasi_p1_entry` | `fusedwasi_p1__entry` |
| `fusedwasi_p1_entry_of` | `fusedwasi_p1__entry_of` |
| `fusedwasi_p1_entry_with` | `fusedwasi_p1__entry_with` |
| `fusedwasi_p1_errno_of_code` | `fusedwasi_p1__errno_of_code` |
| `fusedwasi_p1_fd_at` | `fusedwasi_p1__fd_at` |
| `fusedwasi_p1_fds` | `fusedwasi_p1__fds` |
| `fusedwasi_p1_fds_destroy` | `fusedwasi_p1__fds_destroy` |
| `fusedwasi_p1_fds_get` | `fusedwasi_p1__fds_get` |
| `fusedwasi_p1_filestat_of_fd` | `fusedwasi_p1__filestat_of_fd` |
| `fusedwasi_p1_filestat_of_path` | `fusedwasi_p1__filestat_of_path` |
| `fusedwasi_p1_filetype_of_type` | `fusedwasi_p1__filetype_of_type` |
| `fusedwasi_p1_get_u32` | `fusedwasi_p1__get_u32` |
| `fusedwasi_p1_in_memory` | `fusedwasi_p1__in_memory` |
| `fusedwasi_p1_iovs_valid` | `fusedwasi_p1__iovs_valid` |
| `fusedwasi_p1_list` | `fusedwasi_p1__list` |
| `fusedwasi_p1_make_fds` | `fusedwasi_p1__make_fds` |
| `fusedwasi_p1_mem` | `fusedwasi_p1__mem` |
| `fusedwasi_p1_native_fdflags` | `fusedwasi_p1__native_fdflags` |
| `fusedwasi_p1_new_fd` | `fusedwasi_p1__new_fd` |
| `fusedwasi_p1_not_a_socket` | `fusedwasi_p1__not_a_socket` |
| `fusedwasi_p1_of_code` | `fusedwasi_p1__of_code` |
| `fusedwasi_p1_poll` | `fusedwasi_p1__poll` |
| `fusedwasi_p1_poll_kind` | `fusedwasi_p1__poll_kind` |
| `fusedwasi_p1_put_filestat` | `fusedwasi_p1__put_filestat` |
| `fusedwasi_p1_put_u16` | `fusedwasi_p1__put_u16` |
| `fusedwasi_p1_put_u32` | `fusedwasi_p1__put_u32` |
| `fusedwasi_p1_put_u64` | `fusedwasi_p1__put_u64` |
| `fusedwasi_p1_put_u8` | `fusedwasi_p1__put_u8` |
| `fusedwasi_p1_rc` | `fusedwasi_p1__rc` |
| `fusedwasi_p1_read_std` | `fusedwasi_p1__read_std` |
| `fusedwasi_p1_release` | `fusedwasi_p1__release` |
| `fusedwasi_p1_seek_native` | `fusedwasi_p1__seek_native` |
| `fusedwasi_p1_set_size` | `fusedwasi_p1__set_size` |
| `fusedwasi_p1_stat` | `fusedwasi_p1__stat` |
| `fusedwasi_p1_stat_of_p2` | `fusedwasi_p1__stat_of_p2` |
| `fusedwasi_p1_std_filetype` | `fusedwasi_p1__std_filetype` |
| `fusedwasi_p1_std_is_real` | `fusedwasi_p1__std_is_real` |
| `fusedwasi_p1_stream_status` | `fusedwasi_p1__stream_status` |
| `fusedwasi_p1_strings_get` | `fusedwasi_p1__strings_get` |
| `fusedwasi_p1_strings_sizes` | `fusedwasi_p1__strings_sizes` |
| `fusedwasi_p1_subscription_wait` | `fusedwasi_p1__subscription_wait` |
| `fusedwasi_p1_sync_entry` | `fusedwasi_p1__sync_entry` |
| `fusedwasi_p1_time_of` | `fusedwasi_p1__time_of` |
| `fusedwasi_p1_times` | `fusedwasi_p1__times` |
| `fusedwasi_p1_times_flags_ok` | `fusedwasi_p1__times_flags_ok` |
| `fusedwasi_p1_unseekable` | `fusedwasi_p1__unseekable` |
| `fusedwasi_p1_waiter` | `fusedwasi_p1__waiter` |
| `fusedwasi_p1_waiter_ask` | `fusedwasi_p1__waiter_ask` |
| `fusedwasi_p1_write_std` | `fusedwasi_p1__write_std` |
| `fusedwasi_p2_addr` | `fusedwasi_p2__addr` |
| `fusedwasi_p2_addr_flat` | `fusedwasi_p2__addr_flat` |
| `fusedwasi_p2_addr_text` | `fusedwasi_p2__addr_text` |
| `fusedwasi_p2_addr_unicast` | `fusedwasi_p2__addr_unicast` |
| `fusedwasi_p2_addr_unspecified` | `fusedwasi_p2__addr_unspecified` |
| `fusedwasi_p2_address_result` | `fusedwasi_p2__address_result` |
| `fusedwasi_p2_alloc` | `fusedwasi_p2__alloc` |
| `fusedwasi_p2_always_ready` | `fusedwasi_p2__always_ready` |
| `fusedwasi_p2_buffer_output_check_write` | `fusedwasi_p2__buffer_output_check_write` |
| `fusedwasi_p2_buffer_output_flush` | `fusedwasi_p2__buffer_output_flush` |
| `fusedwasi_p2_buffer_output_ready` | `fusedwasi_p2__buffer_output_ready` |
| `fusedwasi_p2_buffer_output_write` | `fusedwasi_p2__buffer_output_write` |
| `fusedwasi_p2_buffer_set` | `fusedwasi_p2__buffer_set` |
| `fusedwasi_p2_check_bind` | `fusedwasi_p2__check_bind` |
| `fusedwasi_p2_check_connect` | `fusedwasi_p2__check_connect` |
| `fusedwasi_p2_clock_ns` | `fusedwasi_p2__clock_ns` |
| `fusedwasi_p2_datetime` | `fusedwasi_p2__datetime` |
| `fusedwasi_p2_datetime_some` | `fusedwasi_p2__datetime_some` |
| `fusedwasi_p2_deadline_pollable` | `fusedwasi_p2__deadline_pollable` |
| `fusedwasi_p2_deadline_ready` | `fusedwasi_p2__deadline_ready` |
| `fusedwasi_p2_default_clock` | `fusedwasi_p2__default_clock` |
| `fusedwasi_p2_default_exit` | `fusedwasi_p2__default_exit` |
| `fusedwasi_p2_default_random_bytes` | `fusedwasi_p2__default_random_bytes` |
| `fusedwasi_p2_default_resolution_ns` | `fusedwasi_p2__default_resolution_ns` |
| `fusedwasi_p2_default_sleep` | `fusedwasi_p2__default_sleep` |
| `fusedwasi_p2_default_trap` | `fusedwasi_p2__default_trap` |
| `fusedwasi_p2_default_wait` | `fusedwasi_p2__default_wait` |
| `fusedwasi_p2_descriptor` | `fusedwasi_p2__descriptor` |
| `fusedwasi_p2_descriptor_destroy` | `fusedwasi_p2__descriptor_destroy` |
| `fusedwasi_p2_descriptor_find` | `fusedwasi_p2__descriptor_find` |
| `fusedwasi_p2_descriptor_get` | `fusedwasi_p2__descriptor_get` |
| `fusedwasi_p2_descriptor_new` | `fusedwasi_p2__descriptor_new` |
| `fusedwasi_p2_dgram_destroy` | `fusedwasi_p2__dgram_destroy` |
| `fusedwasi_p2_dgram_stream` | `fusedwasi_p2__dgram_stream` |
| `fusedwasi_p2_dgram_stream_new` | `fusedwasi_p2__dgram_stream_new` |
| `fusedwasi_p2_dir_entries` | `fusedwasi_p2__dir_entries` |
| `fusedwasi_p2_dir_entries_destroy` | `fusedwasi_p2__dir_entries_destroy` |
| `fusedwasi_p2_dirref` | `fusedwasi_p2__dirref` |
| `fusedwasi_p2_errno` | `fusedwasi_p2__errno` |
| `fusedwasi_p2_errno_code` | `fusedwasi_p2__errno_code` |
| `fusedwasi_p2_error` | `fusedwasi_p2__error` |
| `fusedwasi_p2_error_new_codes` | `fusedwasi_p2__error_new_codes` |
| `fusedwasi_p2_fail` | `fusedwasi_p2__fail` |
| `fusedwasi_p2_fd_check_write` | `fusedwasi_p2__fd_check_write` |
| `fusedwasi_p2_fd_flush` | `fusedwasi_p2__fd_flush` |
| `fusedwasi_p2_fd_read` | `fusedwasi_p2__fd_read` |
| `fusedwasi_p2_fd_ready_in` | `fusedwasi_p2__fd_ready_in` |
| `fusedwasi_p2_fd_write` | `fusedwasi_p2__fd_write` |
| `fusedwasi_p2_file_check_write` | `fusedwasi_p2__file_check_write` |
| `fusedwasi_p2_file_flush` | `fusedwasi_p2__file_flush` |
| `fusedwasi_p2_file_read` | `fusedwasi_p2__file_read` |
| `fusedwasi_p2_file_stream` | `fusedwasi_p2__file_stream` |
| `fusedwasi_p2_file_stream_destroy` | `fusedwasi_p2__file_stream_destroy` |
| `fusedwasi_p2_file_stream_new` | `fusedwasi_p2__file_stream_new` |
| `fusedwasi_p2_file_stream_set_failure` | `fusedwasi_p2__file_stream_set_failure` |
| `fusedwasi_p2_file_write` | `fusedwasi_p2__file_write` |
| `fusedwasi_p2_filetime_ns` | `fusedwasi_p2__filetime_ns` |
| `fusedwasi_p2_fnv` | `fusedwasi_p2__fnv` |
| `fusedwasi_p2_free_obj` | `fusedwasi_p2__free_obj` |
| `fusedwasi_p2_from_sockaddr` | `fusedwasi_p2__from_sockaddr` |
| `fusedwasi_p2_fs_create_directory_at` | `fusedwasi_p2__fs_create_directory_at` |
| `fusedwasi_p2_fs_fail` | `fusedwasi_p2__fs_fail` |
| `fusedwasi_p2_fs_is_itself` | `fusedwasi_p2__fs_is_itself` |
| `fusedwasi_p2_fs_leaf_slash` | `fusedwasi_p2__fs_leaf_slash` |
| `fusedwasi_p2_fs_link_at` | `fusedwasi_p2__fs_link_at` |
| `fusedwasi_p2_fs_name_op` | `fusedwasi_p2__fs_name_op` |
| `fusedwasi_p2_fs_need` | `fusedwasi_p2__fs_need` |
| `fusedwasi_p2_fs_open_at` | `fusedwasi_p2__fs_open_at` |
| `fusedwasi_p2_fs_pair` | `fusedwasi_p2__fs_pair` |
| `fusedwasi_p2_fs_read` | `fusedwasi_p2__fs_read` |
| `fusedwasi_p2_fs_read_directory` | `fusedwasi_p2__fs_read_directory` |
| `fusedwasi_p2_fs_readlink_at` | `fusedwasi_p2__fs_readlink_at` |
| `fusedwasi_p2_fs_remove_directory_at` | `fusedwasi_p2__fs_remove_directory_at` |
| `fusedwasi_p2_fs_rename_at` | `fusedwasi_p2__fs_rename_at` |
| `fusedwasi_p2_fs_set_size` | `fusedwasi_p2__fs_set_size` |
| `fusedwasi_p2_fs_set_times` | `fusedwasi_p2__fs_set_times` |
| `fusedwasi_p2_fs_set_times_at` | `fusedwasi_p2__fs_set_times_at` |
| `fusedwasi_p2_fs_stat` | `fusedwasi_p2__fs_stat` |
| `fusedwasi_p2_fs_stat_at` | `fusedwasi_p2__fs_stat_at` |
| `fusedwasi_p2_fs_stat_entry` | `fusedwasi_p2__fs_stat_entry` |
| `fusedwasi_p2_fs_symlink_at` | `fusedwasi_p2__fs_symlink_at` |
| `fusedwasi_p2_fs_sync` | `fusedwasi_p2__fs_sync` |
| `fusedwasi_p2_fs_times_of` | `fusedwasi_p2__fs_times_of` |
| `fusedwasi_p2_fs_unlink_file_at` | `fusedwasi_p2__fs_unlink_file_at` |
| `fusedwasi_p2_fs_walk` | `fusedwasi_p2__fs_walk` |
| `fusedwasi_p2_fs_write` | `fusedwasi_p2__fs_write` |
| `fusedwasi_p2_ft_ns` | `fusedwasi_p2__ft_ns` |
| `fusedwasi_p2_getopt` | `fusedwasi_p2__getopt` |
| `fusedwasi_p2_handle_or_trap` | `fusedwasi_p2__handle_or_trap` |
| `fusedwasi_p2_handle_result` | `fusedwasi_p2__handle_result` |
| `fusedwasi_p2_hash_result` | `fusedwasi_p2__hash_result` |
| `fusedwasi_p2_hop_get` | `fusedwasi_p2__hop_get` |
| `fusedwasi_p2_hop_set` | `fusedwasi_p2__hop_set` |
| `fusedwasi_p2_http_append` | `fusedwasi_p2__http_append` |
| `fusedwasi_p2_http_build_request` | `fusedwasi_p2__http_build_request` |
| `fusedwasi_p2_http_case` | `fusedwasi_p2__http_case` |
| `fusedwasi_p2_http_case_of` | `fusedwasi_p2__http_case_of` |
| `fusedwasi_p2_http_change` | `fusedwasi_p2__http_change` |
| `fusedwasi_p2_http_check_entry` | `fusedwasi_p2__http_check_entry` |
| `fusedwasi_p2_http_connect` | `fusedwasi_p2__http_connect` |
| `fusedwasi_p2_http_connect_error` | `fusedwasi_p2__http_connect_error` |
| `fusedwasi_p2_http_consume` | `fusedwasi_p2__http_consume` |
| `fusedwasi_p2_http_declared_length` | `fusedwasi_p2__http_declared_length` |
| `fusedwasi_p2_http_destroy_fields` | `fusedwasi_p2__http_destroy_fields` |
| `fusedwasi_p2_http_destroy_future` | `fusedwasi_p2__http_destroy_future` |
| `fusedwasi_p2_http_destroy_future_pollable` | `fusedwasi_p2__http_destroy_future_pollable` |
| `fusedwasi_p2_http_destroy_future_trailers` | `fusedwasi_p2__http_destroy_future_trailers` |
| `fusedwasi_p2_http_destroy_ibody` | `fusedwasi_p2__http_destroy_ibody` |
| `fusedwasi_p2_http_destroy_irequest` | `fusedwasi_p2__http_destroy_irequest` |
| `fusedwasi_p2_http_destroy_iresponse` | `fusedwasi_p2__http_destroy_iresponse` |
| `fusedwasi_p2_http_destroy_obody` | `fusedwasi_p2__http_destroy_obody` |
| `fusedwasi_p2_http_destroy_orequest` | `fusedwasi_p2__http_destroy_orequest` |
| `fusedwasi_p2_http_destroy_oresponse` | `fusedwasi_p2__http_destroy_oresponse` |
| `fusedwasi_p2_http_destroy_outparam` | `fusedwasi_p2__http_destroy_outparam` |
| `fusedwasi_p2_http_dns_error` | `fusedwasi_p2__http_dns_error` |
| `fusedwasi_p2_http_dup` | `fusedwasi_p2__http_dup` |
| `fusedwasi_p2_http_entry` | `fusedwasi_p2__http_entry` |
| `fusedwasi_p2_http_exact` | `fusedwasi_p2__http_exact` |
| `fusedwasi_p2_http_fields_get_obj` | `fusedwasi_p2__http_fields_get_obj` |
| `fusedwasi_p2_http_fields_handle` | `fusedwasi_p2__http_fields_handle` |
| `fusedwasi_p2_http_fields_ref` | `fusedwasi_p2__http_fields_ref` |
| `fusedwasi_p2_http_fields_remove` | `fusedwasi_p2__http_fields_remove` |
| `fusedwasi_p2_http_fields_take` | `fusedwasi_p2__http_fields_take` |
| `fusedwasi_p2_http_future` | `fusedwasi_p2__http_future` |
| `fusedwasi_p2_http_future_advance` | `fusedwasi_p2__http_future_advance` |
| `fusedwasi_p2_http_future_fail` | `fusedwasi_p2__http_future_fail` |
| `fusedwasi_p2_http_future_get` | `fusedwasi_p2__http_future_get` |
| `fusedwasi_p2_http_future_ready` | `fusedwasi_p2__http_future_ready` |
| `fusedwasi_p2_http_future_trailers` | `fusedwasi_p2__http_future_trailers` |
| `fusedwasi_p2_http_future_trailers_get` | `fusedwasi_p2__http_future_trailers_get` |
| `fusedwasi_p2_http_future_unref` | `fusedwasi_p2__http_future_unref` |
| `fusedwasi_p2_http_get_timeout` | `fusedwasi_p2__http_get_timeout` |
| `fusedwasi_p2_http_header_result` | `fusedwasi_p2__http_header_result` |
| `fusedwasi_p2_http_ibody` | `fusedwasi_p2__http_ibody` |
| `fusedwasi_p2_http_ibody_free` | `fusedwasi_p2__http_ibody_free` |
| `fusedwasi_p2_http_ibody_get` | `fusedwasi_p2__http_ibody_get` |
| `fusedwasi_p2_http_irequest` | `fusedwasi_p2__http_irequest` |
| `fusedwasi_p2_http_irequest_get` | `fusedwasi_p2__http_irequest_get` |
| `fusedwasi_p2_http_iresponse` | `fusedwasi_p2__http_iresponse` |
| `fusedwasi_p2_http_iresponse_free` | `fusedwasi_p2__http_iresponse_free` |
| `fusedwasi_p2_http_iresponse_get` | `fusedwasi_p2__http_iresponse_get` |
| `fusedwasi_p2_http_is_hex` | `fusedwasi_p2__http_is_hex` |
| `fusedwasi_p2_http_is_token` | `fusedwasi_p2__http_is_token` |
| `fusedwasi_p2_http_istream` | `fusedwasi_p2__http_istream` |
| `fusedwasi_p2_http_istream_destroy` | `fusedwasi_p2__http_istream_destroy` |
| `fusedwasi_p2_http_leaf` | `fusedwasi_p2__http_leaf` |
| `fusedwasi_p2_http_lift_error` | `fusedwasi_p2__http_lift_error` |
| `fusedwasi_p2_http_lift_method` | `fusedwasi_p2__http_lift_method` |
| `fusedwasi_p2_http_lift_scheme` | `fusedwasi_p2__http_lift_scheme` |
| `fusedwasi_p2_http_line` | `fusedwasi_p2__http_line` |
| `fusedwasi_p2_http_lower` | `fusedwasi_p2__http_lower` |
| `fusedwasi_p2_http_more` | `fusedwasi_p2__http_more` |
| `fusedwasi_p2_http_ms` | `fusedwasi_p2__http_ms` |
| `fusedwasi_p2_http_name_is` | `fusedwasi_p2__http_name_is` |
| `fusedwasi_p2_http_new_body` | `fusedwasi_p2__http_new_body` |
| `fusedwasi_p2_http_obody` | `fusedwasi_p2__http_obody` |
| `fusedwasi_p2_http_obody_get` | `fusedwasi_p2__http_obody_get` |
| `fusedwasi_p2_http_obody_unref` | `fusedwasi_p2__http_obody_unref` |
| `fusedwasi_p2_http_offsets` | `fusedwasi_p2__http_offsets` |
| `fusedwasi_p2_http_options` | `fusedwasi_p2__http_options` |
| `fusedwasi_p2_http_options_get` | `fusedwasi_p2__http_options_get` |
| `fusedwasi_p2_http_orequest` | `fusedwasi_p2__http_orequest` |
| `fusedwasi_p2_http_orequest_free` | `fusedwasi_p2__http_orequest_free` |
| `fusedwasi_p2_http_orequest_get` | `fusedwasi_p2__http_orequest_get` |
| `fusedwasi_p2_http_oresponse` | `fusedwasi_p2__http_oresponse` |
| `fusedwasi_p2_http_oresponse_get` | `fusedwasi_p2__http_oresponse_get` |
| `fusedwasi_p2_http_oresponse_unref` | `fusedwasi_p2__http_oresponse_unref` |
| `fusedwasi_p2_http_ostream` | `fusedwasi_p2__http_ostream` |
| `fusedwasi_p2_http_ostream_check_write` | `fusedwasi_p2__http_ostream_check_write` |
| `fusedwasi_p2_http_ostream_destroy` | `fusedwasi_p2__http_ostream_destroy` |
| `fusedwasi_p2_http_ostream_flush` | `fusedwasi_p2__http_ostream_flush` |
| `fusedwasi_p2_http_ostream_ready` | `fusedwasi_p2__http_ostream_ready` |
| `fusedwasi_p2_http_ostream_write` | `fusedwasi_p2__http_ostream_write` |
| `fusedwasi_p2_http_outparam_release` | `fusedwasi_p2__http_outparam_release` |
| `fusedwasi_p2_http_parse_field` | `fusedwasi_p2__http_parse_field` |
| `fusedwasi_p2_http_payload` | `fusedwasi_p2__http_payload` |
| `fusedwasi_p2_http_payload_free` | `fusedwasi_p2__http_payload_free` |
| `fusedwasi_p2_http_prepare` | `fusedwasi_p2__http_prepare` |
| `fusedwasi_p2_http_read_body` | `fusedwasi_p2__http_read_body` |
| `fusedwasi_p2_http_read_chunked` | `fusedwasi_p2__http_read_chunked` |
| `fusedwasi_p2_http_read_head` | `fusedwasi_p2__http_read_head` |
| `fusedwasi_p2_http_reader` | `fusedwasi_p2__http_reader` |
| `fusedwasi_p2_http_ready_always` | `fusedwasi_p2__http_ready_always` |
| `fusedwasi_p2_http_replace` | `fusedwasi_p2__http_replace` |
| `fusedwasi_p2_http_set_timeout` | `fusedwasi_p2__http_set_timeout` |
| `fusedwasi_p2_http_span` | `fusedwasi_p2__http_span` |
| `fusedwasi_p2_http_split_authority` | `fusedwasi_p2__http_split_authority` |
| `fusedwasi_p2_http_std_methods` | `fusedwasi_p2__http_std_methods` |
| `fusedwasi_p2_http_store_error` | `fusedwasi_p2__http_store_error` |
| `fusedwasi_p2_http_store_method` | `fusedwasi_p2__http_store_method` |
| `fusedwasi_p2_http_store_opt_scheme` | `fusedwasi_p2__http_store_opt_scheme` |
| `fusedwasi_p2_http_store_opt_string` | `fusedwasi_p2__http_store_opt_string` |
| `fusedwasi_p2_http_store_pairs` | `fusedwasi_p2__http_store_pairs` |
| `fusedwasi_p2_http_store_scheme` | `fusedwasi_p2__http_store_scheme` |
| `fusedwasi_p2_http_store_string` | `fusedwasi_p2__http_store_string` |
| `fusedwasi_p2_http_strerror` | `fusedwasi_p2__http_strerror` |
| `fusedwasi_p2_http_token_char` | `fusedwasi_p2__http_token_char` |
| `fusedwasi_p2_http_valid_path` | `fusedwasi_p2__http_valid_path` |
| `fusedwasi_p2_http_valid_scheme` | `fusedwasi_p2__http_valid_scheme` |
| `fusedwasi_p2_http_wait_fd` | `fusedwasi_p2__http_wait_fd` |
| `fusedwasi_p2_incoming_get` | `fusedwasi_p2__incoming_get` |
| `fusedwasi_p2_input_get` | `fusedwasi_p2__input_get` |
| `fusedwasi_p2_input_stream_new` | `fusedwasi_p2__input_stream_new` |
| `fusedwasi_p2_keepalive_get` | `fusedwasi_p2__keepalive_get` |
| `fusedwasi_p2_keepalive_set` | `fusedwasi_p2__keepalive_set` |
| `fusedwasi_p2_load_u16` | `fusedwasi_p2__load_u16` |
| `fusedwasi_p2_load_u32` | `fusedwasi_p2__load_u32` |
| `fusedwasi_p2_load_u8` | `fusedwasi_p2__load_u8` |
| `fusedwasi_p2_mem` | `fusedwasi_p2__mem` |
| `fusedwasi_p2_memory` | `fusedwasi_p2__memory` |
| `fusedwasi_p2_memory_cell` | `fusedwasi_p2__memory_cell` |
| `fusedwasi_p2_memory_input_read` | `fusedwasi_p2__memory_input_read` |
| `fusedwasi_p2_name_chars` | `fusedwasi_p2__name_chars` |
| `fusedwasi_p2_name_cmp` | `fusedwasi_p2__name_cmp` |
| `fusedwasi_p2_name_of` | `fusedwasi_p2__name_of` |
| `fusedwasi_p2_name_ok` | `fusedwasi_p2__name_ok` |
| `fusedwasi_p2_names_free` | `fusedwasi_p2__names_free` |
| `fusedwasi_p2_need` | `fusedwasi_p2__need` |
| `fusedwasi_p2_net_errno` | `fusedwasi_p2__net_errno` |
| `fusedwasi_p2_net_fail` | `fusedwasi_p2__net_fail` |
| `fusedwasi_p2_net_socket` | `fusedwasi_p2__net_socket` |
| `fusedwasi_p2_net_start` | `fusedwasi_p2__net_start` |
| `fusedwasi_p2_net_status` | `fusedwasi_p2__net_status` |
| `fusedwasi_p2_network_get` | `fusedwasi_p2__network_get` |
| `fusedwasi_p2_new_time` | `fusedwasi_p2__new_time` |
| `fusedwasi_p2_ns_ft` | `fusedwasi_p2__ns_ft` |
| `fusedwasi_p2_ns_ts` | `fusedwasi_p2__ns_ts` |
| `fusedwasi_p2_number_result` | `fusedwasi_p2__number_result` |
| `fusedwasi_p2_ok` | `fusedwasi_p2__ok` |
| `fusedwasi_p2_outgoing_get` | `fusedwasi_p2__outgoing_get` |
| `fusedwasi_p2_output_get` | `fusedwasi_p2__output_get` |
| `fusedwasi_p2_output_stream_new` | `fusedwasi_p2__output_stream_new` |
| `fusedwasi_p2_policy` | `fusedwasi_p2__policy` |
| `fusedwasi_p2_poll_fd` | `fusedwasi_p2__poll_fd` |
| `fusedwasi_p2_pollable_ask` | `fusedwasi_p2__pollable_ask` |
| `fusedwasi_p2_pollable_get` | `fusedwasi_p2__pollable_get` |
| `fusedwasi_p2_random_bytes` | `fusedwasi_p2__random_bytes` |
| `fusedwasi_p2_random_fill` | `fusedwasi_p2__random_fill` |
| `fusedwasi_p2_random_u64` | `fusedwasi_p2__random_u64` |
| `fusedwasi_p2_read` | `fusedwasi_p2__read` |
| `fusedwasi_p2_read_addr` | `fusedwasi_p2__read_addr` |
| `fusedwasi_p2_read_list` | `fusedwasi_p2__read_list` |
| `fusedwasi_p2_received` | `fusedwasi_p2__received` |
| `fusedwasi_p2_reparse` | `fusedwasi_p2__reparse` |
| `fusedwasi_p2_resolved` | `fusedwasi_p2__resolved` |
| `fusedwasi_p2_resolved_destroy` | `fusedwasi_p2__resolved_destroy` |
| `fusedwasi_p2_s_check_write` | `fusedwasi_p2__s_check_write` |
| `fusedwasi_p2_s_flush` | `fusedwasi_p2__s_flush` |
| `fusedwasi_p2_s_read` | `fusedwasi_p2__s_read` |
| `fusedwasi_p2_s_write` | `fusedwasi_p2__s_write` |
| `fusedwasi_p2_same_addr` | `fusedwasi_p2__same_addr` |
| `fusedwasi_p2_seconds` | `fusedwasi_p2__seconds` |
| `fusedwasi_p2_set_blocking` | `fusedwasi_p2__set_blocking` |
| `fusedwasi_p2_set_nonblock` | `fusedwasi_p2__set_nonblock` |
| `fusedwasi_p2_setopt` | `fusedwasi_p2__setopt` |
| `fusedwasi_p2_skip` | `fusedwasi_p2__skip` |
| `fusedwasi_p2_slot` | `fusedwasi_p2__slot` |
| `fusedwasi_p2_sock_check_write` | `fusedwasi_p2__sock_check_write` |
| `fusedwasi_p2_sock_fail` | `fusedwasi_p2__sock_fail` |
| `fusedwasi_p2_sock_flush` | `fusedwasi_p2__sock_flush` |
| `fusedwasi_p2_sock_in_ready` | `fusedwasi_p2__sock_in_ready` |
| `fusedwasi_p2_sock_out_ready` | `fusedwasi_p2__sock_out_ready` |
| `fusedwasi_p2_sock_poll` | `fusedwasi_p2__sock_poll` |
| `fusedwasi_p2_sock_poll_destroy` | `fusedwasi_p2__sock_poll_destroy` |
| `fusedwasi_p2_sock_poll_ready` | `fusedwasi_p2__sock_poll_ready` |
| `fusedwasi_p2_sock_pollable` | `fusedwasi_p2__sock_pollable` |
| `fusedwasi_p2_sock_read` | `fusedwasi_p2__sock_read` |
| `fusedwasi_p2_sock_stream` | `fusedwasi_p2__sock_stream` |
| `fusedwasi_p2_sock_stream_destroy` | `fusedwasi_p2__sock_stream_destroy` |
| `fusedwasi_p2_sock_stream_new` | `fusedwasi_p2__sock_stream_new` |
| `fusedwasi_p2_sock_write` | `fusedwasi_p2__sock_write` |
| `fusedwasi_p2_sockerr` | `fusedwasi_p2__sockerr` |
| `fusedwasi_p2_splice` | `fusedwasi_p2__splice` |
| `fusedwasi_p2_ssize` | `fusedwasi_p2__ssize` |
| `fusedwasi_p2_stat` | `fusedwasi_p2__stat` |
| `fusedwasi_p2_stat_at_rc` | `fusedwasi_p2__stat_at_rc` |
| `fusedwasi_p2_stat_from` | `fusedwasi_p2__stat_from` |
| `fusedwasi_p2_status` | `fusedwasi_p2__status` |
| `fusedwasi_p2_std` | `fusedwasi_p2__std` |
| `fusedwasi_p2_store_u16` | `fusedwasi_p2__store_u16` |
| `fusedwasi_p2_store_u32` | `fusedwasi_p2__store_u32` |
| `fusedwasi_p2_store_u64` | `fusedwasi_p2__store_u64` |
| `fusedwasi_p2_store_u8` | `fusedwasi_p2__store_u8` |
| `fusedwasi_p2_str_add` | `fusedwasi_p2__str_add` |
| `fusedwasi_p2_str_set` | `fusedwasi_p2__str_set` |
| `fusedwasi_p2_stream_destroy` | `fusedwasi_p2__stream_destroy` |
| `fusedwasi_p2_stream_err` | `fusedwasi_p2__stream_err` |
| `fusedwasi_p2_stream_free` | `fusedwasi_p2__stream_free` |
| `fusedwasi_p2_stream_of` | `fusedwasi_p2__stream_of` |
| `fusedwasi_p2_stream_pollable` | `fusedwasi_p2__stream_pollable` |
| `fusedwasi_p2_stream_pollable_ready` | `fusedwasi_p2__stream_pollable_ready` |
| `fusedwasi_p2_string` | `fusedwasi_p2__string` |
| `fusedwasi_p2_sys_close` | `fusedwasi_p2__sys_close` |
| `fusedwasi_p2_sys_closefd` | `fusedwasi_p2__sys_closefd` |
| `fusedwasi_p2_sys_dup` | `fusedwasi_p2__sys_dup` |
| `fusedwasi_p2_sys_fstat` | `fusedwasi_p2__sys_fstat` |
| `fusedwasi_p2_sys_fsync` | `fusedwasi_p2__sys_fsync` |
| `fusedwasi_p2_sys_ftruncate` | `fusedwasi_p2__sys_ftruncate` |
| `fusedwasi_p2_sys_link` | `fusedwasi_p2__sys_link` |
| `fusedwasi_p2_sys_list` | `fusedwasi_p2__sys_list` |
| `fusedwasi_p2_sys_lstat` | `fusedwasi_p2__sys_lstat` |
| `fusedwasi_p2_sys_mkdir` | `fusedwasi_p2__sys_mkdir` |
| `fusedwasi_p2_sys_open_at` | `fusedwasi_p2__sys_open_at` |
| `fusedwasi_p2_sys_open_directory` | `fusedwasi_p2__sys_open_directory` |
| `fusedwasi_p2_sys_pread` | `fusedwasi_p2__sys_pread` |
| `fusedwasi_p2_sys_pwrite` | `fusedwasi_p2__sys_pwrite` |
| `fusedwasi_p2_sys_readlink` | `fusedwasi_p2__sys_readlink` |
| `fusedwasi_p2_sys_rename` | `fusedwasi_p2__sys_rename` |
| `fusedwasi_p2_sys_rmdir` | `fusedwasi_p2__sys_rmdir` |
| `fusedwasi_p2_sys_set_times` | `fusedwasi_p2__sys_set_times` |
| `fusedwasi_p2_sys_set_times_fd` | `fusedwasi_p2__sys_set_times_fd` |
| `fusedwasi_p2_sys_size` | `fusedwasi_p2__sys_size` |
| `fusedwasi_p2_sys_symlink` | `fusedwasi_p2__sys_symlink` |
| `fusedwasi_p2_sys_unlink` | `fusedwasi_p2__sys_unlink` |
| `fusedwasi_p2_table_alive` | `fusedwasi_p2__table_alive` |
| `fusedwasi_p2_table_drop` | `fusedwasi_p2__table_drop` |
| `fusedwasi_p2_table_find` | `fusedwasi_p2__table_find` |
| `fusedwasi_p2_table_get` | `fusedwasi_p2__table_get` |
| `fusedwasi_p2_table_new` | `fusedwasi_p2__table_new` |
| `fusedwasi_p2_table_take` | `fusedwasi_p2__table_take` |
| `fusedwasi_p2_target` | `fusedwasi_p2__target` |
| `fusedwasi_p2_target_is_dir` | `fusedwasi_p2__target_is_dir` |
| `fusedwasi_p2_tcp` | `fusedwasi_p2__tcp` |
| `fusedwasi_p2_tcp_destroy` | `fusedwasi_p2__tcp_destroy` |
| `fusedwasi_p2_tcp_get` | `fusedwasi_p2__tcp_get` |
| `fusedwasi_p2_tcp_make` | `fusedwasi_p2__tcp_make` |
| `fusedwasi_p2_tcp_open` | `fusedwasi_p2__tcp_open` |
| `fusedwasi_p2_tcp_unref` | `fusedwasi_p2__tcp_unref` |
| `fusedwasi_p2_terminal` | `fusedwasi_p2__terminal` |
| `fusedwasi_p2_times` | `fusedwasi_p2__times` |
| `fusedwasi_p2_times_of_abi` | `fusedwasi_p2__times_of_abi` |
| `fusedwasi_p2_to_sockaddr` | `fusedwasi_p2__to_sockaddr` |
| `fusedwasi_p2_ts_ns` | `fusedwasi_p2__ts_ns` |
| `fusedwasi_p2_type_of` | `fusedwasi_p2__type_of` |
| `fusedwasi_p2_udp` | `fusedwasi_p2__udp` |
| `fusedwasi_p2_udp_destroy` | `fusedwasi_p2__udp_destroy` |
| `fusedwasi_p2_udp_get` | `fusedwasi_p2__udp_get` |
| `fusedwasi_p2_udp_open` | `fusedwasi_p2__udp_open` |
| `fusedwasi_p2_udp_unref` | `fusedwasi_p2__udp_unref` |
| `fusedwasi_p2_utf8` | `fusedwasi_p2__utf8` |
| `fusedwasi_p2_walk` | `fusedwasi_p2__walk` |
| `fusedwasi_p2_walk_close` | `fusedwasi_p2__walk_close` |
| `fusedwasi_p2_walk_path_with` | `fusedwasi_p2__walk_path_with` |
| `fusedwasi_p2_wide` | `fusedwasi_p2__wide` |
| `fusedwasi_p2_win_code` | `fusedwasi_p2__win_code` |
| `fusedwasi_p2_win_is_link` | `fusedwasi_p2__win_is_link` |
| `fusedwasi_p2_win_last` | `fusedwasi_p2__win_last` |
| `fusedwasi_p2_win_open` | `fusedwasi_p2__win_open` |
| `fusedwasi_p2_win_set_times_path` | `fusedwasi_p2__win_set_times_path` |
| `fusedwasi_p2_win_stat_handle` | `fusedwasi_p2__win_stat_handle` |
| `fusedwasi_p2_win_stat_path` | `fusedwasi_p2__win_stat_path` |
| `fusedwasi_p2_write` | `fusedwasi_p2__write` |
| `fusedwasi_p2_write_addr` | `fusedwasi_p2__write_addr` |
| `fusedwasi_p2_write_all_and_flush` | `fusedwasi_p2__write_all_and_flush` |
| `fusedwasi_p2_write_stat` | `fusedwasi_p2__write_stat` |
| `fusedwasi_p2_wsa_errno` | `fusedwasi_p2__wsa_errno` |

## C: fields of public structs

| Before | Now |
| --- | --- |
| `fusedwasi_p2_state.slots` | `fusedwasi_p2_state.slots__` |
| `fusedwasi_p2_state.slot_count` | `fusedwasi_p2_state.slot_count__` |
| `fusedwasi_p2_state.slot_capacity` | `fusedwasi_p2_state.slot_capacity__` |
| `fusedwasi_p2_state.std_default` | `fusedwasi_p2_state.std_default__` |
| `fusedwasi_p2_state.p1_table` | `fusedwasi_p2_state.p1_table__` |
| `fusedwasi_p2_guest.memory` | `fusedwasi_p2_guest.memory__` |
| `fusedwasi_p2_guest.memory_cell` | `fusedwasi_p2_guest.memory_cell__` |
| `fusedwasi_p2_guest.memories` | `fusedwasi_p2_guest.memories__` |
| `fusedwasi_p2_stream.dead` | `fusedwasi_p2_stream.dead__` |

## Python

| Module | Before | Now |
| --- | --- | --- |
| `fusedwasi.p1.wasi_p1` | `INT64_MAX` | `_INT64_MAX` |
| `fusedwasi.p1.wasi_p1` | `LINK_MAX` | `_LINK_MAX` |
| `fusedwasi.p1.wasi_p1` | `PATH_MAX` | `_PATH_MAX` |
| `fusedwasi.p1.wasi_p1` | `filestat_of_fd` | `_filestat_of_fd` |
| `fusedwasi.p1.wasi_p1` | `filestat_of_path` | `_filestat_of_path` |
| `fusedwasi.p1.wasi_p1_core` | `Entry` | `_Entry` |
| `fusedwasi.p1.wasi_p1_core` | `Fds` | `_Fds` |
| `fusedwasi.p1.wasi_p1_core` | `K_DIR` | `_K_DIR` |
| `fusedwasi.p1.wasi_p1_core` | `K_FILE` | `_K_FILE` |
| `fusedwasi.p1.wasi_p1_core` | `K_NONE` | `_K_NONE` |
| `fusedwasi.p1.wasi_p1_core` | `K_STD` | `_K_STD` |
| `fusedwasi.p1.wasi_p1_core` | `Listing` | `_Listing` |
| `fusedwasi.p1.wasi_p1_core` | `Stat` | `_Stat` |
| `fusedwasi.p1.wasi_p1_core` | `descriptor_of` | `_descriptor_of` |
| `fusedwasi.p1.wasi_p1_core` | `errno_of_error` | `_errno_of_error` |
| `fusedwasi.p1.wasi_p1_core` | `fds_of` | `_fds_of` |
| `fusedwasi.p1.wasi_p1_core` | `filetype_of` | `_filetype_of` |
| `fusedwasi.p1.wasi_p1_core` | `get_u32` | `_get_u32` |
| `fusedwasi.p1.wasi_p1_core` | `in_memory` | `_in_memory` |
| `fusedwasi.p1.wasi_p1_core` | `iov` | `_iov` |
| `fusedwasi.p1.wasi_p1_core` | `iovs_valid` | `_iovs_valid` |
| `fusedwasi.p1.wasi_p1_core` | `load` | `_load` |
| `fusedwasi.p1.wasi_p1_core` | `mem_size` | `_mem_size` |
| `fusedwasi.p1.wasi_p1_core` | `path_at` | `_path_at` |
| `fusedwasi.p1.wasi_p1_core` | `put` | `_put` |
| `fusedwasi.p1.wasi_p1_core` | `put_bytes` | `_put_bytes` |
| `fusedwasi.p1.wasi_p1_core` | `put_u16` | `_put_u16` |
| `fusedwasi.p1.wasi_p1_core` | `put_u32` | `_put_u32` |
| `fusedwasi.p1.wasi_p1_core` | `put_u64` | `_put_u64` |
| `fusedwasi.p1.wasi_p1_core` | `put_u8` | `_put_u8` |
| `fusedwasi.p1.wasi_p1_core` | `read_bytes` | `_read_bytes` |
| `fusedwasi.p1.wasi_p1_core` | `read_std` | `_read_std` |
| `fusedwasi.p1.wasi_p1_core` | `release` | `_release` |
| `fusedwasi.p1.wasi_p1_core` | `std_filetype` | `_std_filetype` |
| `fusedwasi.p1.wasi_p1_core` | `std_is_real` | `_std_is_real` |
| `fusedwasi.p1.wasi_p1_core` | `std_stream` | `_std_stream` |
| `fusedwasi.p1.wasi_p1_core` | `store` | `_store` |
| `fusedwasi.p1.wasi_p1_core` | `stream_status` | `_stream_status` |
| `fusedwasi.p1.wasi_p1_core` | `write_std` | `_write_std` |
| `fusedwasi.p2.wasi_p2_cli` | `CHUNK` | `_CHUNK` |
| `fusedwasi.p2.wasi_p2_cli` | `FdInput` | `_FdInput` |
| `fusedwasi.p2.wasi_p2_cli` | `FdOutput` | `_FdOutput` |
| `fusedwasi.p2.wasi_p2_cli` | `SysInput` | `_SysInput` |
| `fusedwasi.p2.wasi_p2_cli` | `SysOutput` | `_SysOutput` |
| `fusedwasi.p2.wasi_p2_core` | `GENERATION_MASK` | `_GENERATION_MASK` |
| `fusedwasi.p2.wasi_p2_core` | `INDEX_BITS` | `_INDEX_BITS` |
| `fusedwasi.p2.wasi_p2_core` | `INDEX_MASK` | `_INDEX_MASK` |
| `fusedwasi.p2.wasi_p2_core` | `Slot` | `_Slot` |
| `fusedwasi.p2.wasi_p2_core` | `alloc` | `_alloc` |
| `fusedwasi.p2.wasi_p2_core` | `load_bytes` | `_load_bytes` |
| `fusedwasi.p2.wasi_p2_core` | `load_u16` | `_load_u16` |
| `fusedwasi.p2.wasi_p2_core` | `load_u32` | `_load_u32` |
| `fusedwasi.p2.wasi_p2_core` | `load_u8` | `_load_u8` |
| `fusedwasi.p2.wasi_p2_core` | `store_bytes` | `_store_bytes` |
| `fusedwasi.p2.wasi_p2_core` | `store_u16` | `_store_u16` |
| `fusedwasi.p2.wasi_p2_core` | `store_u32` | `_store_u32` |
| `fusedwasi.p2.wasi_p2_core` | `store_u64` | `_store_u64` |
| `fusedwasi.p2.wasi_p2_core` | `store_u8` | `_store_u8` |
| `fusedwasi.p2.wasi_p2_core` | `table_alive` | `_table_alive` |
| `fusedwasi.p2.wasi_p2_core` | `table_drop` | `_table_drop` |
| `fusedwasi.p2.wasi_p2_core` | `table_get` | `_table_get` |
| `fusedwasi.p2.wasi_p2_core` | `table_new` | `_table_new` |
| `fusedwasi.p2.wasi_p2_core` | `trap` | `_trap` |
| `fusedwasi.p2.wasi_p2_filesystem` | `DirectoryEntries` | `_DirectoryEntries` |
| `fusedwasi.p2.wasi_p2_filesystem` | `ERROR_CODES` | `_ERROR_CODES` |
| `fusedwasi.p2.wasi_p2_filesystem` | `FileInput` | `_FileInput` |
| `fusedwasi.p2.wasi_p2_filesystem` | `FileOutput` | `_FileOutput` |
| `fusedwasi.p2.wasi_p2_filesystem` | `MAX_DEPTH` | `_MAX_DEPTH` |
| `fusedwasi.p2.wasi_p2_filesystem` | `MAX_LINKS` | `_MAX_LINKS` |
| `fusedwasi.p2.wasi_p2_filesystem` | `NAME_MAX` | `_NAME_MAX` |
| `fusedwasi.p2.wasi_p2_filesystem` | `Ref` | `_Ref` |
| `fusedwasi.p2.wasi_p2_filesystem` | `STREAM_CHUNK` | `_STREAM_CHUNK` |
| `fusedwasi.p2.wasi_p2_http` | `CASES` | `_CASES` |
| `fusedwasi.p2.wasi_p2_http` | `DEFAULT_TIMEOUT_NS` | `_DEFAULT_TIMEOUT_NS` |
| `fusedwasi.p2.wasi_p2_http` | `FORBIDDEN` | `_FORBIDDEN` |
| `fusedwasi.p2.wasi_p2_http` | `FORBIDDEN_HEADER` | `_FORBIDDEN_HEADER` |
| `fusedwasi.p2.wasi_p2_http` | `FutureIncomingResponse` | `_FutureIncomingResponse` |
| `fusedwasi.p2.wasi_p2_http` | `FuturePollable` | `_FuturePollable` |
| `fusedwasi.p2.wasi_p2_http` | `FutureTrailers` | `_FutureTrailers` |
| `fusedwasi.p2.wasi_p2_http` | `IMMUTABLE` | `_IMMUTABLE` |
| `fusedwasi.p2.wasi_p2_http` | `INVALID_SYNTAX` | `_INVALID_SYNTAX` |
| `fusedwasi.p2.wasi_p2_http` | `IncomingBody` | `_IncomingBody` |
| `fusedwasi.p2.wasi_p2_http` | `IncomingRequest` | `_IncomingRequest` |
| `fusedwasi.p2.wasi_p2_http` | `IncomingResponse` | `_IncomingResponse` |
| `fusedwasi.p2.wasi_p2_http` | `OutgoingBody` | `_OutgoingBody` |
| `fusedwasi.p2.wasi_p2_http` | `OutgoingRequest` | `_OutgoingRequest` |
| `fusedwasi.p2.wasi_p2_http` | `OutgoingResponse` | `_OutgoingResponse` |
| `fusedwasi.p2.wasi_p2_http` | `ReadyPollable` | `_ReadyPollable` |
| `fusedwasi.p2.wasi_p2_http` | `RequestOptions` | `_RequestOptions` |
| `fusedwasi.p2.wasi_p2_http` | `STD_METHODS` | `_STD_METHODS` |
| `fusedwasi.p2.wasi_p2_http` | `build_request` | `_build_request` |
| `fusedwasi.p2.wasi_p2_http` | `lift_error_code_flat` | `_lift_error_code_flat` |
| `fusedwasi.p2.wasi_p2_http` | `split_authority` | `_split_authority` |
| `fusedwasi.p2.wasi_p2_http` | `store_error_code` | `_store_error_code` |
| `fusedwasi.p2.wasi_p2_io` | `Error` | `_Error` |
| `fusedwasi.p2.wasi_p2_io` | `READ_CAP` | `_READ_CAP` |
| `fusedwasi.p2.wasi_p2_io` | `error_new` | `_error_new` |
| `fusedwasi.p2.wasi_p2_io` | `input_stream_new` | `_input_stream_new` |
| `fusedwasi.p2.wasi_p2_io` | `output_stream_new` | `_output_stream_new` |
| `fusedwasi.p2.wasi_p2_random` | `MAX_BYTES` | `_MAX_BYTES` |
| `fusedwasi.p2.wasi_p2_sockets` | `Addr` | `_Addr` |
| `fusedwasi.p2.wasi_p2_sockets` | `BACKLOG_MAX` | `_BACKLOG_MAX` |
| `fusedwasi.p2.wasi_p2_sockets` | `BIND_IN_PROGRESS` | `_BIND_IN_PROGRESS` |
| `fusedwasi.p2.wasi_p2_sockets` | `BOUND` | `_BOUND` |
| `fusedwasi.p2.wasi_p2_sockets` | `CHECK_SEND_PERMIT` | `_CHECK_SEND_PERMIT` |
| `fusedwasi.p2.wasi_p2_sockets` | `CHUNK` | `_CHUNK` |
| `fusedwasi.p2.wasi_p2_sockets` | `CLOSED` | `_CLOSED` |
| `fusedwasi.p2.wasi_p2_sockets` | `CONNECTED` | `_CONNECTED` |
| `fusedwasi.p2.wasi_p2_sockets` | `CONNECT_IN_PROGRESS` | `_CONNECT_IN_PROGRESS` |
| `fusedwasi.p2.wasi_p2_sockets` | `IncomingDatagrams` | `_IncomingDatagrams` |
| `fusedwasi.p2.wasi_p2_sockets` | `LISTENING` | `_LISTENING` |
| `fusedwasi.p2.wasi_p2_sockets` | `LISTEN_IN_PROGRESS` | `_LISTEN_IN_PROGRESS` |
| `fusedwasi.p2.wasi_p2_sockets` | `NETWORK_ERRORS` | `_NETWORK_ERRORS` |
| `fusedwasi.p2.wasi_p2_sockets` | `NetError` | `_NetError` |
| `fusedwasi.p2.wasi_p2_sockets` | `Network` | `_Network` |
| `fusedwasi.p2.wasi_p2_sockets` | `OutgoingDatagrams` | `_OutgoingDatagrams` |
| `fusedwasi.p2.wasi_p2_sockets` | `Resolved` | `_Resolved` |
| `fusedwasi.p2.wasi_p2_sockets` | `SocketInput` | `_SocketInput` |
| `fusedwasi.p2.wasi_p2_sockets` | `SocketOutput` | `_SocketOutput` |
| `fusedwasi.p2.wasi_p2_sockets` | `SocketPollable` | `_SocketPollable` |
| `fusedwasi.p2.wasi_p2_sockets` | `TcpSock` | `_TcpSock` |
| `fusedwasi.p2.wasi_p2_sockets` | `UNBOUND` | `_UNBOUND` |
| `fusedwasi.p2.wasi_p2_sockets` | `UdpSock` | `_UdpSock` |
| `fusedwasi.signatures` | `P1_MODULE` | `_P1_MODULE` |
| `fusedwasi.p2.State` | `slots` | `_slots` |
| `fusedwasi.p2.State` | `p1` | `_p1` |
| `fusedwasi.p2.Guest` | `memories` | `_memories` |
| `fusedwasi.signatures` | `P1_MODULE` | `_P1_MODULE` |
