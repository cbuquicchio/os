#include <types.h>
#include <lib.h>
#include <limits.h>
#include <proc.h>
#include <synch.h>
#include <proctable.h>

static struct proctable *ptable = NULL;

static struct ptablenode *ptablenode_create(struct proc *p)
{
	KASSERT(p != NULL);

	struct ptablenode *node;

	node = kmalloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}

	node->proc = p;
	node->next = NULL;

	return node;
}

void proctable_bootstrap()
{
	struct proctable *table;

	table = kmalloc(sizeof(*table));
	KASSERT(table != NULL);

	table->ptable_lk = lock_create("process table lock");
	KASSERT(table->ptable_lk != NULL);

	table->head = NULL;
	table->tail = NULL;

	/*
	 * Start the pid counter at one less than the min pid for a user
	 * process. This assumes that the kernel process will be created and
	 * inserted into the proc table before any user procs are. The kernel
	 * proc will then get assigned pid 1 and everything else should line
	 * up fine.
	 */
	table->pidcounter = PID_MIN - 1;

	/* Insert the kernel process as the first entry */
	proctable_insert(kproc, table);
	KASSERT(kproc->pid == 1);

	KASSERT(ptable == NULL);
	ptable = table;
	KASSERT(ptable != NULL);
}

pid_t proctable_insert(struct proc *p, struct proctable *table)
{
	KASSERT(p != NULL);
	KASSERT(table != NULL);
	KASSERT(table->pidcounter <= PID_MAX);

	struct ptablenode *pnode;

	pnode = ptablenode_create(p);
	if (pnode == NULL) {
		return 0;
	}

	lock_acquire(table->ptable_lk);

	KASSERT(pnode->proc != NULL);

	if (table->head == NULL) {	/* Empty table */
		KASSERT(table->tail == NULL);
		table->head = pnode;
		table->tail = pnode;
	} else {
		KASSERT(table->tail->next == NULL);
		table->tail->next = pnode;
		table->tail = pnode;
	}

	pnode->proc->pid = table->pidcounter;
	table->pidcounter++;
	lock_release(table->ptable_lk);

	return pnode->proc->pid;
}

struct proctable *proctable_get()
{
	KASSERT(ptable != NULL);

	return ptable;
}
