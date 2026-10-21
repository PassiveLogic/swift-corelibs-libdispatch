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
 * Main-queue work that reposts itself must not starve the root queues. On
 * threaded platforms the main thread and the worker threads run in
 * parallel. On the sole WASI thread, dispatch_main() gives one pending root
 * item the next turn after each main step. A main tick reposts itself until
 * a global-queue item and a serial-queue item have both run. A starved root
 * fails after 1000 main ticks, not at a wall-clock timeout.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

#define MAX_TICKS 1000

static dispatch_queue_t serial;
static long main_ticks;
static long global_ran;
static long serial_ran;

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

// reposts itself until both root items have run, at most MAX_TICKS times
static void
main_tick(void *context)
{
	(void)context;
	if (!(global_ran && serial_ran) && ++main_ticks <= MAX_TICKS) {
		dispatch_async_f(dispatch_get_main_queue(), NULL, main_tick);
		return;
	}
	test_long_less_than_or_equal("dispatch_main() runs the root items "
			"within 1000 main ticks", main_ticks, MAX_TICKS);
	test_long("global-queue item runs while the main tick reposts itself",
			global_ran, 1);
	test_long("serial-queue item runs while the main tick reposts itself",
			serial_ran, 1);
	dispatch_release(serial);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Root Fairness");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	// dispatch_main(), with a global item and a serial-queue item
	serial = dispatch_queue_create("wasi.main_root_fairness",
			DISPATCH_QUEUE_SERIAL);
	dispatch_async_f(global, NULL, set_global_ran);
	dispatch_async_f(serial, NULL, set_serial_ran);
	dispatch_async_f(dispatch_get_main_queue(), NULL, main_tick);
	dispatch_main();
}
