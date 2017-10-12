#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>

int main()
{
	int pid;
	int tmp = 10;

	pid = fork();
	if (pid < 0) {
		err(1, "Failed! pid < 0");
	}

	if (pid == 0) {
		if (tmp != 10)
			err(1, "Failed! process stack broken.");
	} else {
		int _;
		waitpid(pid, &_, 0);
		success(TEST161_SUCCESS, SECRET, "/testbin/forktest");
	}

	return 0;
}
