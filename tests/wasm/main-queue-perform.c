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
#define __DISPATCH_BUILDING_DISPATCH__ 1
#include <private/private.h>
#undef __DISPATCH_BUILDING_DISPATCH__
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static int turns;
static int ran;

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

static void
expect(const char *step, bool more, bool want_more, int want_turns,
		int want_ran)
{
	printf("%s: more=%d turns=%d ran=%d\n", step, more, turns, ran);
	if (more != want_more || turns != want_turns || ran != want_ran) {
		printf("FAIL: %s: want more=%d turns=%d ran=%d\n", step,
				want_more, want_turns, want_ran);
		exit(1);
	}
}

int
main(void)
{
	// Run the turn that registration requested, if any, so each case starts
	// with no pending work. 64 steps is more than the startup work needs.
	_dispatch_wasi_event_loop_set_scheduler(count_turn, NULL);
	while (turns && _dispatch_wasi_event_loop_perform(64, true)) {
	}

	// One main block, budget of one step: the push requests one turn, the
	// step runs the block, and no work remains.
	turns = ran = 0;
	dispatch_async_f(dispatch_get_main_queue(), NULL, run_once);
	expect("one block", _dispatch_wasi_event_loop_perform(1, true),
			false, 1, 1);

	// A main block that adds one more main block. The first perform has
	// real work left and requests one more turn. The second perform runs
	// the added block and reports no more work.
	turns = ran = 0;
	dispatch_async_f(dispatch_get_main_queue(), NULL, run_and_requeue_once);
	expect("requeue first", _dispatch_wasi_event_loop_perform(1, true),
			true, 2, 1);
	expect("requeue second", _dispatch_wasi_event_loop_perform(1, true),
			false, 2, 2);

	printf("main queue perform OK\n");
	return 0;
}
