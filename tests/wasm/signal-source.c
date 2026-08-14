/*
 * Signal dispatch sources over wasi-libc's _WASI_EMULATED_SIGNAL emulation.
 * There is no asynchronous or cross-process signal delivery on WASI; an
 * in-process raise() invokes the emulated handler synchronously, and the
 * armed source's handler fires on the next drain with the accumulated count.
 */
#include <dispatch/dispatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.signal", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
			SIGUSR1, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		unsigned long count = dispatch_source_get_data(source);
		printf("signal handler fired, count=%lu\n", count);
		if (count == 2) {
			printf("signal source OK\n");
			exit(0);
		}
		printf("FAIL: expected count 2\n");
		exit(1);
	});
	dispatch_resume(source);
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		raise(SIGUSR1);
		raise(SIGUSR1);
	});
	dispatch_main();
}
