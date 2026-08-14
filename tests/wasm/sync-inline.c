/*
 * dispatch_sync and dispatch_barrier_sync run inline on the sole WASI thread
 * without requiring dispatch_main() or any cooperative drain.
 * (Adapted from the dispatch_wasi_sync smoke test in PR #1.)
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

static void inc(void *c) { (*(int *)c)++; }

int main(void)
{
	int n = 0;
	dispatch_sync_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
			&n, inc);
	dispatch_queue_t s = dispatch_queue_create("v2.sync.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync_f(s, &n, inc);
	dispatch_queue_t c = dispatch_queue_create("v2.sync.conc",
			DISPATCH_QUEUE_CONCURRENT);
	__block int barrier_ran = 0;
	dispatch_barrier_sync(c, ^{ barrier_ran = 1; });
	if (n != 2 || !barrier_ran) {
		printf("FAIL: n=%d barrier_ran=%d\n", n, barrier_ran);
		return 1;
	}
	printf("sync inline OK\n");
	return 0;
}
