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
 * A serial queue that targets the main queue re-posts a tick until another
 * source of work sets a flag. This polling pattern works on threaded
 * platforms. On the sole WASI thread, a main-queue step must run one item of
 * such a queue and send the queue to the back of the main queue, so root
 * work and timers still get a turn.
 *  1. A global-queue block sets the flag.
 *  2. A 10 ms dispatch_after sets the flag.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

#define TICK_LIMIT 2000000

static dispatch_queue_t lane;
static volatile int done;
static long ticks;
static int phase;

static void start_phase2(void);

static void
tick(void *ctxt)
{
	(void)ctxt;
	if (done) {
		if (phase == 1) {
			start_phase2();
			return;
		}
		printf("main lane fairness OK\n");
		exit(0);
	}
	if (++ticks == TICK_LIMIT) {
		printf("FAIL: phase %d flag never set after %ld lane ticks\n",
				phase, ticks);
		exit(1);
	}
	dispatch_async_f(lane, NULL, tick);
}

static void
start_phase2(void)
{
	phase = 2;
	done = 0;
	ticks = 0;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC),
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
			^{ done = 1; });
	dispatch_async_f(lane, NULL, tick);
}

int
main(void)
{
	lane = dispatch_queue_create_with_target("wasi.main.lane", NULL,
			dispatch_get_main_queue());
	phase = 1;
	dispatch_async_f(lane, NULL, tick);
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,
			0), ^{ done = 1; });
	dispatch_main();
}
