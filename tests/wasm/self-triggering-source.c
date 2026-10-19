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
 *
 * Each check exits with its own status, so the status names the failed check.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// The wait deadline, and the longest a correct wait can take on a slow host.
#define WAIT_MS 50
#define WAIT_LATE_MS 1000

static volatile int stop;
static long handler_calls;

static uint64_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec) /
			NSEC_PER_MSEC;
}

static void
check(int ok, int code, const char *what)
{
	if (!ok) {
		printf("FAIL: %s (handler calls %ld)\n", what, handler_calls);
		exit(code);
	}
}

int
main(void)
{
	dispatch_source_t s = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, NULL);
	dispatch_semaphore_t never = dispatch_semaphore_create(0);
	dispatch_semaphore_t cancelled = dispatch_semaphore_create(0);

	dispatch_source_set_event_handler(s, ^{
		handler_calls++;
		if (!stop) dispatch_source_merge_data(s, 1);
	});
	dispatch_source_set_cancel_handler(s, ^{
		dispatch_semaphore_signal(cancelled);
	});
	dispatch_resume(s);
	dispatch_source_merge_data(s, 1);

	// Nothing signals this semaphore. The wait runs the source until the
	// deadline, then times out.
	uint64_t start = now_ms();
	long r = dispatch_semaphore_wait(never,
			dispatch_time(DISPATCH_TIME_NOW, WAIT_MS * (int64_t)NSEC_PER_MSEC));
	uint64_t elapsed = now_ms() - start;
	check(r != 0, 1, "the wait did not time out");
	check(elapsed < WAIT_LATE_MS, 2, "the source kept the thread past the "
			"wait deadline");
	check(handler_calls > 1, 3, "the handler did not run again");

	stop = 1;
	dispatch_source_cancel(s);
	r = dispatch_semaphore_wait(cancelled,
			dispatch_time(DISPATCH_TIME_NOW, 2 * (int64_t)NSEC_PER_SEC));
	check(r == 0, 4, "the source did not finish its cancel");

	printf("self-triggering source OK\n");
	return 0;
}
