/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
static unsigned long lane_count;
static uint64_t start_ns;

static uint64_t
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

static void
lane_item(void *context)
{
	(void)context;
	lane_count++;
	if ((lane_count & CLOCK_CHECK_MASK) == 0 &&
			now_ns() - start_ns > LANE_TIME_LIMIT_NS) {
		printf("FAIL: lane held the thread for %lu items\n", lane_count);
		exit(1);
	}
	if (!stop) dispatch_async_f(lane, NULL, lane_item);
}

int
main(void)
{
	lane = dispatch_queue_create("serial-lane", NULL);
	start_ns = now_ns();
	dispatch_async_f(lane, NULL, lane_item);

	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	uint64_t wait_start = now_ns();
	long result = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, WAIT_MS * NSEC_PER_MSEC));
	uint64_t waited_ms = (now_ns() - wait_start) / NSEC_PER_MSEC;
	if (result == 0 || waited_ms < WAIT_MS || waited_ms > WAIT_LATE_MS) {
		printf("FAIL: timed wait result=%ld after %llu ms\n", result,
				(unsigned long long)waited_ms);
		return 1;
	}

	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_async(lane, ^{ dispatch_group_leave(group); });
	dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	stop = 1;
	dispatch_release(group);
	dispatch_release(sem);
	puts("serial lane wait OK");
	return 0;
}
