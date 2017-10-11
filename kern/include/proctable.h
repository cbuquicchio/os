#ifndef _PROC_TABLE_H_
#define _PROC_TABLE_H_

#include <types.h>

struct proc;			/* defined in <proc.h> */
struct lock;			/* defined in <synch.h> */
struct cv;			/* defined in <synch.h> */

struct ptablenode {
	struct proc *proc;	/* The underlying process */
	struct ptablenode *next;	/* A pointer to the next node */
	struct ptablenode *prev;
	struct lock *lk;	/* Lock for accessing values */
	struct cv *cv;		/* Used for waiting procs */
	int status;
	int hasexited;
	pid_t pid;
	pid_t ppid;
};

struct proctable {
	pid_t pidcounter;
	struct lock *ptable_lk;
	struct ptablenode *head;
	struct ptablenode *tail;
};

void proctable_bootstrap(void);
pid_t proctable_insert(struct proc *p);
void proctable_remove(struct ptablenode *node);
struct proctable *proctable_get(void);
struct ptablenode *proctable_lookup(pid_t pid);

#endif				/* _PROC_TABLE_H_ */
