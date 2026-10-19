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
 * A timed wait whose deadline already passed reports a timeout without
 * running queued work. Semaphore waits and group waits follow the same rule.
 * dispatch_time(DISPATCH_TIME_NOW, 0) is the Swift wait(timeout: .now())
 * shape. It is not the DISPATCH_TIME_NOW constant, so it reaches the
 * timed-wait loop. On threaded platforms such a wait never runs client code
 * on the caller's stack, and the single-threaded port must not either. A
 * wait with time left still runs the queued work that satisfies it. An
 * expired wait leaves no stranded semaphore count.
 *
 * Each check exits with its own status, so the status names the failed check.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int ran;

static void
check(int ok, int code, const char *what, intptr_t r)
{
	if (!ok) {
		printf("FAIL: %s (r=%ld ran=%d)\n", what, (long)r, ran);
		exit(code);
	}
}

int
main(void)
{
	dispatch_queue_t q = dispatch_queue_create("expired-wait", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_group_t g = dispatch_group_create();
	intptr_t r;

	dispatch_async(q, ^{ ran++; });
	dispatch_async(q, ^{ ran++; });

	r = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 0));
	check(r != 0 && ran == 0, 1, "semaphore wait at a deadline of now", r);

	r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, -(int64_t)NSEC_PER_MSEC));
	check(r != 0 && ran == 0, 2, "semaphore wait at a past deadline", r);

	dispatch_group_enter(g);
	r = dispatch_group_wait(g, dispatch_time(DISPATCH_TIME_NOW, 0));
	check(r != 0 && ran == 0, 3, "group wait at a deadline of now", r);
	dispatch_group_leave(g);

	// the queued signal is not run by the expired wait
	dispatch_async(q, ^{ ran++; dispatch_semaphore_signal(sem); });
	r = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 0));
	check(r != 0 && ran == 0, 4, "expired wait ran the signaling item", r);

	// a wait with time left runs the three queued items and consumes the
	// signal. 2 s is far longer than the items need.
	r = dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, 2 * (int64_t)NSEC_PER_SEC));
	check(r == 0 && ran == 3, 5, "timed wait with time left", r);

	// the expired waits left no stranded count behind
	r = dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW);
	check(r != 0, 6, "stranded semaphore count", r);

	printf("expired wait OK\n");
	return 0;
}
