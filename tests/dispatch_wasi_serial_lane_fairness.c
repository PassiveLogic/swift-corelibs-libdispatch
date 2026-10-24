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
 * A serial queue that keeps re-posting to itself must not hold the sole
 * thread. A root step runs one item of the lane, then re-enqueues the lane.
 * So while the lane stays busy, a 10 ms dispatch_after, a LOW global item,
 * and a main-queue item all run. Phase 1 uses a plain serial queue. Phase 2
 * uses a serial queue that targets another serial queue. Each lane must
 * keep FIFO order. Pumped by dispatch_main().
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// The test fails when a phase runs longer than this. A fair drain finishes
// each phase soon after its 10 ms timer fires.
#define LANE_TIME_LIMIT_NS (2 * NSEC_PER_SEC)
// The lane reads the clock once every 4096 items, to keep each item cheap.
#define CLOCK_CHECK_MASK 0xfff

static dispatch_queue_t lane;
static int phase;
static long timer_fired, low_ran, main_ran;
static unsigned long lane_count, last_seq;
// lane items that did not run right after the item submitted before them
static long out_of_order;
static uint64_t phase_start_ns;

static void
mark(void *context)
{
	*(long *)context = 1;
}

static void start_phase(int next_phase);

static void
check_phase(void)
{
	if (phase == 1) {
		test_long("plain serial lane runs its items in FIFO order",
				out_of_order, 0);
		test_long("10 ms dispatch_after runs while the plain serial "
				"lane is busy", timer_fired, 1);
		test_long("LOW global item runs while the plain serial lane is "
				"busy", low_ran, 1);
		test_long("main-queue item runs while the plain serial lane is "
				"busy", main_ran, 1);
		return;
	}
	test_long("lane targeting a serial queue runs its items in FIFO order",
			out_of_order, 0);
	test_long("10 ms dispatch_after runs while the lane targeting a serial "
			"queue is busy", timer_fired, 1);
	test_long("LOW global item runs while the lane targeting a serial "
			"queue is busy", low_ran, 1);
	test_long("main-queue item runs while the lane targeting a serial "
			"queue is busy", main_ran, 1);
}

static void
lane_item(void *context)
{
	unsigned long seq = (unsigned long)(uintptr_t)context;
	if (seq != last_seq + 1) {
		out_of_order++;
	}
	last_seq = seq;
	lane_count++;
	if (timer_fired && low_ran && main_ran) {
		check_phase();
		// the system keeps the lane alive until this item returns
		dispatch_release(lane);
		if (phase == 1) {
			start_phase(2);
			return;
		}
		test_stop();
	}
	if ((lane_count & CLOCK_CHECK_MASK) == 0 &&
			dispatch_time(DISPATCH_TIME_NOW, 0) - phase_start_ns >
			LANE_TIME_LIMIT_NS) {
		// the lane held the thread: the check for each item that did
		// not run fails
		check_phase();
		test_stop();
	}
	dispatch_async_f(lane, (void *)(uintptr_t)(seq + 1), lane_item);
}

static void
start_phase(int next_phase)
{
	phase = next_phase;
	timer_fired = low_ran = main_ran = 0;
	lane_count = last_seq = 0;
	out_of_order = 0;
	phase_start_ns = dispatch_time(DISPATCH_TIME_NOW, 0);
	if (phase == 1) {
		lane = dispatch_queue_create("wasi.serial_lane",
				DISPATCH_QUEUE_SERIAL);
	} else {
		dispatch_queue_t outer = dispatch_queue_create(
				"wasi.serial_lane.outer",
				DISPATCH_QUEUE_SERIAL);
		lane = dispatch_queue_create_with_target(
				"wasi.serial_lane.inner", DISPATCH_QUEUE_SERIAL,
				outer);
		dispatch_release(outer);
	}
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_queue_t low = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_LOW, 0);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,
			10 * NSEC_PER_MSEC), global, &timer_fired, mark);
	dispatch_async_f(low, &low_ran, mark);
	dispatch_async_f(dispatch_get_main_queue(), &main_ran, mark);
	dispatch_async_f(lane, (void *)(uintptr_t)1, lane_item);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Serial Lane Fairness");

	start_phase(1);
	dispatch_main();
}
