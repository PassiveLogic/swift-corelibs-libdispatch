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
 * A work item that queues itself again on a workloop must not keep the sole
 * thread. On threaded platforms a workloop drains until it is empty, because
 * other threads serve the rest. On WASI the workloop must run one item per
 * turn, as a serial queue does, so a timed wait still times out near its
 * deadline.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

// The wait deadline, and the longest a correct wait can take on a slow host.
#define WAIT_MS 50
#define WAIT_LATE_MS 1000

static int stop;
static long items_run;
static dispatch_workloop_t workloop;

static void
repost(void *context)
{
	(void)context;
	items_run++;
	if (!stop) {
		dispatch_async_f(workloop, NULL, repost);
	}
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Reposting Workloop");

	workloop = dispatch_workloop_create("wasi.reposting_workloop");
	dispatch_semaphore_t never = dispatch_semaphore_create(0);
	dispatch_semaphore_t drained = dispatch_semaphore_create(0);

	dispatch_async_f(workloop, NULL, repost);

	// Nothing signals this semaphore. The wait runs the workloop until the
	// deadline, then times out.
	uint64_t start = wasi_test_now_ns();
	long result = dispatch_semaphore_wait(never, dispatch_time(
			DISPATCH_TIME_NOW, WAIT_MS * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("timed semaphore wait times out while the workloop reposts "
			"itself", !result, 0);
	test_long_less_than_or_equal("timed semaphore wait returns within "
			"1000 ms", elapsed_ms, WAIT_LATE_MS);
	test_long_greater_than_or_equal("work item runs at least twice during "
			"the wait", items_run, 2);

	stop = 1;
	dispatch_async(workloop, ^{ dispatch_semaphore_signal(drained); });
	result = dispatch_semaphore_wait(drained, dispatch_time(
			DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
	test_long("workloop drains once the item stops reposting itself",
			result, 0);

	dispatch_release(never);
	dispatch_release(drained);
	dispatch_release(workloop);
	test_stop();
	return 0;
}
