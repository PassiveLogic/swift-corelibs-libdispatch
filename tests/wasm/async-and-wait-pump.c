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
 * A blocking wait in a dispatch_async_and_wait body pumps work. Each pumped
 * step must leave the borrowed wlh (the workloop handle of the current
 * drain) unchanged.
 *
 * dispatch_async_and_wait runs its body inline on the calling stack, as
 * dispatch_sync does. _dispatch_fake_wlh lends the body a wlh and takes no
 * reference. The wlh is DISPATCH_WLH_ANON or the workloop itself. No drain
 * step is active, so a wait in the body is a top-level wait, and it pumps.
 * Each pumped step must start with no wlh and give the borrowed wlh back.
 * A step that kept the borrowed wlh would trap with the internal error
 * "Lingering DISPATCH_WLH_ANON", or release the borrowed workloop while the
 * body still runs on it.
 *
 * The cases cover each step kind: a root item, a group item, a timer that
 * fires before its handler signals, and a pending main-queue item. They also
 * cover the plain block path, the barrier entry point, and the private-data
 * path (dispatch_block_create).
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <Block.h>
#include <stdbool.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// the start value of each wait result, so a wait that never ran fails
#define WAIT_NOT_RUN (-2)

static dispatch_queue_t
global_queue(void)
{
	return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

// a root item signals an indefinite semaphore wait in a serial-queue body
static void
test_serial_root_item(void)
{
	dispatch_queue_t queue = dispatch_queue_create(
			"wasi.async_and_wait_pump.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	__block long result = WAIT_NOT_RUN;
	dispatch_async_and_wait(queue, ^{
		dispatch_async(global_queue(), ^{
			dispatch_semaphore_signal(semaphore);
		});
		result = dispatch_semaphore_wait(semaphore,
				DISPATCH_TIME_FOREVER);
	});
	test_long("serial body semaphore wait returns signaled by the pumped "
			"root item", result, 0);
	__block long again = 0;
	dispatch_sync(queue, ^{ again = 1; });
	test_long("serial queue is usable after the body returned", again, 1);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

// a group wait in a concurrent-queue body, through the barrier entry point
static void
test_concurrent_group(void)
{
	dispatch_queue_t queue = dispatch_queue_create(
			"wasi.async_and_wait_pump.concurrent",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_group_t group = dispatch_group_create();
	__block long item_ran = 0;
	__block long result = WAIT_NOT_RUN;
	dispatch_barrier_async_and_wait(queue, ^{
		dispatch_group_async(group, global_queue(), ^{ item_ran = 1; });
		result = dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	});
	test_long("concurrent barrier body group wait returns 0", result, 0);
	test_long("group item runs during the concurrent barrier body group "
			"wait", item_ran, 1);
	dispatch_release(group);
	dispatch_release(queue);
}

// a workloop lends itself as the wlh. The pumped item must not see it. The
// body must see it again after the wait.
static void
test_workloop(void)
{
	dispatch_workloop_t workloop = dispatch_workloop_create(
			"wasi.async_and_wait_pump.workloop");
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	__block long item_saw_workloop = -1;
	__block long body_saw_workloop = -1;
	__block long result = WAIT_NOT_RUN;
	dispatch_async_and_wait(workloop, ^{
		dispatch_async(global_queue(), ^{
			item_saw_workloop =
					dispatch_workloop_is_current(workloop);
			dispatch_semaphore_signal(semaphore);
		});
		// 1 s is far longer than the pumped item needs
		result = dispatch_semaphore_wait(semaphore,
				dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
		body_saw_workloop = dispatch_workloop_is_current(workloop);
	});
	test_long("workloop body timed wait returns signaled within 1 s",
			result, 0);
	test_long("pumped item does not see the borrowed workloop",
			item_saw_workloop, 0);
	test_long("body sees its workloop again after the wait",
			body_saw_workloop, 1);
	test_long("workloop does not leak to the caller",
			dispatch_workloop_is_current(workloop), 0);
	__block long again = 0;
	dispatch_async_and_wait(workloop, ^{ again = 1; });
	test_long("workloop is usable after the body returned", again, 1);
	dispatch_release(semaphore);
	dispatch_release(workloop);
}

// the private-data path (_dispatch_async_and_wait_block_with_privdata)
static void
test_privdata(void)
{
	dispatch_queue_t queue = dispatch_queue_create(
			"wasi.async_and_wait_pump.privdata",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	__block long result = WAIT_NOT_RUN;
	dispatch_block_t body = dispatch_block_create(0, ^{
		dispatch_async(global_queue(), ^{
			dispatch_semaphore_signal(semaphore);
		});
		result = dispatch_semaphore_wait(semaphore,
				DISPATCH_TIME_FOREVER);
	});
	dispatch_async_and_wait(queue, body);
	test_long("private-data body semaphore wait returns signaled by the "
			"pumped root item", result, 0);
	Block_release(body);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

// the timer step fires first, then the root step runs the handler
static void
test_timer(void)
{
	dispatch_queue_t queue = dispatch_queue_create(
			"wasi.async_and_wait_pump.timer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	__block long result = WAIT_NOT_RUN;
	dispatch_async_and_wait(queue, ^{
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC),
				global_queue(), ^{
			dispatch_semaphore_signal(semaphore);
		});
		result = dispatch_semaphore_wait(semaphore,
				DISPATCH_TIME_FOREVER);
	});
	test_long("timer body semaphore wait returns signaled by the 1 ms "
			"timer handler", result, 0);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

// a pending main-queue item while a 10 ms timed wait parks. The wait leaves
// the main-queue item pending and must return without a crash.
static void
test_main_item(void)
{
	dispatch_queue_t queue = dispatch_queue_create(
			"wasi.async_and_wait_pump.main",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	__block long result = WAIT_NOT_RUN;
	dispatch_async_and_wait(queue, ^{
		dispatch_async(dispatch_get_main_queue(), ^{ });
		result = dispatch_semaphore_wait(semaphore, dispatch_time(
				DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC));
	});
	test_long("main-item body 10 ms timed wait times out", !result, 0);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Async And Wait Pump");

	test_serial_root_item();
	test_concurrent_group();
	test_workloop();
	test_privdata();
	test_timer();
	test_main_item();
	test_stop();
	return 0;
}
