/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_
/*
 * Contains some file-related maximum length constants
 */
#include <vnode.h>
#include <limits.h>
#include <synch.h>
#define TABLE_MAX (10 * OPEN_MAX)
/*
 * Put your function declarations and data types here ...
 */
struct file_info {
    struct vnode *file_vnode;
    off_t file_offset;
    int file_refcount;
    int file_flag;
};
// Global open file table
struct file_info **of_table;

int open_trust(char *filename, int flags, mode_t mode, int *retval);
int sys_open(userptr_t filename, int flags, mode_t mode,int *retval);
int sys_close(int fd, int *retval);
ssize_t sys_read(int fd, void *buf, size_t buflen, int *retval);
ssize_t sys_write(int fd, const void *buf, size_t buflen, int *retval);
int sys_dup2(int oldfd, int newfd, int *retval);
off_t sys_lseek(int fd, off_t pos, int whence, off_t *retval);



#endif /* _FILE_H_ */
