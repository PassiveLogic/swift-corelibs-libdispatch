/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * Main-queue work that reposts itself must not starve the root queues. On
 * threaded platforms the main thread and the worker threads run in
 * parallel. On the sole WASI thread, the drain gives one pending root item
 * the next turn after each main step. Two pumps share that drain step:
 *
 *  1. A top-level timed semaphore wait. A main tick reposts itself until a
 *     global-queue item has signaled the semaphore.
 *  2. dispatch_main(). A main tick reposts itself until a global-queue item
 *     and a serial-queue item have both run.
 *
 * A starved root fails after MAX_TICKS main ticks, not on a wall-clock
 * timeout, so the failure is fast and deterministic.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TICKS 1000

static dispatch_semaphore_t sem;
static int wait_ticks;
static int wait_root_ran;
static int main_ticks;
static int global_ran;
static int serial_ran;

static void
signal_from_root(void *context)
{
	(void)context;
	wait_root_ran = 1;
	dispatch_semaphore_signal(sem);
}

static void
wait_tick(void *context)
{
	(void)context;
	if (wait_root_ran) return;
	if (++wait_ticks > MAX_TICKS) {
		printf("FAIL: top-level wait: root item starved by %d main ticks\n",
				wait_ticks);
		exit(1);
	}
	dispatch_async_f(dispatch_get_main_queue(), NULL, wait_tick);
}

static void
set_global_ran(void *context)
{
	(void)context;
	global_ran = 1;
}

static void
set_serial_ran(void *context)
{
	(void)context;
	serial_ran = 1;
}

static void
main_tick(void *context)
{
	(void)context;
	if (global_ran && serial_ran) {
		printf("wait ticks %d, dispatch_main ticks %d\n", wait_ticks,
				main_ticks);
		printf("main root fairness OK\n");
		exit(0);
	}
	if (++main_ticks > MAX_TICKS) {
		printf("FAIL: dispatch_main: root items starved by %d main ticks "
				"(global_ran=%d serial_ran=%d)\n", main_ticks, global_ran,
				serial_ran);
		exit(1);
	}
	dispatch_async_f(dispatch_get_main_queue(), NULL, main_tick);
}

int
main(void)
{
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	// 1. top-level timed wait, signaled from a root item
	sem = dispatch_semaphore_create(0);
	dispatch_async_f(dispatch_get_main_queue(), NULL, wait_tick);
	dispatch_async_f(global, NULL, signal_from_root);
	if (dispatch_semaphore_wait(sem,
			dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
		printf("FAIL: top-level wait timed out after %d main ticks\n",
				wait_ticks);
		exit(1);
	}

	// 2. dispatch_main(), with a global item and a serial-queue item
	dispatch_queue_t serial = dispatch_queue_create("main-root-fairness",
			NULL);
	dispatch_async_f(global, NULL, set_global_ran);
	dispatch_async_f(serial, NULL, set_serial_ran);
	dispatch_async_f(dispatch_get_main_queue(), NULL, main_tick);
	dispatch_main();
}
