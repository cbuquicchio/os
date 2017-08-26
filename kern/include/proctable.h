#ifndef _PROC_TABLE_H_
#define _PROC_TABLE_H_

#include <types.h>

struct proc;			/* defined in <proc.h> */

struct ptablenode {
	struct proc *proc;
	struct ptablenode *next;
};

struct proctable {
	pid_t pidcounter;
	struct ptablenode *head;
	struct ptablenode *tail;
};

struct ptablenode *proctablenode_create(struct proc *p);

void proctable_bootstrap(void);
void proctable_add(struct ptablenode *node, struct proctable *table);

#endif				/* _PROC_TABLE_H_ */
