/*
 * A blocking dispatch_semaphore_wait(FOREVER) satisfied by file-descriptor
 * readiness: the sole thread parks in the host poll, the runner writes to
 * stdin after a delay, the read source's handler runs and signals the
 * semaphore. This is the callback-driven-server shape: without fd sources
 * counting as progress, the wait would have crashed as a deadlock.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(void)
{
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_queue_t queue = dispatch_queue_create("wasi.fdwake", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			0 /* stdin */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		char buf[64];
		(void)read(0, buf, sizeof(buf));
		dispatch_source_cancel(source);
		dispatch_semaphore_signal(sem);
	});
	dispatch_resume(source);
	printf("waiting on semaphore\n");
	fflush(stdout);
	if (dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER) != 0) {
		printf("FAIL: wait returned nonzero\n");
		return 1;
	}
	printf("fd wakeup OK\n");
	return 0;
}
