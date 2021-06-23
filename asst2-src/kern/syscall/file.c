#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <proc.h>

/*
 * Add your file-related functions here ...
 */
int sys_open(userptr_t filename, int flags, mode_t mode, int *retval) {
    // Copy in and check filename
    char *filename_internal = kmalloc(NAME_MAX * sizeof(char));
    if (filename_internal == NULL)
        return ENOMEM;
    int result = copyinstr(filename, filename_internal, NAME_MAX, NULL);
    if (result) {
        kfree(filename_internal);
        return result;
    }
    // After checking, use function that trusts file names.
    result = open_trust(filename_internal, flags, mode, retval);
    kfree(filename_internal);
    return result;
}

int open_trust(char *filename, int flags, mode_t mode, int *retval) {
    // Open and get vnode
    struct vnode *file_vnode;
    int result = vfs_open(filename , flags, mode, &file_vnode);
    if (result) {
        return result;
    }
    // Find free entry in open file table
    struct file_info **position;
    bool exist = false;
    for (int i = 0; i < TABLE_MAX; i++) {
        if (of_table[i] == NULL) {
            exist = true;
            of_table[i] = kmalloc(sizeof(struct file_info));
            if (of_table[i] == NULL) {
                vfs_close(file_vnode);
                return ENOMEM;
            }
            of_table[i]->file_vnode = file_vnode;
            of_table[i]->file_refcount = 1;
            of_table[i]->file_flag = flags;
            // Files open in append mode
            if (flags == O_APPEND) {
                struct stat info;
                result = VOP_STAT(file_vnode, &info);
                if (result) {
                    vfs_close(file_vnode);
                    kfree(of_table[i]);
                    return result;
                }
                of_table[i]->file_offset = info.st_size;
            } else {
                of_table[i]->file_offset = 0;
            }
            position = &(of_table[i]);
            break;
        }
    }
    // No free entry in of table
    if (exist == false) {
        vfs_close(file_vnode);
        return ENFILE;
    }
    // Find free entry in file descriptor table
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->fd_table[i] == NULL) {
            // fd number
            *retval = i;
            // of table position
            curproc->fd_table[i] = position;
            return 0;
        }
    }
    // No free entry in fd table
    vfs_close(file_vnode);
    kfree(*position);
    return EMFILE;

}

int sys_close(int fd, int *retval) {
    //check fd is a valid file handle or not
    if (fd < 0 || fd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }
    if (curproc->fd_table[fd] == NULL) {
        *retval = -1;
        return EBADF;
    }
    struct file_info *file  = *(curproc->fd_table[fd]);
    // at least 2 reference count exsists after sys_open
    if (file->file_refcount > 1) {
        file->file_refcount--;
    }
    // the only reference
    else {
        vfs_close(file->file_vnode);
        kfree(file);
        *(curproc->fd_table[fd]) = NULL;
    }
    curproc->fd_table[fd] = NULL;
    *retval = 0;
    return 0;
}


ssize_t sys_read(int fd, void *buf, size_t buflen, int *retval) {
    //check fd is a valid file handle or not
    if (fd < 0 || fd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }
    if (curproc->fd_table[fd] == NULL) {
        *retval = -1;
        return EBADF;
    }
    struct file_info *file  = *(curproc->fd_table[fd]);
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u_io = kmalloc(sizeof(struct uio));
    void *safe_buf[buflen];
    int result;

    if (file->file_flag == O_WRONLY) {
        *retval = -1;
        return EBADF;
    }

    /* initialize a uio */
    uio_kinit(iov, u_io, safe_buf, buflen, file->file_offset, UIO_READ);

    result = VOP_READ(file->file_vnode, u_io);
    if (result) {
        *retval = -1;
        return result;
    }

    *retval = u_io->uio_offset - file->file_offset;
    file->file_offset = u_io->uio_offset;
    kfree(iov);
    kfree(u_io);
    if (*retval == 0)
        return 0;
    result = copyout(safe_buf, (userptr_t)buf, buflen);
    if (result) {
        return result;
    }

    return 0;
}

ssize_t sys_write(int fd, const void *buf, size_t buflen, int *retval) {
    //check fd is a valid file handle or not
    if (fd < 0 || fd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }
    if (curproc->fd_table[fd] == NULL) {
        *retval = -1;
        return EBADF;
    }
    struct file_info *file  = *(curproc->fd_table[fd]);
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u_io = kmalloc(sizeof(struct uio));
    void *safe_buf[buflen];
    int result;

    if (file->file_flag == O_RDONLY) {
        *retval = -1;
        return EBADF;
    }

    result = copyin((userptr_t)buf, safe_buf, buflen);
    if (result) {
        *retval = -1;
        return result;
    }

    /* initialize a uio */
    uio_kinit(iov, u_io, safe_buf, buflen, file->file_offset, UIO_WRITE);

    result = VOP_WRITE(file->file_vnode, u_io);
    if (result) {
        *retval = -1;
        return result;
    }

    *retval = u_io->uio_offset - file->file_offset;
    file->file_offset = u_io->uio_offset;

    kfree(iov);
    kfree(u_io);

    return 0;
}


int sys_dup2(int oldfd, int newfd, int *retval) {
    //check fd is a valid file handle or not
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }
    if (curproc->fd_table[oldfd] == NULL) {
        *retval = -1;
        return EBADF;
    }
    *retval = newfd;
    if (oldfd == newfd) {
        return 0;
    }
    // Close opened file
    if (curproc->fd_table[newfd] != NULL) {
        int retval;
        sys_close(newfd, &retval);
        KASSERT(retval == 0);
    }
    struct file_info *file  = *(curproc->fd_table[oldfd]);
    // Copy pointer to new fd
    curproc->fd_table[newfd] = curproc->fd_table[oldfd];
    file->file_refcount++;
    return 0;
}



off_t sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
    //check fd is a valid file handle or not
    if (fd < 0 || fd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }
    if (curproc->fd_table[fd] == NULL) {
        *retval = -1;
        return EBADF;
    }
    struct file_info *file  = *(curproc->fd_table[fd]);
    // Check if the file is seekable
    if (!VOP_ISSEEKABLE(file->file_vnode)) {
        *retval = -1;
        return ESPIPE;
    }
    off_t first_off = file->file_offset;
    // Start position in file
    if (whence == SEEK_SET) {
        file->file_offset = pos;
    } else if (whence == SEEK_CUR) {
        file->file_offset += pos;
    } else if (whence == SEEK_END) {
        struct stat info;
        int result = VOP_STAT(file->file_vnode, &info);
        if (result) {
            return result;
        }
        file->file_offset = info.st_size + pos;
    } else {
        // invalid whence
        *retval = -1;
        return EINVAL;
    }
    // check if offset is negative
    if (file->file_offset < 0) {
        file->file_offset = first_off;
        *retval = -1;
        return EINVAL;
    }
    *retval = file->file_offset;
    return 0;
}
