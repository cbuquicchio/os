#include <types.h>
#include <lib.h>
#include <filetable.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>		/* defines the max open files */
#include <synch.h>
#include <vfs.h>
#include <vnode.h>

struct filehandle *filehandle_create(int flag)
{
	/* Also assert valid flags */
	struct filehandle *fh;

	fh = kmalloc(sizeof(*fh));
	if (fh == NULL) {
		return NULL;
	}

	fh->fh_lk = lock_create("file handle");
	if (fh->fh_lk == NULL) {
		kfree(fh);
		return NULL;
	}

	fh->vn = NULL;
	fh->offset = 0;
	fh->refcount = 1;
	fh->flag = flag;

	return fh;
}

void filehandle_cleanup(struct filehandle *fh)
{
	KASSERT(fh != NULL);

	lock_acquire(fh->fh_lk);

	KASSERT(fh->refcount > 0);
	fh->refcount--;

	if (fh->refcount > 0) {
		lock_release(fh->fh_lk);
		return;
	}

	/*
	 * We may attempt a cleanup before ever opening a file object. Since
	 * vfs_close attempts to free the underlying memory of the vnode
	 * pointer we want to guard against this.
	 */
	if (fh->vn != NULL)
		vfs_close(fh->vn);

	lock_release(fh->fh_lk);
	lock_destroy(fh->fh_lk);
	kfree(fh);
}

struct filetable *filetable_create()
{
	int res;
	struct filetable *table;

	table = kmalloc(sizeof(*table));
	if (table == NULL) {
		return NULL;
	}

	table->files = kmalloc(sizeof(struct filehandle *) * OPEN_MAX);
	if (table->files == NULL) {
		kfree(table);
		return NULL;
	}

	/* Initialze contents of table */
	int i;
	for (i = 0; i < OPEN_MAX; i++) {
		table->files[i] = NULL;
	}

	table->files[0] = filehandle_create(O_RDONLY);	/* STDIN */
	if (table->files[0] == NULL) {
		kfree(table->files);
		kfree(table);
		return NULL;
	}

	table->files[1] = filehandle_create(O_WRONLY);	/* STDOUT */
	if (table->files[1] == NULL) {
		filehandle_cleanup(table->files[0]);
		kfree(table->files);
		kfree(table);
	}

	table->files[2] = filehandle_create(O_WRONLY);	/* STDERR */
	if (table->files[2] == NULL) {
		filehandle_cleanup(table->files[0]);
		filehandle_cleanup(table->files[1]);
		kfree(table->files);
		kfree(table);
	}

	char con[] = "con:";
	char *conpath;

	/*
	 * Each call to vfs_open destroys the string conpath that is passed in.
	 * That is why we have to continue to free and realloc space for it;
	 * We also assert instead of gracefull failing if we are unable to open
	 * the console device ready/writing. This is because each process
	 * assumes that those file handles are there and something is very
	 * wrong if we are unable to opent them up.
	 */

	conpath = kstrdup(con);
	KASSERT(conpath != NULL);
	res = vfs_open(conpath, O_RDONLY, 0, &table->files[0]->vn);
	KASSERT(res == 0);
	kfree(conpath);

	conpath = kstrdup(con);
	KASSERT(conpath != NULL);
	res = vfs_open(kstrdup(con), O_WRONLY, 0, &table->files[1]->vn);
	KASSERT(res == 0);
	kfree(conpath);

	conpath = kstrdup(con);
	KASSERT(conpath != NULL);
	res = vfs_open(kstrdup(con), O_WRONLY, 0, &table->files[2]->vn);
	KASSERT(res == 0);
	kfree(conpath);

	table->lk = lock_create("file table");

	return table;
}

int filetable_insert(struct filehandle *file, struct filetable *table)
{
	KASSERT(file != NULL && table != NULL);

	int pos = -1;
	int i;

	lock_acquire(table->lk);
	for (i = 0; i < OPEN_MAX; i++) {
		if (table->files[i] == NULL) {
			table->files[i] = file;
			pos = i;
			break;
		}
	}
	lock_release(table->lk);

	return pos;
}

struct filehandle *filetable_remove(int fd, struct filetable *table)
{
	KASSERT(fd >= 0 && fd < OPEN_MAX);
	KASSERT(table != NULL);

	struct filehandle *fh;

	lock_acquire(table->lk);
	fh = table->files[fd];
	table->files[fd] = NULL;
	lock_release(table->lk);

	return fh;
}

struct filehandle *filetable_lookup(int fd, struct filetable *table)
{

	KASSERT(fd >= 0);
	KASSERT(table != NULL);
	KASSERT(lock_do_i_hold(table->lk));

	struct filehandle *fh = NULL;

	fh = table->files[fd];

	return fh;
}

/*
 * This assumes that the process that owns the file table cleaning up before
 * destroying itself. It may be useful to check to make sure that the current
 * process holds the file table that is being destroyed.
 */
void filetable_destroy(struct filetable *table)
{
	KASSERT(table != NULL);

	lock_acquire(table->lk);
	int i;
	for (i = 0; i < OPEN_MAX; i++) {
		if (table->files[i] != NULL) {
			filehandle_cleanup(table->files[i]);
			table->files[i] = NULL;
		}
	}
	lock_release(table->lk);

	lock_destroy(table->lk);
	kfree(table->files);
	kfree(table);
}

struct filetable *filetable_createcopy(struct filetable *src)
{
	KASSERT(src != NULL);
	KASSERT(src->files != NULL);

	struct filetable *dest;

	dest = kmalloc(sizeof(*dest));
	if (dest == NULL) {
		return NULL;
	}

	dest->files = kmalloc(sizeof(struct filehandle) * OPEN_MAX);
	if (dest->files == NULL) {
		kfree(dest);
		return NULL;
	}

	dest->lk = lock_create("file table");
	if (dest->lk == NULL) {
		kfree(dest->files);
		kfree(dest);
		return NULL;
	}

	lock_acquire(src->lk);
	int i;
	for (i = 0; i < OPEN_MAX; i++) {
		struct filehandle *fh = src->files[i];

		if (fh != NULL) {
			lock_acquire(fh->fh_lk);
			fh->refcount++;
			lock_release(fh->fh_lk);
		}

		dest->files[i] = fh;
	}
	lock_release(src->lk);

	return dest;
}
