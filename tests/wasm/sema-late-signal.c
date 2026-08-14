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
 * A timed semaphore wait whose deadline lapses while the cooperative drain is
 * running the very item that signals it: a 10ms timer fires during the 20ms
 * wait's park, and its handler burns 50ms before signaling. The drain cannot
 * be preempted at the deadline, so by the time the wait can return the
 * semaphore has been signaled - the wait must report success and consume the
 * signal, never "timed out" with the count stranded (which would make a later
 * unrelated wait succeed with no matching signal).
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static uint64_t
mono_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

int
main(void)
{
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_MSEC)),
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		uint64_t until = mono_ns() + 50 * NSEC_PER_MSEC;
		while (mono_ns() < until) { }
		dispatch_semaphore_signal(sem);
	});
	intptr_t r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, (int64_t)(20 * NSEC_PER_MSEC)));
	if (r != 0) {
		printf("FAIL: wait reported timeout though the signal landed before "
				"it could return (r=%ld)\n", (long)r);
		if (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0) {
			printf("FAIL: stranded count consumed by unrelated wait\n");
		}
		exit(1);
	}
	// the signal was consumed by the wait: an immediate retry must time out
	if (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0) {
		printf("FAIL: signal double-delivered\n");
		exit(1);
	}
	printf("late signal OK\n");
	return 0;
}
