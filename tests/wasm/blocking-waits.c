/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

/*
 * Blocking-wait API contracts on single-threaded WASI:
 *  - dispatch_block_wait on a queued block runs the block and returns 0
 *  - dispatch_group_wait(FOREVER) returns 0 when the group is empty
 *  - a timed dispatch_semaphore_wait that nothing can signal waits for its
 *    full timeout, then returns KERN_OPERATION_TIMED_OUT. A single thread
 *    is not a reason to return early.
 *  - queued work signals a dispatch_semaphore_wait(FOREVER)
 *
 * Each check exits with its own status, so the status names the failed check.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// the block wait must return long before this timeout
#define BLOCK_WAIT_TIMEOUT (2 * NSEC_PER_SEC)
// the timeout of the semaphore wait that nothing signals
#define SEMA_WAIT_MS 250
// the minimum time that wait must take, with 50 ms of margin for a coarse
// host clock
#define SEMA_MIN_ELAPSED_MS 200

static uint64_t
mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec) /
			NSEC_PER_MSEC;
}

int main(void)
{
	dispatch_queue_t gq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	__block int block_ran = 0;
	dispatch_block_t b = dispatch_block_create(0, ^{ block_ran = 1; });
	dispatch_async(gq, b);
	if (dispatch_block_wait(b,
			dispatch_time(DISPATCH_TIME_NOW, BLOCK_WAIT_TIMEOUT)) != 0 ||
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
			dispatch_time(DISPATCH_TIME_NOW,
			(int64_t)(SEMA_WAIT_MS * NSEC_PER_MSEC)));
	uint64_t elapsed = mono_ms() - t0;
	if (r == 0 || elapsed < SEMA_MIN_ELAPSED_MS) {
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
