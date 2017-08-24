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
	fh->refcount = 0;
	fh->flag = flag;

	return fh;
}

void filehandle_destroy(struct filehandle *fh)
{
	KASSERT(fh != NULL);

	lock_destroy(fh->fh_lk);
	kfree(fh->vn);
	kfree(fh);
}

struct filetable *filetable_create()
{
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

	char con[] = "con:";	/* Filename for the console */

	table->files[0] = filehandle_create(O_RDONLY);	/* STDIN */
	if (table->files[0] == NULL) {
		kfree(table->files);
		kfree(table);
		return NULL;
	}

	table->files[1] = filehandle_create(O_WRONLY);	/* STDOUT */
	if (table->files[1] == NULL) {
		filehandle_destroy(table->files[0]);
		kfree(table->files);
		kfree(table);
	}

	table->files[2] = filehandle_create(O_WRONLY);	/* STDERR */
	if (table->files[2] == NULL) {
		filehandle_destroy(table->files[0]);
		filehandle_destroy(table->files[1]);
		kfree(table->files);
		kfree(table);
	}

	/* These string copies might need to be destroyed after */
	/* We also need to check for error that return */
	vfs_open(kstrdup(con), O_RDONLY, 0, &table->files[0]->vn);
	vfs_open(kstrdup(con), O_WRONLY, 0, &table->files[1]->vn);
	vfs_open(kstrdup(con), O_WRONLY, 0, &table->files[2]->vn);

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

struct filehandle *filetable_lookup(int fd, struct filetable *table)
{

	KASSERT(fd >= 0);
	KASSERT(table != NULL);

	struct filehandle *fh = NULL;

	lock_acquire(table->lk);
	fh = table->files[fd];
	lock_release(table->lk);

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

	lock_destroy(table->lk);
	kfree(table->files);
	kfree(table);
}
