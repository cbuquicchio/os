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

	pid = fork();
	if (pid < 0) {
		err(1, "Failed! pid < 0");
	}

	if (pid == 0) {
		nprintf("The child\n");
	} else {
		nprintf("That parent\n");
	}

	success(TEST161_SUCCESS, SECRET, "/testbin/forktest");
	return 0;
}
