#ifndef _PROC_TABLE_H_
#define _PROC_TABLE_H_

#include <types.h>

struct proc;			/* defined in <proc.h> */
struct lock;			/* defined in <synch.h> */

struct ptablenode {
	struct proc *proc;
	struct ptablenode *next;
};

struct proctable {
	pid_t pidcounter;
	struct lock *ptable_lk;
	struct ptablenode *head;
	struct ptablenode *tail;
};

struct ptablenode *ptablenode_create(struct proc *p);

void proctable_bootstrap(void);
pid_t proctable_add(struct ptablenode *node, struct proctable *table);

#endif				/* _PROC_TABLE_H_ */
