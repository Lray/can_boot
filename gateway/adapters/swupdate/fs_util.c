#include "fs_util.h"

#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int fs_write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = (const unsigned char *)buffer;
    size_t remaining = length;

    while (remaining > 0u)
    {
        ssize_t count = write(fd, cursor, remaining);

        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            if (count == 0)
            {
                errno = EIO;
            }
            return -1;
        }
        cursor += (size_t)count;
        remaining -= (size_t)count;
    }
    return 0;
}

int fs_rename_noreplace(int old_dir_fd, const char *old_name, int new_dir_fd,
                        const char *new_name)
{
#if defined(SYS_renameat2)
    return (int)syscall(SYS_renameat2, old_dir_fd, old_name, new_dir_fd, new_name,
                        RENAME_NOREPLACE);
#else
    (void)old_dir_fd;
    (void)old_name;
    (void)new_dir_fd;
    (void)new_name;
    errno = ENOSYS;
    return -1;
#endif
}
