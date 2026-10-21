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
 * A source applies all of its queued changes before it calls a handler. Here
 * the source targets the main queue, and its first handler sets the event
 * handler two times. These changes wait on the source, because the handler
 * owns the source. On WASI, a main-queue step narrows the lanes that target
 * the main queue. The source drains its own changes with narrowing disabled,
 * so the next event must call the last handler.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static dispatch_source_t source;

// the handler that the second event calls
static void
finish(long handler)
{
	test_long("the second event calls the last handler", handler, 2);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Source Handlers");

	source = dispatch_source_create(DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0,
			dispatch_get_main_queue());
	dispatch_source_set_event_handler(source, ^{
		dispatch_source_set_event_handler(source, ^{ finish(1); });
		dispatch_source_set_event_handler(source, ^{ finish(2); });
		dispatch_source_merge_data(source, 1);
	});
	dispatch_resume(source);
	dispatch_source_merge_data(source, 1);
	dispatch_main();
}
