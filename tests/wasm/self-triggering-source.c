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
 * A data source whose handler merges data into the same source must not keep
 * the sole thread. The source targets the default overcommit root queue (a
 * NULL target queue). On threaded platforms such a source may keep one
 * overcommit thread, because another thread serves the rest. On WASI the
 * source must re-queue after each handler call, so a timed wait still times
 * out near its deadline.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

// The wait deadline, and the longest a correct wait can take on a slow host.
#define WAIT_MS 50
#define WAIT_LATE_MS 1000

static int stop;
static long handler_calls;

int
main(void)
{
	dispatch_test_start("Dispatch WASI Self Triggering Source");

	dispatch_source_t source = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, NULL);
	dispatch_semaphore_t never = dispatch_semaphore_create(0);
	dispatch_semaphore_t cancelled = dispatch_semaphore_create(0);

	dispatch_source_set_event_handler(source, ^{
		handler_calls++;
		if (!stop) {
			dispatch_source_merge_data(source, 1);
		}
	});
	dispatch_source_set_cancel_handler(source, ^{
		dispatch_semaphore_signal(cancelled);
	});
	dispatch_resume(source);
	dispatch_source_merge_data(source, 1);

	// Nothing signals this semaphore. The wait runs the source until the
	// deadline, then times out.
	uint64_t start = wasi_test_now_ns();
	long result = dispatch_semaphore_wait(never, dispatch_time(
			DISPATCH_TIME_NOW, WAIT_MS * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("timed wait that nothing signals times out", !result, 0);
	test_long_less_than_or_equal("source yields the thread so the wait "
			"returns within 1000 ms", elapsed_ms, WAIT_LATE_MS);
	test_long_greater_than_or_equal("event handler runs again after it "
			"merges data into its own source", handler_calls, 2);

	stop = 1;
	dispatch_source_cancel(source);
	result = dispatch_semaphore_wait(cancelled,
			dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
	test_long("cancel handler runs before the 2 s wait times out", result,
			0);

	dispatch_release(source);
	dispatch_release(never);
	dispatch_release(cancelled);
	test_stop();
	return 0;
}
