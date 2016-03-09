/*
 * sample.c
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile bool intr = false;

static void
onsigint(int signo)
{
	intr = true;
}

static int
sys(void)
{
	return 0;
}

static int
foo(void)
{
	int err;

	if ((err = sys())) {
		fprintf(stderr, "sys(): %s\n", strerror(err));
		return err;
	}

	return 0;
}

static int
bar(void)
{
	int err;

	if ((err = sys())) {
		fprintf(stderr, "sys(): %s\n", strerror(err));
		return err;
	}

	return 0;
}

int
main(void)
{
	int err;

	signal(SIGINT, onsigint);
	signal(SIGTERM, onsigint);

	for (; !intr; usleep(250000)) {
		if ((err = foo())) {
			fprintf(stderr, "foo(): %s\n", strerror(err));
			continue;
		}
		if ((err = bar())) {
			fprintf(stderr, "bar(): %s\n", strerror(err));
			continue;
		}
	}

	fprintf(stderr, "done.\n");

	return 0;
}
