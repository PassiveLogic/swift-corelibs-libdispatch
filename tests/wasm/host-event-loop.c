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
 * A reactor module for run-wasi-host-event-loop-test.mjs. The runner is the
 * host event loop. It calls these exports, serves each scheduler call with a
 * host turn, and checks the counters below.
 */
#include <dispatch/dispatch.h>
#define __DISPATCH_BUILDING_DISPATCH__ 1
#include <private/private.h>
#undef __DISPATCH_BUILDING_DISPATCH__
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void host_event_loop_initialize(void);
void host_event_loop_register_from_work_item(void);
int host_event_loop_schedule_calls_at_registration(void);
int host_event_loop_submit(void);
int host_event_loop_submit_second_burst(void);
int host_event_loop_submit_timer_only(void);
int host_event_loop_perform(void);
int host_event_loop_perform_timer(void);
int64_t host_event_loop_next_timer_delay(void);
int host_event_loop_immediate_count(void);
int host_event_loop_result(void);
int host_event_loop_submit_serial_lane(void);
int host_event_loop_serial_lane_count(void);
int host_event_loop_serial_lane_low_seen_at(void);

__attribute__((import_module("dispatch_host"), import_name("schedule")))
extern void host_schedule(void);

#define IMMEDIATE_ITEMS 2

static int immediate_count;
static int immediate_order[IMMEDIATE_ITEMS];
static int timer_count;
static int second_burst_count;
static int schedule_calls;
static int schedule_calls_at_registration = -1;

// Records one immediate item. The runner submits the items once, but the
// bounds check stops an extra item from writing past the end of the array.
static void
record_immediate(int value)
{
	if (immediate_count >= IMMEDIATE_ITEMS) {
		fprintf(stderr, "FAIL: more than %d immediate items\n",
				IMMEDIATE_ITEMS);
		abort();
	}
	immediate_order[immediate_count++] = value;
}

static void
schedule_host_turn(void *context)
{
	(void)context;
	schedule_calls++;
	host_schedule();
}

__attribute__((export_name("host_event_loop_initialize")))
void
host_event_loop_initialize(void)
{
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
}

// Registers from inside a running work item, pumped by a top-level wait.
// Registration inside a drain must not request a turn itself. The drain
// step hands over the work that is still pending when it ends.
__attribute__((export_name("host_event_loop_register_from_work_item")))
void
host_event_loop_register_from_work_item(void)
{
	dispatch_queue_t queue = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	dispatch_async(queue, ^{
		_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
		// still inside the drain step: no turn may have been requested yet
		schedule_calls_at_registration = schedule_calls;
	});
	dispatch_async(queue, ^{ dispatch_semaphore_signal(done); });
	dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
	dispatch_release(done);
}

__attribute__((export_name("host_event_loop_schedule_calls_at_registration")))
int
host_event_loop_schedule_calls_at_registration(void)
{
	return schedule_calls_at_registration;
}

__attribute__((export_name("host_event_loop_submit")))
int
host_event_loop_submit(void)
{
	dispatch_queue_t queue = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_async(queue, ^{ record_immediate(1); });
	dispatch_async(queue, ^{ record_immediate(2); });
	return immediate_count + timer_count;
}

__attribute__((export_name("host_event_loop_submit_second_burst")))
int
host_event_loop_submit_second_burst(void)
{
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		second_burst_count++;
	});
	return second_burst_count;
}

__attribute__((export_name("host_event_loop_submit_timer_only")))
int
host_event_loop_submit_timer_only(void)
{
	// 250 ms: long enough that the runner must arm a host timeout
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC),
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		timer_count++;
	});
	return timer_count;
}

__attribute__((export_name("host_event_loop_perform")))
int
host_event_loop_perform(void)
{
	// one drain step, in the turn that the scheduler requested
	return _dispatch_wasi_event_loop_perform(1, true);
}

__attribute__((export_name("host_event_loop_perform_timer")))
int
host_event_loop_perform_timer(void)
{
	// one drain step from a host timeout, which consumes no requested turn
	return _dispatch_wasi_event_loop_perform(1, false);
}

__attribute__((export_name("host_event_loop_next_timer_delay")))
int64_t
host_event_loop_next_timer_delay(void)
{
	return _dispatch_wasi_event_loop_next_timer_delay();
}

__attribute__((export_name("host_event_loop_immediate_count")))
int
host_event_loop_immediate_count(void)
{
	return immediate_count;
}

__attribute__((export_name("host_event_loop_result")))
int
host_event_loop_result(void)
{
	if (immediate_count == IMMEDIATE_ITEMS &&
			(immediate_order[0] != 1 || immediate_order[1] != 2)) {
		return -1;
	}
	// one decimal digit for each counter: 211 means 2 immediate items,
	// 1 timer, and 1 second-burst item
	return immediate_count * 100 + timer_count * 10 + second_burst_count;
}

// A serial queue that re-posts to itself SERIAL_LANE_ITEMS times, plus one
// LOW global item. The runner checks that each perform(1) advances the lane
// by at most one item. It also checks that the LOW item runs while the lane
// is busy. The runner expects 100 items.
#define SERIAL_LANE_ITEMS 100

static dispatch_queue_t serial_lane;
static int serial_lane_count;
static int serial_lane_low_seen_at = -1;

static void
serial_lane_item(void *context)
{
	(void)context;
	if (++serial_lane_count < SERIAL_LANE_ITEMS) {
		dispatch_async_f(serial_lane, NULL, serial_lane_item);
	}
}

static void
serial_lane_low_item(void *context)
{
	(void)context;
	serial_lane_low_seen_at = serial_lane_count;
}

__attribute__((export_name("host_event_loop_submit_serial_lane")))
int
host_event_loop_submit_serial_lane(void)
{
	serial_lane = dispatch_queue_create("host-serial-lane", NULL);
	dispatch_async_f(serial_lane, NULL, serial_lane_item);
	dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0),
			NULL, serial_lane_low_item);
	return serial_lane_count;
}

__attribute__((export_name("host_event_loop_serial_lane_count")))
int
host_event_loop_serial_lane_count(void)
{
	return serial_lane_count;
}

__attribute__((export_name("host_event_loop_serial_lane_low_seen_at")))
int
host_event_loop_serial_lane_low_seen_at(void)
{
	return serial_lane_low_seen_at;
}
