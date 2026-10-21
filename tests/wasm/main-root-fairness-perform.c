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
 * Host-loop variant of main-root-fairness.c. A host turn with a budget of
 * one drain step must not pick the main queue forever while a root item is
 * pending. The command module stands in for the host loop: the registered
 * scheduler records a turn request, and the loop below serves each request
 * with _dispatch_wasi_event_loop_perform(1, true).
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <stdbool.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// The test fails after this many host turns. A fair drain runs the root
// item within a few turns.
#define MAX_TURNS 1000

static bool turn_requested;
static long global_ran;

static void
schedule_host_turn(void *context)
{
	(void)context;
	turn_requested = true;
}

static void
set_global_ran(void *context)
{
	(void)context;
	global_ran = 1;
}

static void
tick(void *context)
{
	(void)context;
	if (!global_ran) {
		dispatch_async_f(dispatch_get_main_queue(), NULL, tick);
	}
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Main Root Fairness Perform");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	dispatch_async_f(global, NULL, set_global_ran);
	dispatch_async_f(dispatch_get_main_queue(), NULL, tick);
	// serve each requested turn, at most MAX_TURNS of them
	long turns = 0;
	while (turn_requested && ++turns <= MAX_TURNS) {
		turn_requested = false;
		(void)_dispatch_wasi_event_loop_perform(1, true);
	}
	test_long_less_than_or_equal("host loop runs the root item within "
			"1000 turns", turns, MAX_TURNS);
	test_long("root item runs before the host loop goes idle", global_ran,
			1);
	test_stop();
	return 0;
}
