#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>

struct lock;			/* Sleeplock defined in synch.h */
struct vnode;			/* file object defined in vnode.h */

/*
 * The File Handle is the intermediary between a file descriptor(the index of
 * the file handle in the file table) and the file object(the vnode). This is a
 * method of indirection that abstracts away the lower level operation from
 * user space. User programs only receive and deal with file descriptors.
 */

struct filehandle {
	struct vnode *vn;	/* The underlying file object */
	struct lock *fh_lk;	/* Sleep Lock */
	off_t offset;		/* The offset in a file */
	uint32_t refcount;	/* Number of threads with this file open */
	int flag;
};

struct filetable {
	struct filehandle **files;	/* Open files in the file table */
	struct lock *lk;	/* Sleep lock */
};

/*
 * File Handle operations
 *
 * filehandle_create  - Create a file handle struct with the supplied flag.
 * filehandle_destroy - Destroys the given filehandle.
 */

struct filehandle *filehandle_create(int flag);
void filehandle_destroy(struct filehandle *fh);

/*
 * File Table operations
 *
 * filetable_create     - Takes no arguments and returns a file table pointer.
 *                        The file table object is initialzed with three
 *                        filehandles corresponding for stdin, stdout, and
 *                        stderr
 * filetable_createcopy - Creates a copied file table based on the source table
 *                        pass in. This copy has the same filehandle pointers
 *                        in its files array. Also all of those file handles
 *                        have their refcount incremented.
 * filetable_insert     - Given a file handle insert it into the array of open
 *                        files. Return the position in the table that the
 *                        handle was inserted(file descriptor). If no vacant
 *                        spots are found return -1.
 * filetable_remove     - Removes a file handle in the file table. The file
 *                        table at the position specified is marked NULL. The
 *                        handle instance is returned if it was found in the
 *                        table otherwise NULL will be returned to indicate the
 *                        file handle was not found.
 * filetable_lookup     - Given file desriptor return its correspond file
 *                        handle.
 * filetable_destroy    - Free the resources used by the file table.
 */

struct filetable *filetable_create(void);
struct filetable *filetable_createcopy(struct filetable *src);
int filetable_insert(struct filehandle *file, struct filetable *table);
struct filehandle *filetable_remove(int fd, struct filetable *table);
struct filehandle *filetable_lookup(int fd, struct filetable *table);
void filetable_destroy(struct filetable *table);

#endif				/* _FILETABLE_H_ */
