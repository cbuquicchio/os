#include <types.h>
#include <current.h>
#include <kern/errno.h>
#include <limits.h>
#include <proc.h>
#include <proctable.h>
#include <thread.h>
#include <mips/trapframe.h>
#include <syscall.h>

static void enter_forked_proc(void *tf, unsigned long _)
{
	KASSERT(tf != NULL);
	(void) _;

	struct trapframe stack = *(struct trapframe *) tf;

	stack.tf_v0 = 0;
	stack.tf_a3 = 0;
	stack.tf_epc += 4;
	mips_usermode(&stack);
}

int sys_fork(struct trapframe *tf, int *retval)
{
	struct trapframe *tfcpy;
	struct proc *newproc;
	int err;
	pid_t pid;

	tfcpy = kmalloc(sizeof(*tfcpy));
	if (tfcpy == NULL) {
		return ENOMEM;
	}

	memcpy(tfcpy, tf, sizeof(*tfcpy));

	newproc = proc_create_forkable();

	pid = proctable_insert(newproc, proctable_get());
	if (pid < PID_MIN) {
		kfree(tfcpy);
		return ENOMEM;
	}

	*retval = pid;

	err = thread_fork("test", newproc, &enter_forked_proc, tfcpy, 0);
	if (err) {
		proc_destroy(newproc);
		return err;
	}

	return 0;
}
