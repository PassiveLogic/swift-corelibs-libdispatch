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
 * Top-level timed waits and a main-queue backlog on single-threaded WASI:
 *  - a timed semaphore wait and a timed group wait do not run main-queue
 *    blocks, so a main-queue backlog cannot make them return late
 *  - a wait with an expired deadline does not run main-queue blocks either
 *  - root-queue work still runs during the wait, one item per step
 *  - dispatch_main() then runs the whole main-queue backlog in order
 *
 * Each check exits with its own status, so the status names the failed check.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// the number of main-queue items in the backlog
#define MAIN_ITEMS 20
// each main-queue item spins this long, so the backlog takes 100 ms
#define ITEM_MS 5
// the timeout of each timed wait
#define BUDGET_MS 20
// The latest return of a timed wait. The 30 ms margin allows for one root
// item (ITEM_MS) and scheduling noise. It is far below the backlog time.
#define LATE_LIMIT_MS (BUDGET_MS + 30)

static uint64_t
mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec) /
			NSEC_PER_MSEC;
}

static void
spin_ms(uint64_t ms)
{
	uint64_t end = mono_ms() + ms;
	while (mono_ms() < end) {
	}
}

static int main_ran;
static int root_ran;
static int waits_done;

static void
check_wait(const char *what, intptr_t r, uint64_t elapsed, uint64_t limit)
{
	if (r == 0 || elapsed > limit || main_ran != 0) {
		printf("FAIL: %s r=%ld elapsed=%llums limit=%llums main_ran=%d\n",
				what, (long)r, (unsigned long long)elapsed,
				(unsigned long long)limit, main_ran);
		exit(1);
	}
}

int main(void)
{
	dispatch_queue_t mq = dispatch_get_main_queue();
	dispatch_queue_t gq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	for (int i = 0; i < MAIN_ITEMS; i++) {
		dispatch_async(mq, ^{
			if (!waits_done) {
				printf("FAIL: main item %d ran inside a top-level wait\n", i);
				exit(4);
			}
			if (main_ran != i) {
				printf("FAIL: main item %d ran at %d\n", i, main_ran);
				exit(2);
			}
			spin_ms(ITEM_MS);
			if (++main_ran == MAIN_ITEMS) {
				printf("timed wait main backlog OK (root_ran=%d)\n",
						root_ran);
				exit(0);
			}
		});
	}
	dispatch_async(gq, ^{ root_ran++; });

	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	uint64_t t0 = mono_ms();
	intptr_t r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, BUDGET_MS * NSEC_PER_MSEC));
	check_wait("sema timedwait", r, mono_ms() - t0, LATE_LIMIT_MS);
	if (root_ran != 1) {
		printf("FAIL: root item did not run in the wait (root_ran=%d)\n",
				root_ran);
		exit(3);
	}

	dispatch_group_t g = dispatch_group_create();
	dispatch_group_enter(g);
	t0 = mono_ms();
	r = dispatch_group_wait(g,
			dispatch_time(DISPATCH_TIME_NOW, BUDGET_MS * NSEC_PER_MSEC));
	check_wait("group timedwait", r, mono_ms() - t0, LATE_LIMIT_MS);

	t0 = mono_ms();
	r = dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW);
	check_wait("sema expired wait", r, mono_ms() - t0, ITEM_MS);
	t0 = mono_ms();
	r = dispatch_group_wait(g, DISPATCH_TIME_NOW);
	check_wait("group expired wait", r, mono_ms() - t0, ITEM_MS);
	dispatch_group_leave(g);

	waits_done = 1;
	dispatch_main();
}
