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
 * A dispatch_after with a dispatch_walltime() deadline fires on a serial
 * queue while main blocks in a timed semaphore wait, and not before its
 * 20 ms deadline.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Wall Timer");

	dispatch_queue_t queue = dispatch_queue_create("wasi.wall.timer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = wasi_test_now_ns();
	dispatch_after(dispatch_walltime(NULL, 20 * NSEC_PER_MSEC), queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	dispatch_release(semaphore);
	dispatch_release(queue);
	test_long("walltime timer signals before the 500 ms wait times out",
			result, 0);
	test_long_greater_than_or_equal("walltime timer fires no earlier than "
			"20 ms", elapsed_ms, 20);
	test_stop();
	return 0;
}
