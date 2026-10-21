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
 * A serial queue that targets the main queue must not starve root work and
 * timers. The queue re-posts a tick until other work sets a flag. This
 * polling pattern works on threaded platforms. On the sole WASI thread, a
 * main-queue step must run one item of such a queue, then move the queue to
 * the back of the main queue. So root work and timers still get a turn.
 *  1. A global-queue block sets the flag.
 *  2. A 10 ms dispatch_after sets the flag.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// The test fails after this many ticks. A fair drain sets the flag after a
// few ticks. The limit only stops a starved run in a few seconds.
#define TICK_LIMIT 2000000

static dispatch_queue_t lane;
static long done;
static long ticks;
static int phase;

static void start_phase2(void);

// reposts itself until the flag is set, at most TICK_LIMIT times
static void
tick(void *context)
{
	(void)context;
	if (!done && ++ticks < TICK_LIMIT) {
		dispatch_async_f(lane, NULL, tick);
		return;
	}
	if (phase == 1) {
		test_long("global-queue block runs within 2000000 lane ticks",
				done, 1);
		if (done) {
			start_phase2();
			return;
		}
	} else {
		test_long("10 ms dispatch_after block runs within 2000000 lane "
				"ticks", done, 1);
	}
	// the system keeps the lane alive until this item returns
	dispatch_release(lane);
	test_stop();
}

static void
start_phase2(void)
{
	phase = 2;
	done = 0;
	ticks = 0;
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC),
			global, ^{ done = 1; });
	dispatch_async_f(lane, NULL, tick);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Lane Fairness");

	lane = dispatch_queue_create_with_target("wasi.main.lane",
			DISPATCH_QUEUE_SERIAL, dispatch_get_main_queue());
	phase = 1;
	dispatch_async_f(lane, NULL, tick);
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ done = 1; });
	dispatch_main();
}
