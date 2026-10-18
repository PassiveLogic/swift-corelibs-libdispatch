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
 * A client constructor sees an initialized libdispatch. WASI links
 * libdispatch statically, and wasm-ld runs default-priority constructors in
 * link order, client objects first. libdispatch_init therefore runs at
 * priority 101, so a default-priority client constructor sees a bound main
 * queue. This is what a shared libdispatch gives on Linux, and what libSystem
 * gives on Darwin.
 *
 * Expected behavior verified here:
 *  - the constructor runs on the main queue (dispatch_assert_queue passes,
 *    the current label is the main queue label)
 *  - a constructor that posts deferred main-queue work and then waits for
 *    global-queue work (an indefinite semaphore wait and a timed group
 *    wait) gets that work, and does not crash in the main-queue drain
 *  - the deferred main-queue block runs on the main queue
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_ctor_label;
static const char *g_main_block_label;
static int g_main_block_runs;
static int g_global_block_runs;

__attribute__((constructor))
static void
client_constructor(void)
{
	g_ctor_label = dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL);
	dispatch_assert_queue(dispatch_get_main_queue());

	dispatch_async(dispatch_get_main_queue(), ^{
		g_main_block_label =
				dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL);
		g_main_block_runs++;
	});

	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,
			0), ^{
		g_global_block_runs++;
		dispatch_semaphore_signal(sem);
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	dispatch_release(sem);

	dispatch_group_t group = dispatch_group_create();
	dispatch_group_async(group, dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		g_global_block_runs++;
	});
	if (dispatch_group_wait(group,
			dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC))) {
		printf("FAIL: timed group wait in a constructor timed out\n");
		exit(1);
	}
	dispatch_release(group);
}

static void
check(void *ctxt)
{
	(void)ctxt;
	if (!g_ctor_label || strcmp(g_ctor_label, "com.apple.main-thread")) {
		printf("FAIL: constructor queue label %s\n",
				g_ctor_label ? g_ctor_label : "(null)");
		exit(1);
	}
	if (g_global_block_runs != 2) {
		printf("FAIL: global blocks ran %d times\n", g_global_block_runs);
		exit(1);
	}
	if (g_main_block_runs != 1 || !g_main_block_label ||
			strcmp(g_main_block_label, "com.apple.main-thread")) {
		printf("FAIL: main block ran %d times on %s\n", g_main_block_runs,
				g_main_block_label ? g_main_block_label : "(null)");
		exit(1);
	}
	printf("constructor main queue OK\n");
	exit(0);
}

int
main(void)
{
	// Blocking waits never drain the main queue. The constructor's deferred
	// main-queue block runs in dispatch_main(), before this check, which was
	// queued after it (FIFO).
	dispatch_async_f(dispatch_get_main_queue(), NULL, check);
	dispatch_main();
}
