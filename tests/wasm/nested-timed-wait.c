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
 * A timed semaphore wait inside a drained work item does not crash. It
 * waits for its full timeout, then reports a timeout. A far-future timer
 * stays armed meanwhile.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// the timeout of the nested wait
#define WAIT_MS 200

static uint64_t
now_ms(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return ((uint64_t)time.tv_sec * NSEC_PER_SEC + (uint64_t)time.tv_nsec) /
			NSEC_PER_MSEC;
}

int
main(void)
{
	__block int passed = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.nested.timed", NULL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	// 10 s: the timer stays armed for the whole test
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), queue, ^{});
	dispatch_async(queue, ^{
		uint64_t start = now_ms();
		long result = dispatch_semaphore_wait(semaphore,
				dispatch_time(DISPATCH_TIME_NOW, WAIT_MS * NSEC_PER_MSEC));
		uint64_t elapsed = now_ms() - start;
		passed = result != 0 && elapsed >= WAIT_MS;
		dispatch_semaphore_signal(done);
	});
	// top-level wait: pumps the queued item
	dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
	if (!passed) return 1;
	puts("probe OK");
	return 0;
}
