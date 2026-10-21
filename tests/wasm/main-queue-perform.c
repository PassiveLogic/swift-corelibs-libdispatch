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
 * A main-queue drain that empties the queue leaves no pending main work.
 * _dispatch_wasi_event_loop_perform() returns true only when more work
 * remains, and a main-queue batch requests exactly one host turn. The
 * main queue drain ends with a wakeup that adds no item. That wakeup must
 * not mark the main queue as pending.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdbool.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static long turns;
static long ran;

static void
count_turn(void *context)
{
	(void)context;
	turns++;
}

static void
run_once(void *context)
{
	(void)context;
	ran++;
}

static void
run_and_requeue_once(void *context)
{
	(void)context;
	if (ran++ == 0) {
		dispatch_async_f(dispatch_get_main_queue(), NULL,
				run_and_requeue_once);
	}
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Queue Perform");

	// Run the turn that registration requested, if any, so each case starts
	// with no pending work. 64 steps is more than the startup work needs.
	_dispatch_wasi_event_loop_set_scheduler(count_turn, NULL);
	while (turns && _dispatch_wasi_event_loop_perform(64, true)) {
	}

	// One main block, budget of one step: the push requests one turn, the
	// step runs the block, and no work remains.
	turns = ran = 0;
	dispatch_async_f(dispatch_get_main_queue(), NULL, run_once);
	bool more = _dispatch_wasi_event_loop_perform(1, true);
	test_long("one main block: perform with a one-step budget reports no "
			"more work", more, 0);
	test_long("one main block: the push requests one host turn", turns, 1);
	test_long("one main block: the step runs the block", ran, 1);

	// A main block that adds one more main block. The first perform has
	// real work left and requests one more turn. The second perform runs
	// the added block and reports no more work.
	turns = ran = 0;
	dispatch_async_f(dispatch_get_main_queue(), NULL, run_and_requeue_once);
	more = _dispatch_wasi_event_loop_perform(1, true);
	test_long("requeue: the first perform reports more work", more, 1);
	test_long("requeue: the added block requests a second host turn",
			turns, 2);
	test_long("requeue: the first perform runs the first block", ran, 1);
	more = _dispatch_wasi_event_loop_perform(1, true);
	test_long("requeue: the second perform reports no more work", more, 0);
	test_long("requeue: the second perform requests no further host turn",
			turns, 2);
	test_long("requeue: the second perform runs the added block", ran, 2);

	test_stop();
	return 0;
}
