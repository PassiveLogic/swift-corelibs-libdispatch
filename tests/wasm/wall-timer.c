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
 * A dispatch_after with a walltime deadline fires, and a top-level timed
 * semaphore wait returns when its handler signals.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// the walltime delay of the timer
#define TIMER_MS 20
// the timeout of the wait, far longer than the timer delay
#define WAIT_TIMEOUT_MS 500

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
	dispatch_queue_t queue = dispatch_queue_create("wasi.wall.timer", NULL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = now_ms();
	dispatch_after(dispatch_walltime(NULL, TIMER_MS * NSEC_PER_MSEC), queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, WAIT_TIMEOUT_MS * NSEC_PER_MSEC));
	uint64_t elapsed = now_ms() - start;
	dispatch_release(semaphore);
	dispatch_release(queue);
	if (result != 0 || elapsed < TIMER_MS) return 1;
	puts("wall timer OK");
	return 0;
}
