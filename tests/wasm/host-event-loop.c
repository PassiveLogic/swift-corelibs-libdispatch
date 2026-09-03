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

#include <dispatch/dispatch.h>
#define __DISPATCH_BUILDING_DISPATCH__ 1
#include <private/private.h>
#undef __DISPATCH_BUILDING_DISPATCH__
#include <stdint.h>

void host_event_loop_initialize(void);
void host_event_loop_register_during_drain(void);
int host_event_loop_submit(void);
int host_event_loop_submit_second_burst(void);
int host_event_loop_submit_timer_only(void);
int host_event_loop_perform(void);
int host_event_loop_perform_timer(void);
int64_t host_event_loop_next_timer_delay(void);
int host_event_loop_immediate_count(void);
int host_event_loop_result(void);

__attribute__((import_module("dispatch_host"), import_name("schedule")))
extern void host_schedule(void);

static int immediate_count;
static int immediate_order[2];
static int timer_count;
static int second_burst_count;

static void
schedule_host_turn(void *context)
{
	(void)context;
	host_schedule();
}

__attribute__((export_name("host_event_loop_initialize")))
void
host_event_loop_initialize(void)
{
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
}

__attribute__((export_name("host_event_loop_register_during_drain")))
void
host_event_loop_register_during_drain(void)
{
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	});
}

__attribute__((export_name("host_event_loop_submit")))
int
host_event_loop_submit(void)
{
	dispatch_queue_t queue = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_async(queue, ^{
		immediate_order[immediate_count++] = 1;
	});
	dispatch_async(queue, ^{
		immediate_order[immediate_count++] = 2;
	});
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
	return _dispatch_wasi_event_loop_perform(1, true);
}

__attribute__((export_name("host_event_loop_perform_timer")))
int
host_event_loop_perform_timer(void)
{
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
	if (immediate_count == 2 &&
			(immediate_order[0] != 1 || immediate_order[1] != 2)) {
		return -1;
	}
	return immediate_count * 100 + timer_count * 10 + second_burst_count;
}
