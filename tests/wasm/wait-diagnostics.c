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

// Pins that each indefinite-wait deadlock diagnostic names the public call
// that blocked. A crash case per build: WAIT_DIAG_SYNC parks a dispatch_sync
// behind queued work, WAIT_DIAG_GROUP parks a dispatch_group_wait, and
// WAIT_DIAG_CANCEL parks dispatch_source_cancel_and_wait on a timer source.
// Each one runs inside a drained work item, so nothing can satisfy it.
// nested-wait.c pins the dispatch_semaphore_wait case.

#include <dispatch/dispatch.h>
#include <stdio.h>

// declared in private/source_private.h in this tree
extern void dispatch_source_cancel_and_wait(dispatch_source_t source);

int
main(void)
{
	dispatch_queue_t global = dispatch_get_global_queue(0, 0);
#if WAIT_DIAG_SYNC
	dispatch_queue_t queue = dispatch_queue_create("wasi.wait.diag", NULL);
	dispatch_async(global, ^{
		puts("nested sync starting");
		dispatch_async(queue, ^{});
		dispatch_sync(queue, ^{});
	});
#elif WAIT_DIAG_GROUP
	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_async(global, ^{
		puts("nested group wait starting");
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	});
#elif WAIT_DIAG_CANCEL
	dispatch_queue_t queue = dispatch_queue_create("wasi.wait.diag", NULL);
	dispatch_source_t timer = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW,
			60 * NSEC_PER_SEC), DISPATCH_TIME_FOREVER, 0);
	dispatch_source_set_event_handler(timer, ^{});
	dispatch_activate(timer);
	dispatch_async(global, ^{
		puts("nested cancel_and_wait starting");
		// a timer source always waits for the manager to unregister it
		dispatch_source_cancel_and_wait(timer);
	});
#else
#error "define one WAIT_DIAG_* case"
#endif
	dispatch_main();
}
