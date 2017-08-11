/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->lk_spinlk);
	lock->lk_owner = NULL;

	return lock;
}

void lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_owner == NULL);

	wchan_destroy(lock->lk_wchan);
	spinlock_cleanup(&lock->lk_spinlk);

	kfree(lock->lk_name);
	kfree(lock);
}

void lock_acquire(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&lock->lk_spinlk);

	// If you already hold the lock no need to re-acquire it
	if (lock_do_i_hold(lock)) {
		spinlock_release(&lock->lk_spinlk);
		return;
	}

	/* Call this (atomically) before waiting for a lock */
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

	struct spinlock *spinlk = &lock->lk_spinlk;

	while (lock->lk_owner != NULL) {

		// Spinlock will be released and re-acquired before leaving
		// this function
		wchan_sleep(lock->lk_wchan, spinlk);
	}

	KASSERT(lock->lk_owner == NULL);
	lock->lk_owner = curthread;

	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);

	spinlock_release(spinlk);
}

void lock_release(struct lock *lock)
{
	KASSERT(lock != NULL);

	spinlock_acquire(&lock->lk_spinlk);

	KASSERT(lock_do_i_hold(lock));

	lock->lk_owner = NULL;
	KASSERT(lock->lk_owner == NULL);

	wchan_wakeone(lock->lk_wchan, &lock->lk_spinlk);

	/* Call this (atomically) when the lock is released */
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

	spinlock_release(&lock->lk_spinlk);
}

bool lock_do_i_hold(struct lock *lock)
{
	KASSERT(lock != NULL);

	return curthread == lock->lk_owner;
}

////////////////////////////////////////////////////////////
//
// CV

struct cv *cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name == NULL) {
		kfree(cv);
		return NULL;
	}

	cv->cv_wchan = wchan_create(name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}

	spinlock_init(&cv->cv_spinlk);

	return cv;
}

void cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	wchan_destroy(cv->cv_wchan);
	spinlock_cleanup(&cv->cv_spinlk);
	kfree(cv->cv_name);
	kfree(cv);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));

	struct spinlock *spinlk = &cv->cv_spinlk;

	spinlock_acquire(spinlk);

	lock_release(lock);
	wchan_sleep(cv->cv_wchan, spinlk);
	spinlock_release(spinlk);

	lock_acquire(lock);
}

void cv_signal(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));

	struct spinlock *spinlk = &cv->cv_spinlk;

	spinlock_acquire(spinlk);
	wchan_wakeone(cv->cv_wchan, spinlk);
	spinlock_release(spinlk);
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));

	struct spinlock *spinlk = &cv->cv_spinlk;

	spinlock_acquire(spinlk);
	wchan_wakeall(cv->cv_wchan, spinlk);
	spinlock_release(spinlk);
}

struct rwlock *rwlock_create(const char *name)
{
	struct rwlock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->rwlock_name = kstrdup(name);
	if (lock->rwlock_name == NULL) {
		kfree(lock);
		return NULL;
	}

	lock->rw_wchan = wchan_create(lock->rwlock_name);
	if (lock->rw_wchan == NULL) {
		kfree(lock->rwlock_name);
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->rw_spinlk);

	lock->reader_count = 0;
	lock->is_writing = 0;
	lock->writer_waiting_count = 0;

	return lock;
}

void rwlock_destroy(struct rwlock *lock)
{
	KASSERT(lock != NULL);

	wchan_destroy(lock->rw_wchan);
	kfree(lock->rwlock_name);
	kfree(lock);
}

void rwlock_acquire_read(struct rwlock *lock)
{
	KASSERT(lock);

	spinlock_acquire(&lock->rw_spinlk);

	// Prevent adding readers when there are writers waiting
	// writers have priority
	while (lock->is_writing || lock->writer_waiting_count > 0) {
		wchan_sleep(lock->rw_wchan, &lock->rw_spinlk);
	}

	lock->reader_count++;
	spinlock_release(&lock->rw_spinlk);
}

void rwlock_release_read(struct rwlock *lock)
{
	KASSERT(lock);

	spinlock_acquire(&lock->rw_spinlk);

	KASSERT(lock->reader_count > 0);
	lock->reader_count--;

	// Wait till all readers are done until waking all sleeping threads
	// this allows readers to have priority when there is a writer waiting
	// to write
	if (lock->reader_count == 0) {
		wchan_wakeall(lock->rw_wchan, &lock->rw_spinlk);
	}

	spinlock_release(&lock->rw_spinlk);
}

void rwlock_acquire_write(struct rwlock *lock)
{
	KASSERT(lock);

	spinlock_acquire(&lock->rw_spinlk);

	while (lock->reader_count > 0 || lock->is_writing) {
		lock->writer_waiting_count++;
		wchan_sleep(lock->rw_wchan, &lock->rw_spinlk);
	}

	lock->is_writing = 1;
	lock->writer_waiting_count--;

	spinlock_release(&lock->rw_spinlk);
}

void rwlock_release_write(struct rwlock *lock)
{
	KASSERT(lock);

	spinlock_acquire(&lock->rw_spinlk);

	lock->is_writing = 0;
	lock->writer_waiting_count--;

	wchan_wakeall(lock->rw_wchan, &lock->rw_spinlk);

	spinlock_release(&lock->rw_spinlk);

}
