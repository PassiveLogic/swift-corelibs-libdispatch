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
 * Host-loop variant of main-root-fairness.c. A host turn with a budget of
 * one drain step must not pick the main queue forever while a root item is
 * pending. The command module stands in for the host loop: the registered
 * scheduler records a turn request, and the loop below serves each request
 * with _dispatch_wasi_event_loop_perform(1, true).
 */
#include <dispatch/dispatch.h>
#define __DISPATCH_BUILDING_DISPATCH__ 1
#include <private/private.h>
#undef __DISPATCH_BUILDING_DISPATCH__
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TURNS 1000

static bool turn_requested;
static int ticks;
static int global_ran;

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
	ticks++;
	if (!global_ran) {
		dispatch_async_f(dispatch_get_main_queue(), NULL, tick);
	}
}

int
main(void)
{
	_dispatch_wasi_event_loop_set_scheduler(schedule_host_turn, NULL);
	dispatch_async_f(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), NULL, set_global_ran);
	dispatch_async_f(dispatch_get_main_queue(), NULL, tick);
	int turns = 0;
	while (turn_requested) {
		if (++turns > MAX_TURNS) {
			printf("FAIL: root item starved: %d host turns, %d main ticks\n",
					turns - 1, ticks);
			return 1;
		}
		turn_requested = false;
		(void)_dispatch_wasi_event_loop_perform(1, true);
	}
	if (!global_ran) {
		printf("FAIL: host loop went idle before the root item ran\n");
		return 1;
	}
	printf("host turns %d, main ticks %d\n", turns, ticks);
	printf("main root fairness perform OK\n");
	return 0;
}
