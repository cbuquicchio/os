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
	if (node == NULL)
		return NULL;

	node->cv = cv_create("");
	if (node->cv == NULL) {
		kfree(node);
		return NULL;
	}

	node->lk = lock_create("");
	if (node->lk == NULL) {
		cv_destroy(node->cv);
		kfree(node);
		return NULL;
	}

	node->proc = p;
	node->next = NULL;
	node->status = 0;
	node->hasexited = 0;

	return node;
}

static void ptablenode_destroy(struct ptablenode *node)
{
	KASSERT(node != NULL);
	KASSERT(lock_do_i_hold(node->lk));

	cv_destroy(node->cv);
	lock_release(node->lk);
	lock_destroy(node->lk);
	kfree(node);
}

void proctable_bootstrap()
{
	struct proctable *table;

	table = kmalloc(sizeof(*table));
	KASSERT(table != NULL);

	table->ptable_lk = lock_create("process table lock");
	KASSERT(table->ptable_lk != NULL);

	table->head = NULL;
	table->pidcounter = PID_MIN;

	KASSERT(ptable == NULL);
	ptable = table;
	KASSERT(ptable != NULL);
}

pid_t proctable_insert(struct proc *p, pid_t ppid, struct proctable *table)
{
	KASSERT(p != NULL);
	KASSERT(table != NULL);
	KASSERT(table->pidcounter <= PID_MAX);

	struct ptablenode *pnode;

	pnode = ptablenode_create(p);
	if (pnode == NULL)
		return PID_MIN - 1;	/* return an impossible pid */

	lock_acquire(table->ptable_lk);

	KASSERT(pnode->proc != NULL);

	if (table->head == NULL) {	/* Empty table */
		table->head = pnode;
	} else {
		struct ptablenode *tmp = table->head;
		table->head = pnode;
		pnode->next = tmp;
	}

	pnode->ppid = ppid;
	pnode->pid = table->pidcounter;
	p->pid = table->pidcounter;
	table->pidcounter++;

	lock_release(table->ptable_lk);

	return pnode->proc->pid;
}

void proctable_remove(pid_t pid)
{
	KASSERT(ptable != NULL);

	lock_acquire(ptable->ptable_lk);

	struct ptablenode *cur;
	struct ptablenode *prev;

	cur = ptable->head;
	prev = NULL;

	while (cur != NULL && cur->pid != pid) {
		prev = cur;
		cur = cur->next;
	}

	/* Not found */
	if (cur == NULL) {
		lock_release(ptable->ptable_lk);
		return;
	}

	if (prev == NULL)	/* cur is the head of the list */
		ptable->head = cur->next;
	else
		prev->next = cur->next;

	cur->next = NULL;

	ptablenode_destroy(cur);
	lock_release(ptable->ptable_lk);
}

struct ptablenode *proctable_lookup(pid_t pid)
{
	KASSERT(ptable != NULL);

	struct ptablenode *node;

	lock_acquire(ptable->ptable_lk);
	/*
	 *  If the pid we are looking for is created later than the max pid
	 *  that has been created already we can  guarantee that no process
	 *  exists with that pid.
	 */
	if (pid > ptable->pidcounter) {
		lock_release(ptable->ptable_lk);
		return NULL;
	}

	node = ptable->head;

	/* Iterate until we find a matching pid or we reach the end */
	while (node != NULL && node->pid != pid)
		node = node->next;

	lock_release(ptable->ptable_lk);

	return node;
}

struct proctable *proctable_get()
{
	KASSERT(ptable != NULL);

	return ptable;
}
