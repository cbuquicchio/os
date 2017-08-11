/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

/*
 * Use these stubs to test your reader-writer locks.
 */

/*
 * This tests creating and destroying a reader-writer lock
 */
int
rwtest(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	struct rwlock* lock = rwlock_create("testlock");
	int res = TEST161_SUCCESS;

	if (lock == NULL) {
		res = TEST161_FAIL;
	}

	if (lock->reader_count != 0 || lock->writer_waiting_count != 0 ||
			lock->is_writing != 0) {
		res = TEST161_FAIL;
	}

	// If we get passed this point without an fatal error or failed
	// assertion we can consider the test a success
	rwlock_destroy(lock);

	success(res, SECRET, "rwt1");

	return 0;
}

/*
 * This tests destroying a reader-writer lock
 */
int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	struct rwlock *lock = rwlock_create("testlock");
	int res = TEST161_SUCCESS;

	rwlock_acquire_read(lock);

	if (lock->reader_count != 1) {
		res = TEST161_FAIL;
	}

	rwlock_destroy(lock);

	success(res, SECRET, "rwt2");

	return 0;
}

/*
 * This tests rwlock_release_read
 */
int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	const int num_iters = 10;
	struct rwlock *lock = rwlock_create("testlock");
	int res = TEST161_SUCCESS;

	lock->reader_count = num_iters;

	for (int i = 0; i < num_iters; i++) {
		rwlock_release_read(lock);
	}

	if (lock->reader_count != 0) {
		res = TEST161_FAIL;
	}

	rwlock_destroy(lock);

	success(res, SECRET, "rwt3");
	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	success(TEST161_FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt5");

	return 0;
}
