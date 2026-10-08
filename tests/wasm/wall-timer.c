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

#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint64_t
now_ms(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.wall.timer", NULL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = now_ms();
	dispatch_after(dispatch_walltime(NULL, 20 * NSEC_PER_MSEC), queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
	uint64_t elapsed = now_ms() - start;
	dispatch_release(semaphore);
	dispatch_release(queue);
	if (result != 0 || elapsed < 20) return 1;
	puts("wall timer OK");
	return 0;
}
