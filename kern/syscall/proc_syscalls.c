#include <types.h>
#include <lib.h>
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
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/wait.h>

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

	tfcpy = kmalloc(sizeof(*tfcpy));
	if (tfcpy == NULL)
		return ENOMEM;

	memcpy(tfcpy, tf, sizeof(*tfcpy));

	newproc = proc_create_forkable(curproc->p_name);
	if (newproc == NULL) {
		kfree(tfcpy);
		return ENOMEM;
	}

	*retval = newproc->pid;

	err = thread_fork(curthread->t_name, newproc, &enter_forked_proc,
			  tfcpy, 0);
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
	struct ptablenode *childnode;
	int err = 0;

	if (pid < PID_MIN || pid > PID_MAX)	/* Impossble pid value check */
		return ESRCH;

	if (options != 0)
		return EINVAL;

	childnode = proctable_lookup(pid);
	if (childnode == NULL)
		return ESRCH;	/* Process does not exist */

	lock_acquire(childnode->lk);

	if (childnode->ppid != curproc->pid) {
		lock_release(childnode->lk);
		return ECHILD;	/* Process is not a child process */
	}

	if (childnode->hasexited == 0)	/* child proc has not exited */
		cv_wait(childnode->cv, childnode->lk);

	/*
	 * Process should be destroyed at this point by the child process
	 * exiting.
	 */
	KASSERT(childnode->proc == NULL);

	*retval = pid;
	if (status != NULL)
		err = copyout(&childnode->status, status, sizeof(int));

	proctable_remove(childnode->pid);

	return err;
}

int sys__exit(int exitcode)
{
	struct ptablenode *procnode;

	procnode = proctable_lookup(curproc->pid);
	KASSERT(procnode != NULL);

	lock_acquire(procnode->lk);

	procnode->hasexited = 1;
	procnode->status = _MKWAIT_EXIT(exitcode);

	/* Clean up the current process */
	proc_remthread(curthread);
	proc_destroy(procnode->proc);
	procnode->proc = NULL;

	/* Wake up the parent process */
	cv_broadcast(procnode->cv, procnode->lk);

	lock_release(procnode->lk);

	thread_exit();
	/* thread_exit does not return */
	panic("thread_exit returned\n");

	return EINVAL;
}

static int setup_runprogram(char *progname, vaddr_t * stackptr,
			    vaddr_t * entrypoint)
{
	struct addrspace *as;
	struct vnode *v;
	int err;

	/* Open the file. */
	err = vfs_open(progname, O_RDONLY, 0, &v);
	if (err)
		return err;

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Destroy the old address space before assigning the new one */
	as_destroy(proc_getas());

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	err = load_elf(v, entrypoint);
	if (err) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return err;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	err = as_define_stack(as, stackptr);

	return err;
}

static int process_arguments(userptr_t uargs, char *kargbuf, int *argc)
{
	char *kargs;
	int err;		/* Error flag */
	size_t bytetotal;	/* The total number of bytes in kargbuf */
	size_t slen;		/* The number of bytes copied using copyinstr */

	/* Initialize argc */
	*argc = 0;
	err = 0;
	bytetotal = 0;

	while (1) {
		err = copyin(uargs + (*argc) * 4, &kargs, 4);
		if (err || kargs == NULL)
			break;

		err = copyinstr((userptr_t) kargs, kargbuf + bytetotal,
				ARG_MAX - bytetotal, &slen);
		if (err)
			break;

		bytetotal += slen;

		/* Pad with NULL bytes to take up the rest of the word size */
		while (bytetotal % sizeof(kargs)) {
			bytetotal++;
			*(kargbuf + bytetotal) = '\0';
		}

		(*argc)++;
	}

	return err;
}

int sys_execv(userptr_t progname, userptr_t args)
{
	char *kprogname;
	size_t slen;
	int err;

	kprogname = kmalloc(PATH_MAX);
	if (kprogname == NULL)
		return ENOMEM;

	/* Copy the progranme name from userspace to kernel space */
	err = copyinstr(progname, kprogname, PATH_MAX, &slen);
	if (err) {
		kfree(kprogname);
		return err;
	}

	char *kargbuf;
	int argc;

	kargbuf = kmalloc(ARG_MAX);
	if (kargbuf == NULL) {
		kfree(kprogname);
		return ENOMEM;
	}

	err = process_arguments(args, kargbuf, &argc);
	if (err) {
		kfree(kprogname);
		kfree(kargbuf);
		return err;
	}

	vaddr_t entrypoint;
	vaddr_t stackptr;

	err = setup_runprogram(kprogname, &stackptr, &entrypoint);
	if (err) {
		kfree(kprogname);
		kfree(kargbuf);
		return err;
	}

	/* Copy each one of the arguments to the stack */
	int i;
	char *kargv[argc + 1];
	size_t sofar;
	for (i = 0, sofar = 0; i < argc; i++) {
		slen = strlen(kargbuf + sofar);
		slen += sizeof(char *) - slen % sizeof(char *);

		stackptr -= sofar + slen;
		err = copyout(kargbuf + sofar, (userptr_t) stackptr, slen);
		if (err)
			break;

		kargv[i] = (char *)stackptr;
		sofar += slen;
	}

	stackptr -= sizeof(kargv);
	kargv[argc] = NULL;

	/* Copy argv to the stack */
	err = copyout(kargv, (userptr_t) stackptr, sizeof(kargv));
	kfree(kargbuf);
	if (err)
		return err;

	enter_new_process(argc, (userptr_t) stackptr, NULL,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
