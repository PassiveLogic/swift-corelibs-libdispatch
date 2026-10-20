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
 * A barrier submitted between two concurrent-queue items orders them: the
 * final barrier observes all three in submission order, then stops the test
 * from inside dispatch_main().
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static long count;
static long order[3];

int
main(void)
{
	dispatch_test_start("Dispatch WASI Barrier Order");

	dispatch_queue_t queue = dispatch_queue_create("wasi.barrier",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_async(global, ^{
		dispatch_async(queue, ^{ order[count++] = 1; });
		dispatch_barrier_async(queue, ^{ order[count++] = 2; });
		dispatch_async(queue, ^{ order[count++] = 3; });
		dispatch_barrier_async(queue, ^{
			test_long("final barrier runs after all three items",
					count, 3);
			test_long("item submitted first runs first",
					order[0], 1);
			test_long("barrier runs after the item submitted "
					"before it", order[1], 2);
			test_long("item submitted after the barrier runs "
					"after it", order[2], 3);
			// the system keeps the queue alive until this
			// block returns
			dispatch_release(queue);
			test_stop();
		});
	});
	dispatch_main();
}
