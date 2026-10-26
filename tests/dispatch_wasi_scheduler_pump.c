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
 * The host scheduler callback must return without running Dispatch (see
 * private/private.h). A drain inside the callback would run the work that
 * the poking dispatch_async just recorded, on the dispatch_async stack. So
 * dispatch_main() inside the callback must crash with the named diagnostic
 * before the block runs. This is a crash test: it prints a marker before
 * the call, and a call that returns is a failure.
 * dispatch_wasi_scheduler_no_pump.c pins the control case: calls that do
 * not drain.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool submitting;
static int schedule_calls;

static void
schedule_host_turn(void *context)
{
	(void)context;
	if (schedule_calls++) return;
	puts("scheduler callback entered");
	fflush(stdout);
	dispatch_main();
}

int
main(void)
{
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	dispatch_block_t block = ^{
		if (submitting) {
			puts("FAIL: block ran inside the scheduler callback");
			fflush(stdout);
			exit(1);
		}
		exit(1);
	};
	submitting = true;
	dispatch_async(global, block);
	submitting = false;
	puts("FAIL: the scheduler callback returned");
	return 1;
}
