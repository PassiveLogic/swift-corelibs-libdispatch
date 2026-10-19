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


// The host scheduler callback must return without running Dispatch (see
// private/private.h). A pump point inside the callback would drain the work
// that the poking dispatch_async just recorded, on the dispatch_async stack.
// So each pumping call must crash with the named diagnostic before the block
// runs. SCHEDULER_PUMP_MODE selects the call that the callback makes:
//   0 dispatch_semaphore_wait(DISPATCH_TIME_FOREVER)
//   1 dispatch_semaphore_wait with a 100 ms timeout
//   2 dispatch_main()
//   3 dispatch_sync on a serial queue that holds the submitted block
//   4 dispatch_group_wait(DISPATCH_TIME_FOREVER)
//   5 control: calls that do not pump (dispatch_async, an uncontended
//     dispatch_sync, a DISPATCH_TIME_NOW wait) must not crash

#include <dispatch/dispatch.h>
#define __DISPATCH_BUILDING_DISPATCH__ 1
#include <private/private.h>
#undef __DISPATCH_BUILDING_DISPATCH__
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SCHEDULER_PUMP_MODE
#error define SCHEDULER_PUMP_MODE
#endif

static dispatch_semaphore_t semaphore;
static dispatch_group_t group;
static dispatch_queue_t serial;
static bool submitting;
static int block_runs;
static int extra_runs;
static int schedule_calls;

static void
schedule_host_turn(void *context)
{
	(void)context;
	if (schedule_calls++) return;
	puts("scheduler callback entered");
	fflush(stdout);
	switch (SCHEDULER_PUMP_MODE) {
	case 0:
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
		break;
	case 1:
		(void)dispatch_semaphore_wait(semaphore,
				dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC));
		break;
	case 2:
		dispatch_main();
	case 3:
		dispatch_sync(serial, ^{});
		break;
	case 4:
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		break;
	default: {
		dispatch_async(dispatch_get_global_queue(0, 0), ^{ extra_runs++; });
		dispatch_queue_t idle = dispatch_queue_create("wasi.idle", NULL);
		dispatch_sync(idle, ^{});
		dispatch_release(idle);
		if (!dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW)) {
			puts("FAIL: DISPATCH_TIME_NOW wait succeeded");
			exit(1);
		}
		break;
	}
	}
}

int
main(void)
{
	semaphore = dispatch_semaphore_create(0);
	group = dispatch_group_create();
	serial = dispatch_queue_create("wasi.scheduler.pump", NULL);
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	dispatch_block_t block = ^{
		if (submitting) {
			puts("FAIL: block ran inside the scheduler callback");
			fflush(stdout);
			exit(1);
		}
		block_runs++;
		dispatch_semaphore_signal(semaphore);
	};
	submitting = true;
	if (SCHEDULER_PUMP_MODE == 3) {
		dispatch_async(serial, block);
	} else if (SCHEDULER_PUMP_MODE == 4) {
		dispatch_group_async(group, dispatch_get_global_queue(0, 0), block);
	} else {
		dispatch_async(dispatch_get_global_queue(0, 0), block);
	}
	submitting = false;
	if (block_runs || schedule_calls != 1) {
		printf("FAIL: runs=%d calls=%d\n", block_runs, schedule_calls);
		return 1;
	}
	// the host turn that the callback asked for. 8 turns is more than the
	// two blocks need.
	for (int turn = 0; turn < 8; turn++) {
		if (!_dispatch_wasi_event_loop_perform(1, true)) break;
	}
	if (block_runs != 1 || extra_runs != 1) {
		printf("FAIL: runs=%d extra=%d\n", block_runs, extra_runs);
		return 1;
	}
	puts("scheduler non-pumping calls OK");
	return 0;
}
