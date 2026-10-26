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
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

// the number of main-queue items in the backlog
#define MAIN_ITEMS 20
// each main-queue item spins this long, so the backlog takes 100 ms
#define ITEM_MS 5
// the timeout of each timed wait
#define BUDGET_MS 20
// The latest return of a timed wait. The 30 ms margin allows for scheduling
// noise. It is far below the backlog time.
#define LATE_LIMIT_MS (BUDGET_MS + 30)

static void
spin_ms(uint64_t ms)
{
	uint64_t end = wasi_test_now_ns() + ms * NSEC_PER_MSEC;
	while (wasi_test_now_ns() < end) {
	}
}

static long main_ran;
static long root_ran;
// main items that ran out of order
static long main_ran_out_of_order;

// the 20th main item ends the test
static void
check_backlog(void)
{
	test_long("dispatch_main() runs all 20 main-queue items", main_ran,
			MAIN_ITEMS);
	test_long("main-queue items run in order", main_ran_out_of_order, 0);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Timed Wait Main Backlog");

	dispatch_queue_t main_queue = dispatch_get_main_queue();
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	for (int i = 0; i < MAIN_ITEMS; i++) {
		dispatch_async(main_queue, ^{
			if (main_ran != i) {
				main_ran_out_of_order++;
			}
			spin_ms(ITEM_MS);
			if (++main_ran == MAIN_ITEMS) {
				check_backlog();
			}
		});
	}
	dispatch_async(global, ^{ root_ran++; });

	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = wasi_test_now_ns();
	long result = dispatch_semaphore_wait(semaphore, dispatch_time(
			DISPATCH_TIME_NOW, BUDGET_MS * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("timed semaphore wait with a main backlog times out",
			!result, 0);
	test_long_less_than_or_equal("timed semaphore wait returns within "
			"50 ms despite the 100 ms main backlog", elapsed_ms,
			LATE_LIMIT_MS);
	test_long("timed semaphore wait runs no main-queue item", main_ran, 0);
	test_long("root-queue item runs during the timed semaphore wait",
			root_ran, 1);

	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	start = wasi_test_now_ns();
	result = dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW,
			BUDGET_MS * NSEC_PER_MSEC));
	elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("timed group wait with a main backlog times out", !result,
			0);
	test_long_less_than_or_equal("timed group wait returns within 50 ms "
			"despite the 100 ms main backlog", elapsed_ms,
			LATE_LIMIT_MS);
	test_long("timed group wait runs no main-queue item", main_ran, 0);

	start = wasi_test_now_ns();
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW);
	elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("expired semaphore wait with a main backlog times out",
			!result, 0);
	test_long_less_than_or_equal("expired semaphore wait returns within "
			"5 ms", elapsed_ms, ITEM_MS);
	test_long("expired semaphore wait runs no main-queue item", main_ran,
			0);

	start = wasi_test_now_ns();
	result = dispatch_group_wait(group, DISPATCH_TIME_NOW);
	elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("expired group wait with a main backlog times out", !result,
			0);
	test_long_less_than_or_equal("expired group wait returns within 5 ms",
			elapsed_ms, ITEM_MS);
	test_long("expired group wait runs no main-queue item", main_ran, 0);
	dispatch_group_leave(group);

	dispatch_release(group);
	dispatch_release(semaphore);
	dispatch_main();
}
