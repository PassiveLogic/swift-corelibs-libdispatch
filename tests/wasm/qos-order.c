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
 * Root queues drain highest QoS first: a HIGH item submitted after a LOW
 * item still runs before it. The LOW item runs last and stops the test from
 * inside dispatch_main().
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static long count;
static long order[2];

int
main(void)
{
	dispatch_test_start("Dispatch WASI QoS Order");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_queue_t low = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_LOW, 0);
	dispatch_queue_t high = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	dispatch_async(global, ^{
		dispatch_async(low, ^{
			order[count++] = 1;
			test_long("both root queue items run", count, 2);
			test_long("HIGH item runs first", order[0], 2);
			test_long("LOW item runs after the HIGH item",
					order[1], 1);
			test_stop();
		});
		dispatch_async(high, ^{ order[count++] = 2; });
	});
	dispatch_main();
}
