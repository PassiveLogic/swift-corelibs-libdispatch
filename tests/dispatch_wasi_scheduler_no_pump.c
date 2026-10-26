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
 * The host scheduler callback may make calls that do not drain Dispatch (see
 * private/private.h): a dispatch_async, an uncontended dispatch_sync, and a
 * DISPATCH_TIME_NOW wait. None of them runs the block that the poking
 * dispatch_async just recorded, the callback runs once for both
 * dispatch_async calls, and the host turn that the callback asked for then
 * runs both blocks. dispatch_wasi_scheduler_pump.c pins the crash of a call
 * that drains.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdbool.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static dispatch_semaphore_t semaphore;
static bool submitting;
static long block_runs;
static long extra_runs;
static long runs_in_callback;
static long schedule_calls;
static long now_wait_result;

static void
schedule_host_turn(void *context)
{
	(void)context;
	if (schedule_calls++) return;
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		extra_runs++;
	});
	dispatch_queue_t idle = dispatch_queue_create("wasi.idle",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync(idle, ^{});
	dispatch_release(idle);
	now_wait_result = dispatch_semaphore_wait(semaphore,
			DISPATCH_TIME_NOW);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Scheduler No Pump");

	semaphore = dispatch_semaphore_create(0);
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	dispatch_block_t block = ^{
		if (submitting) {
			runs_in_callback++;
		}
		block_runs++;
		dispatch_semaphore_signal(semaphore);
	};
	submitting = true;
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), block);
	submitting = false;
	test_long("block does not run inside the scheduler callback",
			runs_in_callback, 0);
	test_long("block does not run before the host turn", block_runs, 0);
	test_long("both dispatch_async calls request one host turn",
			schedule_calls, 1);
	test_long("DISPATCH_TIME_NOW wait inside the scheduler callback times "
			"out", !now_wait_result, 0);

	// the host turn that the callback asked for. 8 turns is more than the
	// two blocks need.
	for (int turn = 0; turn < 8; turn++) {
		if (!_dispatch_wasi_event_loop_perform(1, true)) break;
	}
	test_long("host turn runs the submitted block once", block_runs, 1);
	test_long("host turn runs the block queued by the callback once",
			extra_runs, 1);

	dispatch_release(semaphore);
	test_stop();
	return 0;
}
