/*
 * Blocking-wait API contracts on single-threaded WASI:
 *  - dispatch_block_wait on a queued block runs the block and returns success
 *  - dispatch_group_wait(FOREVER) returns 0 once the group empties
 *  - a timed dispatch_semaphore_wait that can never be satisfied consumes its
 *    full timeout before reporting KERN_OPERATION_TIMED_OUT (it must not
 *    return early: single-threadedness is not an excuse to skip the wait)
 *  - dispatch_semaphore_wait(FOREVER) is satisfied by queued work
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static uint64_t
mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(void)
{
	dispatch_queue_t gq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	__block int block_ran = 0;
	dispatch_block_t b = dispatch_block_create(0, ^{ block_ran = 1; });
	dispatch_async(gq, b);
	if (dispatch_block_wait(b,
			dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) != 0 ||
			!block_ran) {
		printf("FAIL: block_wait (ran=%d)\n", block_ran);
		exit(1);
	}

	dispatch_group_t g = dispatch_group_create();
	__block int group_ran = 0;
	dispatch_group_async(g, gq, ^{ group_ran = 1; });
	if (dispatch_group_wait(g, DISPATCH_TIME_FOREVER) != 0 || !group_ran) {
		printf("FAIL: group_wait forever (ran=%d)\n", group_ran);
		exit(2);
	}

	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	uint64_t t0 = mono_ms();
	intptr_t r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, (int64_t)(250 * NSEC_PER_MSEC)));
	uint64_t elapsed = mono_ms() - t0;
	if (r == 0 || elapsed < 200) {
		printf("FAIL: sema timedwait r=%ld elapsed=%llums\n", (long)r,
				(unsigned long long)elapsed);
		exit(3);
	}

	dispatch_semaphore_t sem2 = dispatch_semaphore_create(0);
	dispatch_async(gq, ^{ dispatch_semaphore_signal(sem2); });
	if (dispatch_semaphore_wait(sem2, DISPATCH_TIME_FOREVER) != 0) {
		printf("FAIL: sema wait forever\n");
		exit(4);
	}

	printf("blocking waits OK\n");
	return 0;
}
