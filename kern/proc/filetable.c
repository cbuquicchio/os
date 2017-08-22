#include <types.h>
#include <lib.h>
#include <filetable.h>
#include <kern/fcntl.h>
#include <limits.h> /* defines the max open files */
#include <synch.h>
#include <vfs.h>
#include <vnode.h>

struct filehandle *
file_open(char *filename, int flags)
{
	KASSERT(filename != NULL);
	/* Also check for valid flags */

	struct filehandle *fh;

	fh = kmalloc(sizeof(*fh));
	if (fh == NULL) {
		return NULL;
	}

	fh->vn = kmalloc(sizeof(struct vnode));
	if (fh->vn == NULL) {
		kfree(fh);
		return NULL;
	}

	/*
	 * filename needs to copied and passed in because vfs_open destroys the
	 * string that it is passed
	 */
	int err = vfs_open(kstrdup(filename), flags, 0, &fh->vn);
	if (err) {
		kfree(fh->vn);
		kfree(fh);
		return NULL;
	}

	return fh;
}

struct filetable *
filetable_create()
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

	char *con = kstrdup("con:"); /* Filename for the console */

	/* STDIN */
	table->files[0] = file_open(con, O_RDONLY);
	if (table->files[0] == NULL) {
		kfree(table->files);
		kfree(table);
		return NULL;
	}

	/* STDOUT */
	table->files[1] = file_open(con, O_WRONLY);
	if (table->files[1] == NULL) {
		vfs_close(table->files[0]->vn);
		kfree(table->files[0]->vn);
		kfree(table->files[0]);
		kfree(table->files);
		kfree(table);
		return NULL;
	}

	/* STDERR */
	table->files[2] = file_open(con, O_WRONLY);
	if (table->files[2] == NULL) {
		vfs_close(table->files[0]->vn);
		kfree(table->files[0]->vn);
		kfree(table->files[0]);
		vfs_close(table->files[1]->vn);
		kfree(table->files[1]->vn);
		kfree(table->files[1]);
		kfree(table->files);
		kfree(table);
		return NULL;
	}

	table->lk = lock_create("file table");

	return table;
}

int
filetable_insert(struct filehandle *file, struct filetable *table)
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

struct filehandle *
filetable_lookup(int fd, struct filetable *table)
{

	KASSERT(fd >= 0 && fd < OPEN_MAX);
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
void
filetable_destroy(struct filetable *table)
{
	KASSERT(table != NULL);

	lock_destroy(table->lk);
	kfree(table->files);
	kfree(table);
}
