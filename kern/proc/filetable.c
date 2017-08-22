#include <types.h>
#include <lib.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <filetable.h>

#define TABLE_SIZE 10

struct filehandle *
file_open(char *filename, int flags)
{
	KASSERT(filename != NULL);

	struct filehandle *fh;

	fh = kmalloc(sizeof(*fh));
	if (fh == NULL) {
		return NULL;
	}

	(void)flags;

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

	table->files = kmalloc(sizeof(struct filehandle *) * TABLE_SIZE);
	if (table->files == NULL) {
		kfree(table);
		return NULL;
	}

	/* Initialze contents of table */
	int i;
	for (i = 0; i < TABLE_SIZE; i++) {
		table->files[i] = NULL;
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

	for (i = 0; i < TABLE_SIZE; i++) {
		if (table->files[i] == NULL) {
			table->files[i] = file;
			pos = i;
			break;
		}
	}

	return pos;
}

struct filehandle *
filetable_lookup(int fd, struct filetable *table)
{

	KASSERT(fd >= 0 && fd < TABLE_SIZE);
	KASSERT(table != NULL);

	return table->files[fd];
}

void
filetable_destroy(struct filetable *table)
{
	KASSERT(table != NULL);

	lock_destroy(table->lk);
	kfree(table->files);
	kfree(table);
}
