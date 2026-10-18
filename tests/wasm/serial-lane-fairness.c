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
 * A serial queue that keeps re-posting to itself must not hold the sole
 * thread. A root step runs one item of the lane, then re-enqueues the lane.
 * So while the lane stays busy, a 10 ms dispatch_after, a LOW global item,
 * and a main-queue item all run. Phase 1 uses a plain serial queue. Phase 2
 * uses a serial queue that targets another serial queue. Each lane must
 * keep FIFO order. Pumped by dispatch_main().
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LANE_TIME_LIMIT_NS (2 * NSEC_PER_SEC)

static dispatch_queue_t lane;
static int phase;
static int timer_fired, low_ran, main_ran;
static unsigned long lane_count, last_seq;
static uint64_t phase_start_ns;

static uint64_t
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

static void
mark(void *context)
{
	*(int *)context = 1;
}

static void start_phase(int next_phase);

static void
lane_item(void *context)
{
	unsigned long seq = (unsigned long)(uintptr_t)context;
	if (seq != last_seq + 1) {
		printf("FAIL: phase %d lane order %lu after %lu\n", phase, seq,
				last_seq);
		exit(1);
	}
	last_seq = seq;
	lane_count++;
	if (timer_fired && low_ran && main_ran) {
		if (phase == 1) {
			start_phase(2);
			return;
		}
		puts("serial lane fairness OK");
		exit(0);
	}
	if ((lane_count & 0xfff) == 0 &&
			now_ns() - phase_start_ns > LANE_TIME_LIMIT_NS) {
		printf("FAIL: phase %d lane held the thread for %lu items: "
				"timer=%d low=%d main=%d\n", phase, lane_count,
				timer_fired, low_ran, main_ran);
		exit(1);
	}
	dispatch_async_f(lane, (void *)(uintptr_t)(seq + 1), lane_item);
}

static void
start_phase(int next_phase)
{
	phase = next_phase;
	timer_fired = low_ran = main_ran = 0;
	lane_count = last_seq = 0;
	phase_start_ns = now_ns();
	if (phase == 1) {
		lane = dispatch_queue_create("serial-lane", NULL);
	} else {
		dispatch_queue_t outer = dispatch_queue_create("outer-lane", NULL);
		lane = dispatch_queue_create_with_target("inner-lane", NULL, outer);
		dispatch_release(outer);
	}
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC),
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
			&timer_fired, mark);
	dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0),
			&low_ran, mark);
	dispatch_async_f(dispatch_get_main_queue(), &main_ran, mark);
	dispatch_async_f(lane, (void *)(uintptr_t)1, lane_item);
}

int
main(void)
{
	start_phase(1);
	dispatch_main();
}
