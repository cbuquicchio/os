#include <types.h>
#include <lib.h>
#include <limits.h>
#include <proc.h>
#include <synch.h>
#include <proctable.h>

static struct proctable *ptable = NULL;

struct ptablenode *ptablenode_create(struct proc *p)
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
	table->pidcounter = PID_MIN;

	KASSERT(ptable == NULL);
	ptable = table;
	KASSERT(ptable != NULL);
}

pid_t proctable_add(struct ptablenode *node, struct proctable *table)
{
	KASSERT(node != NULL);
	KASSERT(table != NULL);
	KASSERT(table->pidcounter <= PID_MAX);

	lock_acquire(table->ptable_lk);

	KASSERT(table->tail->next == NULL);
	table->tail->next = node;
	table->tail = node;

	KASSERT(node->proc != NULL);
	node->proc->pid = ++table->pidcounter;
	lock_acquire(table->ptable_lk);

	return node->proc->pid;
}
