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
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static long ran;

int
main(void)
{
	dispatch_test_start("Dispatch WASI Expired Wait");

	dispatch_queue_t queue = dispatch_queue_create("wasi.expired_wait",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_group_t group = dispatch_group_create();
	long result;

	dispatch_async(queue, ^{ ran++; });
	dispatch_async(queue, ^{ ran++; });

	result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 0));
	test_long("semaphore wait at a deadline of now times out", !result, 0);
	test_long("semaphore wait at a deadline of now runs no queued work",
			ran, 0);

	result = dispatch_semaphore_wait(semaphore, dispatch_time(
			DISPATCH_TIME_NOW, -(int64_t)NSEC_PER_MSEC));
	test_long("semaphore wait at a past deadline times out", !result, 0);
	test_long("semaphore wait at a past deadline runs no queued work", ran,
			0);

	dispatch_group_enter(group);
	result = dispatch_group_wait(group,
			dispatch_time(DISPATCH_TIME_NOW, 0));
	test_long("group wait at a deadline of now times out", !result, 0);
	test_long("group wait at a deadline of now runs no queued work", ran,
			0);
	dispatch_group_leave(group);

	// the queued signal is not run by the expired wait
	dispatch_async(queue, ^{
		ran++;
		dispatch_semaphore_signal(semaphore);
	});
	result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 0));
	test_long("expired semaphore wait times out with a signal queued",
			!result, 0);
	test_long("expired semaphore wait does not run the queued signaling "
			"item", ran, 0);

	// a wait with time left runs the three queued items and consumes the
	// signal. 2 s is far longer than the items need.
	result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
	test_long("timed wait with time left is satisfied by the queued signal",
			result, 0);
	test_long("timed wait with time left runs the three queued items", ran,
			3);

	// the expired waits left no stranded count behind
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW);
	test_long("expired waits leave no stranded semaphore count", !result,
			0);

	dispatch_release(group);
	dispatch_release(semaphore);
	dispatch_release(queue);
	test_stop();
	return 0;
}
