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
 * A timed semaphore wait reports success when the signal arrives before the
 * wait can return, even after the deadline. A TIMER_MS timer fires while the
 * WAIT_MS wait parks. Its handler spins for SPIN_MS, then signals. The
 * cooperative drain cannot preempt the handler at the deadline. So when the
 * wait can return, the semaphore has a signal. The wait must consume it and
 * report success. A "timed out" result would strand the count. Then a later
 * unrelated wait would succeed with no matching signal.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// the timer fires during the wait
#define TIMER_MS 10
// the wait deadline passes while the handler spins
#define WAIT_MS 20
#define SPIN_MS 50

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
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
			(int64_t)(TIMER_MS * NSEC_PER_MSEC)),
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		uint64_t until = mono_ns() + SPIN_MS * NSEC_PER_MSEC;
		while (mono_ns() < until) { }
		dispatch_semaphore_signal(sem);
	});
	intptr_t r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, (int64_t)(WAIT_MS * NSEC_PER_MSEC)));
	if (r != 0) {
		printf("FAIL: wait reported timeout though the signal landed before "
				"it could return (r=%ld)\n", (long)r);
		if (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0) {
			printf("FAIL: stranded count consumed by unrelated wait\n");
		}
		exit(1);
	}
	// the wait consumed the signal, so an immediate retry must time out
	if (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0) {
		printf("FAIL: signal double-delivered\n");
		exit(1);
	}
	printf("late signal OK\n");
	return 0;
}
