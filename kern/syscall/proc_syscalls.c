#include <types.h>
#include <current.h>
#include <kern/errno.h>
#include <limits.h>
#include <proc.h>
#include <proctable.h>
#include <thread.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <synch.h>
#include <copyinout.h>

static void enter_forked_proc(void *tf, unsigned long _)
{
	KASSERT(tf != NULL);
	(void)_;

	struct trapframe stack = *(struct trapframe *)tf;

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
	if (newproc == NULL) {
		kfree(tfcpy);
		return ENOMEM;
	}

	pid = proctable_insert(newproc, proctable_get());
	if (pid < PID_MIN) {
		proc_destroy(newproc);
		kfree(tfcpy);
		return ENOMEM;
	}

	*retval = pid;

	err = thread_fork("test", newproc, &enter_forked_proc, tfcpy, 0);
	if (err) {
		kfree(tfcpy);
		proc_destroy(newproc);
		return err;
	}

	return 0;
}

int sys_getpid(int *retval)
{
	KASSERT(curproc != NULL);

	*retval = curproc->pid;

	return 0;
}

int sys_waitpid(pid_t pid, userptr_t status, int options, int *retval)
{
	(void)options;
	struct ptablenode *childnode;

	if (pid < PID_MIN || pid > PID_MAX)	/* Impossble pid value check */
		return ESRCH;

	childnode = proctable_lookup(pid);
	if (childnode == NULL)
		return ESRCH;	/* Process does not exist */

	if (childnode->proc->ppid != curproc->pid)
		return ECHILD;	/* Process is not a child process */

	if (status == NULL)
		return EFAULT;

	lock_acquire(childnode->lk);

	if (childnode->hasexited == 0) {	/* child proc has not exited */
		cv_wait(childnode->cv, childnode->lk);
	}

	*retval = pid;
	copyout(&childnode->status, status, sizeof(int));
	lock_release(childnode->lk);

	return 0;
}

int sys__exit(int exitcode)
{
	struct ptablenode *procnode;

	procnode = proctable_lookup(curproc->pid);
	KASSERT(procnode != NULL);

	lock_acquire(procnode->lk);
	procnode->hasexited = 1;
	procnode->status = exitcode;
	cv_broadcast(procnode->cv, procnode->lk);
	lock_release(procnode->lk);

	thread_exit();

	return 0;
}
