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
 * Top-level waits must get the thread back from a serial queue that keeps
 * re-posting to itself. A timed semaphore wait returns at its deadline, and
 * a group wait returns once a lane item leaves the group, while the lane is
 * still busy.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

// The test fails when the lane runs longer than this.
#define LANE_TIME_LIMIT_NS (2 * NSEC_PER_SEC)
// The lane reads the clock once every 4096 items, to keep each item cheap.
#define CLOCK_CHECK_MASK 0xfff
// the timeout of the timed wait
#define WAIT_MS 20
// The timed wait fails when it returns later than this.
#define WAIT_LATE_MS 1000

static dispatch_queue_t lane;
static int stop;
static long lane_overran;
static unsigned long lane_count;
static uint64_t start_ns;

// reposts itself until stopped, or until the lane ran past its time limit
static void
lane_item(void *context)
{
	(void)context;
	lane_count++;
	if ((lane_count & CLOCK_CHECK_MASK) == 0 &&
			wasi_test_now_ns() - start_ns > LANE_TIME_LIMIT_NS) {
		lane_overran = 1;
		return;
	}
	if (!stop) {
		dispatch_async_f(lane, NULL, lane_item);
	}
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Serial Lane Wait");

	lane = dispatch_queue_create("wasi.serial_lane", DISPATCH_QUEUE_SERIAL);
	start_ns = wasi_test_now_ns();
	dispatch_async_f(lane, NULL, lane_item);

	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t wait_start_ns = wasi_test_now_ns();
	long result = dispatch_semaphore_wait(semaphore, dispatch_time(
			DISPATCH_TIME_NOW, WAIT_MS * NSEC_PER_MSEC));
	long waited_ms = (long)((wasi_test_now_ns() - wait_start_ns) /
			NSEC_PER_MSEC);
	test_long("timed semaphore wait times out while the lane is busy",
			!result, 0);
	test_long_greater_than_or_equal("timed semaphore wait lasts at least "
			"its 20 ms timeout", waited_ms, WAIT_MS);
	test_long_less_than_or_equal("timed semaphore wait returns within "
			"1000 ms", waited_ms, WAIT_LATE_MS);

	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_async(lane, ^{ dispatch_group_leave(group); });
	result = dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	test_long("group wait returns once a lane item leaves the group",
			result, 0);
	stop = 1;
	test_long("lane yields the thread within its 2 s time limit",
			lane_overran, 0);
	dispatch_release(group);
	dispatch_release(semaphore);
	// the queued item keeps the lane alive
	dispatch_release(lane);
	test_stop();
	return 0;
}
