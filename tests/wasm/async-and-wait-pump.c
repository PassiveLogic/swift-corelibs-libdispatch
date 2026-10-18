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
 * dispatch_async_and_wait runs its body inline on the calling stack, the
 * same as dispatch_sync, but under a borrowed wlh: _dispatch_fake_wlh sets
 * DISPATCH_WLH_ANON, or the workloop itself, and takes no reference. No
 * drain step is active, so a blocking wait in that body is a top-level wait
 * and pumps, the same as a wait in a dispatch_sync body. Each pumped step
 * must start with no wlh and must give the borrowed wlh back unchanged.
 * Before the fix, the first pumped root or main step crashed with the
 * internal "Lingering DISPATCH_WLH_ANON", or released the borrowed workloop
 * and freed it while the body still ran on it.
 *
 * Every step kind is covered: a root item, a group item, a timer that fires
 * before its handler signals, and a pending main-queue item. The plain
 * block funnel, the barrier entry point, and the private-data funnel
 * (dispatch_block_create) are covered.
 */
#include <dispatch/dispatch.h>
#include <Block.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// declared in private/workloop_private.h in this tree
extern void dispatch_async_and_wait(dispatch_queue_t queue,
		dispatch_block_t block);
extern void dispatch_barrier_async_and_wait(dispatch_queue_t queue,
		dispatch_block_t block);
extern dispatch_queue_t dispatch_workloop_create(const char *label);
extern bool dispatch_workloop_is_current(dispatch_queue_t workloop);

#ifndef ONLY_CASE
#define ONLY_CASE 0
#endif

static dispatch_queue_t
global_queue(void)
{
	return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

static void
fail(const char *what)
{
	printf("FAIL: %s\n", what);
	exit(1);
}

// a root item signals an indefinite semaphore wait in a serial-queue body
static void
case_serial_root_item(void)
{
	dispatch_queue_t q = dispatch_queue_create("aawp.serial", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	__block long r = -2;
	dispatch_async_and_wait(q, ^{
		dispatch_async(global_queue(), ^{ dispatch_semaphore_signal(sem); });
		r = dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	});
	if (r != 0) fail("serial body wait did not return signaled");
	__block int again = 0;
	dispatch_sync(q, ^{ again = 1; });
	if (!again) fail("serial queue unusable after the body returned");
	dispatch_release(sem);
	dispatch_release(q);
}

// a group wait in a concurrent-queue body, through the barrier entry point
static void
case_concurrent_group(void)
{
	dispatch_queue_t q = dispatch_queue_create("aawp.concurrent",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_group_t g = dispatch_group_create();
	__block int item_ran = 0;
	__block long r = -2;
	dispatch_barrier_async_and_wait(q, ^{
		dispatch_group_async(g, global_queue(), ^{ item_ran = 1; });
		r = dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
	});
	if (r != 0 || !item_ran) fail("concurrent body group wait");
	dispatch_release(g);
	dispatch_release(q);
}

// a workloop lends itself as the wlh; the pumped item must not see it, and
// the body must see it again after the wait
static void
case_workloop(void)
{
	dispatch_queue_t wl = dispatch_workloop_create("aawp.workloop");
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	__block int item_saw_wl = -1;
	__block int body_saw_wl = -1;
	__block long r = -2;
	dispatch_async_and_wait(wl, ^{
		dispatch_async(global_queue(), ^{
			item_saw_wl = dispatch_workloop_is_current(wl);
			dispatch_semaphore_signal(sem);
		});
		r = dispatch_semaphore_wait(sem,
				dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
		body_saw_wl = dispatch_workloop_is_current(wl);
	});
	if (r != 0) fail("workloop body wait timed out");
	if (item_saw_wl != 0) fail("pumped item saw the borrowed workloop");
	if (body_saw_wl != 1) fail("body lost its workloop after the wait");
	if (dispatch_workloop_is_current(wl)) fail("workloop leaked to caller");
	__block int again = 0;
	dispatch_async_and_wait(wl, ^{ again = 1; });
	if (!again) fail("workloop unusable after the body returned");
	dispatch_release(sem);
	dispatch_release(wl);
}

// the private-data funnel (_dispatch_async_and_wait_block_with_privdata)
static void
case_privdata(void)
{
	dispatch_queue_t q = dispatch_queue_create("aawp.privdata", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	__block long r = -2;
	dispatch_block_t body = dispatch_block_create(0, ^{
		dispatch_async(global_queue(), ^{ dispatch_semaphore_signal(sem); });
		r = dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	});
	dispatch_async_and_wait(q, body);
	if (r != 0) fail("privdata body wait did not return signaled");
	Block_release(body);
	dispatch_release(sem);
	dispatch_release(q);
}

// the timer step fires first, then the root step runs the handler
static void
case_timer(void)
{
	dispatch_queue_t q = dispatch_queue_create("aawp.timer", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	__block long r = -2;
	dispatch_async_and_wait(q, ^{
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC),
				global_queue(), ^{ dispatch_semaphore_signal(sem); });
		r = dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	});
	if (r != 0) fail("timer body wait did not return signaled");
	dispatch_release(sem);
	dispatch_release(q);
}

// a pending main-queue item while a timed wait parks; the wait may or may
// not run it, but it must return without a crash
static void
case_main_item(void)
{
	dispatch_queue_t q = dispatch_queue_create("aawp.main", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	__block long r = -2;
	dispatch_async_and_wait(q, ^{
		dispatch_async(dispatch_get_main_queue(), ^{ });
		r = dispatch_semaphore_wait(sem,
				dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC));
	});
	if (r == 0) fail("main-item body wait returned signaled");
	dispatch_release(sem);
	dispatch_release(q);
}

int
main(void)
{
	if (ONLY_CASE == 0 || ONLY_CASE == 1) case_serial_root_item();
	if (ONLY_CASE == 0 || ONLY_CASE == 2) case_concurrent_group();
	if (ONLY_CASE == 0 || ONLY_CASE == 3) case_workloop();
	if (ONLY_CASE == 0 || ONLY_CASE == 4) case_privdata();
	if (ONLY_CASE == 0 || ONLY_CASE == 5) case_timer();
	if (ONLY_CASE == 0 || ONLY_CASE == 6) case_main_item();
	printf("async and wait pump OK\n");
	return 0;
}
