#include <types.h>
#include <copyinout.h>
#include <current.h>
#include <filetable.h>
#include <limits.h>
#include <syscall.h>

int
sys_open(userptr_t filename, int flags)
{
	struct filehandle *fh;
	struct filetable *ft = curthread->t_filetable;
	size_t len_actual = 0;
	char *fn = kmalloc(sizeof(char) * PATH_MAX);

	copyinstr(filename, fn, PATH_MAX, &len_actual);

	if (len_actual > PATH_MAX) {
		return -1;
	}

	fh = file_open(fn, flags);
	if (fh == NULL) {
		return -1;
	}

	int fd = filetable_insert(fh, ft);

	return fd;
}
