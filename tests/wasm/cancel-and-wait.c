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

// Pins the boundary of dispatch_source_cancel_and_wait on single-threaded
// WASI. From top level, a timer source cancels: the wait pumps the manager
// queue until it unregisters the source. From inside a work item, a source
// that cancels inline (a DATA_ADD source on an idle queue) needs no wait and
// returns. wait-diagnostics.c pins the timer case inside a work item, which
// crashes with a named diagnostic.

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

// declared in private/source_private.h in this tree
extern void dispatch_source_cancel_and_wait(dispatch_source_t source);

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.cancel.wait", NULL);
	dispatch_source_t timer = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW,
			60 * NSEC_PER_SEC), DISPATCH_TIME_FOREVER, 0);
	dispatch_source_set_event_handler(timer, ^{});
	dispatch_activate(timer);
	dispatch_source_cancel_and_wait(timer);
	if (!dispatch_source_testcancel(timer)) {
		puts("FAIL: timer not canceled");
		return 1;
	}

	dispatch_source_t data = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, queue);
	dispatch_source_set_event_handler(data, ^{});
	dispatch_activate(data);
	dispatch_async(dispatch_get_global_queue(0, 0), ^{
		dispatch_source_cancel_and_wait(data);
		if (!dispatch_source_testcancel(data)) {
			puts("FAIL: data source not canceled");
			exit(1);
		}
		puts("cancel and wait OK");
		exit(0);
	});
	dispatch_main();
}
