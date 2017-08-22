#ifndef _FILETABLE_H_
#define  _FILETABLE_H_

struct lock; /* sleeplock defined in synch.h */
struct vnode; /* file object defined in vnode.h */

struct filehandle {
	struct vnode *vn;
};

struct filetable {
	struct filehandle **files;
	struct lock *lk;
};

struct filehandle *
file_open(char *filename, int flags);

struct filetable *
filetable_create(void);

int
filetable_insert(struct filehandle *file, struct filetable *table);

struct filehandle *
filetable_lookup(int fd, struct filetable *table);

void
filetable_destroy(struct filetable *table);

#endif /* _FILETABLE_H_ */
