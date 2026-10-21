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
 * dispatch_source_cancel_and_wait returns in two cases on single-threaded
 * WASI:
 *  - At top level, a timer source cancels. The wait pumps the manager queue
 *    until the manager unregisters the source.
 *  - Inside a work item, a source that cancels inline needs no wait. A
 *    DATA_ADD source on an idle queue is such a source.
 * wait-diagnostics.c covers a timer source inside a work item. That case
 * crashes with a named diagnostic.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Cancel And Wait");

	dispatch_queue_t queue = dispatch_queue_create("wasi.cancel.wait",
			DISPATCH_QUEUE_SERIAL);
	dispatch_source_t timer = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	// 60 s: the timer must not fire during the test
	dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW,
			60 * NSEC_PER_SEC), DISPATCH_TIME_FOREVER, 0);
	dispatch_source_set_event_handler(timer, ^{});
	dispatch_activate(timer);
	dispatch_source_cancel_and_wait(timer);
	test_long("top-level dispatch_source_cancel_and_wait cancels the timer "
			"source", !dispatch_source_testcancel(timer), 0);
	dispatch_release(timer);

	dispatch_source_t data = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, queue);
	dispatch_source_set_event_handler(data, ^{});
	dispatch_activate(data);
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_source_cancel_and_wait(data);
		test_long("dispatch_source_cancel_and_wait inside a work item "
				"cancels the data source",
				!dispatch_source_testcancel(data), 0);
		dispatch_release(data);
		dispatch_release(queue);
		test_stop();
	});
	dispatch_main();
}
