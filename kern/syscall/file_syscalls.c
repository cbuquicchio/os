#include <types.h>
#include <copyinout.h>
#include <current.h>
#include <filetable.h>
#include <limits.h>
#include <synch.h>
#include <syscall.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <kern/seek.h>		/* Contains the codes for lseek whence */
#include <kern/stat.h>

int sys_open(userptr_t filename, int flags, int *retval)
{
	KASSERT(curthread->t_filetable != NULL);

	int res;
	char *kfname;
	struct filehandle *fh;

	if (filename == NULL)
		return EFAULT;

	kfname = kmalloc(sizeof(*kfname) * PATH_MAX);
	if (kfname == NULL) {
		return ENOMEM;
	}

	res = copyinstr(filename, kfname, PATH_MAX, NULL);
	if (res) {
		kfree(kfname);
		return res;
	}

	fh = filehandle_create(flags);
	if (fh == NULL) {
		kfree(kfname);
		return -1;
	}

	res = vfs_open(kfname, flags, 0, &fh->vn);
	if (res) {
		filehandle_destroy(fh);
		kfree(kfname);
		return res;
	}

	res = filetable_insert(fh, curthread->t_filetable);
	if (res < 0) {
		filehandle_destroy(fh);
		kfree(kfname);
		return EMFILE;
	}

	*retval = res;

	return 0;
}

int sys_close(int fd)
{
	KASSERT(curthread->t_filetable != NULL);

	int err = 0;
	struct filehandle *fh;

	if (fd < 0 || fd >= OPEN_MAX)
		return EBADF;

	fh = filetable_remove(fd, curthread->t_filetable);
	if (fh == NULL)
		return EBADF;

	lock_acquire(fh->fh_lk);
	vfs_close(fh->vn);
	lock_release(fh->fh_lk);

	filehandle_destroy(fh);

	return err;
}

static int readwrite_check(int fd, userptr_t buf)
{
	int res = 0;

	if (fd < 0 || fd >= OPEN_MAX)	/* Invaid file descriptor */
		res = EBADF;

	if (buf == NULL)	/* Invalid userspace address */
		res = EFAULT;

	return res;
}

int sys_read(int fd, userptr_t buf, size_t nbytes, int *retval)
{
	KASSERT(curthread->t_filetable != NULL);

	int res;
	void *kbuf;
	struct filehandle *fh;
	struct uio block;
	struct iovec vec;

	res = readwrite_check(fd, buf);
	if (res)
		return res;

	lock_acquire(curthread->t_filetable->lk);
	fh = filetable_lookup(fd, curthread->t_filetable);
	lock_release(curthread->t_filetable->lk);

	if (fh == NULL)		/* File not found in table */
		return EBADF;

	lock_acquire(fh->fh_lk);
	if (fh->flag == O_WRONLY) {	/* Cannot read, file is writeonly */
		lock_release(fh->fh_lk);
		return EBADF;
	}

	kbuf = kmalloc(nbytes);
	if (kbuf == NULL) {
		lock_release(fh->fh_lk);
		return ENOMEM;
	}

	/* Init uio memory block abstration */
	uio_kinit(&vec, &block, kbuf, nbytes, fh->offset, UIO_READ);

	res = VOP_READ(fh->vn, &block);
	if (res) {
		lock_release(fh->fh_lk);
		kfree(kbuf);
		return res;
	}

	res = copyout(kbuf, buf, nbytes);
	if (res) {
		lock_release(fh->fh_lk);
		kfree(kbuf);
		return res;
	}

	*retval = nbytes - block.uio_resid;
	fh->offset += (off_t) (*retval);

	lock_release(fh->fh_lk);
	kfree(kbuf);

	return 0;
}

int sys_write(int fd, userptr_t buf, size_t nbytes, int *retval)
{
	KASSERT(curthread->t_filetable != NULL);

	int res;		/* Used for responses to function calls */
	void *kbuf;		/* Kernel buffer to copy user buffer to */
	struct filehandle *fh;
	struct uio block;	/* Memory block abstraction */
	struct iovec vec;

	res = readwrite_check(fd, buf);
	if (res)
		return res;

	lock_acquire(curthread->t_filetable->lk);
	fh = filetable_lookup(fd, curthread->t_filetable);
	lock_release(curthread->t_filetable->lk);

	/* Check to see if file was not found in filetable */
	if (fh == NULL)
		return EBADF;

	lock_acquire(fh->fh_lk);

	/* Cannot write to readonly file */
	if (fh->flag == O_RDONLY) {
		lock_release(fh->fh_lk);
		return EBADF;
	}

	kbuf = kmalloc(nbytes);
	if (kbuf == NULL) {
		lock_release(fh->fh_lk);
		return ENOMEM;
	}

	res = copyin(buf, kbuf, nbytes);
	if (res) {
		lock_release(fh->fh_lk);
		kfree(kbuf);
		return res;
	}

	/* Initialze the memory block data structure */
	uio_kinit(&vec, &block, kbuf, nbytes, fh->offset, UIO_WRITE);

	res = VOP_WRITE(fh->vn, &block);
	if (res) {
		lock_release(fh->fh_lk);
		kfree(kbuf);
		return res;
	}

	*retval = nbytes - block.uio_resid;	/* # of bytes attempted -
						   # left to write */

	fh->offset += (off_t) (*retval);	/* Update the offset */

	lock_release(fh->fh_lk);
	kfree(kbuf);

	return 0;
}

int sys_lseek(int fd, off_t pos, userptr_t whence, int *retval_hi,
	      int *retval_lo)
{
	KASSERT(curthread->t_filetable != NULL);

	int err;
	off_t newpos;
	struct filehandle *fh;
	struct stat vnstats;
	int kwhence = 0;

	if (fd < 0 || fd >= OPEN_MAX)
		return EBADF;

	err = copyin(whence, &kwhence, sizeof(int32_t));
	if (err)
		return err;

	lock_acquire(curthread->t_filetable->lk);
	fh = filetable_lookup(fd, curthread->t_filetable);
	lock_release(curthread->t_filetable->lk);
	if (fh == NULL)
		return EBADF;

	lock_acquire(fh->fh_lk);

	if (!VOP_ISSEEKABLE(fh->vn)) {
		lock_release(fh->fh_lk);
		return ESPIPE;
	}

	err = VOP_STAT(fh->vn, &vnstats);
	if (err) {
		lock_release(fh->fh_lk);
		return err;
	}

	/*
	 * This checks for both valid position as well as a valid value for
	 * whence. The default case is the case where whence is not a valid
	 * option.
	 */
	switch (kwhence) {
	case SEEK_SET:
		newpos = pos;
		break;

	case SEEK_CUR:
		newpos = pos + fh->offset;
		break;

	case SEEK_END:
		newpos = pos + vnstats.st_size;
		break;

	default:
		newpos = -1;
	}

	if (newpos < 0) {
		lock_release(fh->fh_lk);
		return EINVAL;
	}

	fh->offset = newpos;
	lock_release(fh->fh_lk);

	*retval_lo = (uint32_t) (newpos & 0xFFFFFFFF);	/* low bits */
	*retval_hi = (uint32_t) (newpos >> 32);	/* high bits */

	return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval)
{
	KASSERT(curthread->t_filetable != NULL);

	if (oldfd < 0 || newfd < 0 || oldfd >= OPEN_MAX || newfd >= OPEN_MAX)
		return EBADF;

	int res;
	struct filehandle *oldfh;
	struct filehandle *newfh;

	lock_acquire(curthread->t_filetable->lk);
	oldfh = filetable_lookup(oldfd, curthread->t_filetable);
	lock_release(curthread->t_filetable->lk);

	if (oldfh == NULL) {
		return EBADF;
	}

	lock_acquire(oldfh->fh_lk);
	newfh = filehandle_create(oldfh->flag);
	if (newfh == NULL) {
		lock_release(oldfh->fh_lk);
		return ENOMEM;
	}

	newfh->offset = oldfh->offset;
	newfh->vn = oldfh->vn;
	lock_release(oldfh->fh_lk);

	res = filetable_insert(newfh, curthread->t_filetable);
	KASSERT(res < OPEN_MAX);
	if (res < 0) {
		return EMFILE;
	}

	*retval = res;

	return 0;
}

int sys___getcwd(userptr_t buf, size_t nbytes, int *retval)
{
	if (buf == NULL)
		return EFAULT;

	int err;
	size_t kbytes;
	char *kbuf;
	struct uio memblock;
	struct iovec vec;

	kbuf = kmalloc(nbytes);
	if (kbuf == NULL)
		return ENOMEM;

	/* Initialize memblock */
	uio_kinit(&vec, &memblock, kbuf, nbytes, 0, UIO_READ);

	err = copyoutstr(kbuf, buf, nbytes, &kbytes);
	if (err) {
		kfree(kbuf);
		return err;
	}

	kfree(kbuf);
	*retval = kbytes;

	return 0;
}
