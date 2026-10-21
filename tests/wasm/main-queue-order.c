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
 * The thread-bound main queue drains in strict serial FIFO order,
 * interleaved with global-queue work, plus a main-queue timer that runs
 * after all main-queue items and stops the test.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

#define MAIN_STEP_COUNT 5

static long step, global_ran;

static void
main_step(void *context)
{
	long n = (long)(intptr_t)context;
	test_long("main queue runs items in FIFO order", n, step);
	step++;
}

static void
global_work(void *context)
{
	(void)context;
	global_ran = 1;
}

static void
finish(void *context)
{
	(void)context;
	test_long("all main queue items run before the main queue timer",
			step, MAIN_STEP_COUNT);
	test_long("global-queue item runs", global_ran, 1);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Queue Order");

	dispatch_queue_t main_queue = dispatch_get_main_queue();
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	for (intptr_t i = 0; i < MAIN_STEP_COUNT; i++) {
		dispatch_async_f(main_queue, (void *)i, main_step);
	}
	dispatch_async_f(global, NULL, global_work);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC),
			main_queue, NULL, finish);
	dispatch_main();
}
