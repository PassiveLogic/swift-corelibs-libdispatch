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
 * A self-replenishing HIGH root queue must not starve a LOW root item: the
 * drain rotates among pending roots, so the LOW item runs partway through
 * the HIGH chain. Pumped by dispatch_main(); the last HIGH item checks and
 * stops the test.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

#define HIGH_WORK_COUNT 100

static long high_count;
static long low_seen_after = -1;

static void
default_work(void *context)
{
	(void)context;
}

static void
high_work(void *context)
{
	dispatch_queue_t queue = context;
	high_count++;
	if (high_count < HIGH_WORK_COUNT) {
		dispatch_async_f(dispatch_get_global_queue(
				DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), NULL,
				default_work);
		dispatch_async_f(queue, queue, high_work);
		return;
	}
	// the self-replenishing HIGH chain is done: the LOW item must have run
	// somewhere in the middle of it, not after it
	test_long_greater_than_or_equal("LOW item runs after the HIGH chain "
			"starts", low_seen_after, 1);
	test_long_less_than("LOW item runs before the HIGH chain ends",
			low_seen_after, HIGH_WORK_COUNT);
	test_stop();
}

static void
low_work(void *context)
{
	(void)context;
	low_seen_after = high_count;
}

static void
seed_work(void *context)
{
	(void)context;
	dispatch_queue_t low = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_LOW, 0);
	dispatch_queue_t high = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	dispatch_async_f(low, NULL, low_work);
	dispatch_async_f(high, high, high_work);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Root Fairness");

	dispatch_async_f(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), NULL, seed_work);
	dispatch_main();
}
