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
 * Each indefinite-wait deadlock diagnostic names the public call that
 * blocked. Each build selects one crash case:
 *  - WAIT_DIAG_SYNC: a dispatch_sync waits behind queued work
 *  - WAIT_DIAG_GROUP: a dispatch_group_wait waits
 *  - WAIT_DIAG_CANCEL: dispatch_source_cancel_and_wait waits on a timer
 * Each wait runs inside a drained work item, so nothing can satisfy it.
 * nested-wait.c covers the dispatch_semaphore_wait case.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
#if WAIT_DIAG_SYNC
	dispatch_queue_t queue = dispatch_queue_create("wasi.wait.diag",
			DISPATCH_QUEUE_SERIAL);
	dispatch_async(global, ^{
		puts("nested sync starting");
		fflush(stdout);
		dispatch_async(queue, ^{});
		dispatch_sync(queue, ^{});
	});
#elif WAIT_DIAG_GROUP
	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_async(global, ^{
		puts("nested group wait starting");
		fflush(stdout);
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	});
#elif WAIT_DIAG_CANCEL
	dispatch_queue_t queue = dispatch_queue_create("wasi.wait.diag",
			DISPATCH_QUEUE_SERIAL);
	dispatch_source_t timer = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	// 60 s: the timer must not fire during the test
	dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW,
			60 * NSEC_PER_SEC), DISPATCH_TIME_FOREVER, 0);
	dispatch_source_set_event_handler(timer, ^{});
	dispatch_activate(timer);
	dispatch_async(global, ^{
		puts("nested cancel_and_wait starting");
		fflush(stdout);
		// a timer source always waits for the manager to unregister it
		dispatch_source_cancel_and_wait(timer);
	});
#else
#error "define one WAIT_DIAG_* case"
#endif
	dispatch_main();
}
