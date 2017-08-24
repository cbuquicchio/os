#ifndef _FILETABLE_H_
#define  _FILETABLE_H_

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
 * file_open - Given and pathname pointing to a file or device (file-like obj)
 *             make a call to the VFS layer to open the file and return a
 *             pointer to the file handle.
 */

struct filehandle *file_open(char *filename, int flags);

struct filehandle *filehandle_create(int flag);

void filehandle_destroy(struct filehandle *fh);

/*
 * File Table operations
 *
 * filetable_create  - Takes no arguments and returns a file table pointer. The
 *                     underlying file table object is initialzed with three
 *                     filehandles corresponding for stdin, stdout, and stderr
 * filetable_insert  - Given a file handle insert it into the array of open
 *                     files. Return the position in the table that the handle
 *                     was inserted(file descriptor). If no vacant spots are
 *                     found return -1.
 * filetable_lookup  - Given file desriptor return its correspond file handle.
 * filetable_destroy - Free the resources used by the file table.
 */

struct filetable *filetable_create(void);

int filetable_insert(struct filehandle *file, struct filetable *table);

struct filehandle *filetable_lookup(int fd, struct filetable *table);

void filetable_destroy(struct filetable *table);

#endif				/* _FILETABLE_H_ */
