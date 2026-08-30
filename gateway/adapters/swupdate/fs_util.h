#ifndef FS_UTIL_H
#define FS_UTIL_H

#include <stddef.h>

int fs_write_all(int fd, const void *buffer, size_t length);
int fs_rename_noreplace(int old_dir_fd, const char *old_name, int new_dir_fd,
                        const char *new_name);

#endif
