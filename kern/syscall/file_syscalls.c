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

int sys_write(int fd, userptr_t buf, size_t nbytes, int *retval)
{
	KASSERT(curthread->t_filetable != NULL);

	int res;		/* Used for responses to function calls */
	void *kbuf;		/* Kernel buffer to copy user buffer to */
	struct filehandle *fh;
	struct uio block;	/* Memory block abstraction */
	struct iovec vec;

	/* Check if file descriptor is valid */
	if (fd < 0 || fd >= OPEN_MAX)
		return EBADF;

	/* Check for valid usersapce address */
	if (buf == NULL)
		return EFAULT;

	fh = filetable_lookup(fd, curthread->t_filetable);

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
