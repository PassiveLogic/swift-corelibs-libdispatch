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
 * libdispatch statically. wasm-ld runs default-priority constructors in link
 * order, and client objects come first. So _libdispatch_init runs at priority
 * 101, and a default-priority client constructor sees a bound main queue.
 * A shared libdispatch on Linux and libSystem on Darwin give the same result.
 *
 * The test checks that:
 *  - the constructor runs on the main queue (dispatch_assert_queue passes,
 *    and the current label is the main queue label)
 *  - a constructor can post deferred main-queue work, then wait for
 *    global-queue work (an indefinite semaphore wait and a timed group
 *    wait). The constructor gets that work, and the main-queue drain does
 *    not crash.
 *  - the deferred main-queue block runs on the main queue
 *
 * The constructor runs before main, so it records what it sees in static
 * variables and a main-queue block asserts them under dispatch_main().
 */
#include <dispatch/dispatch.h>
#include <string.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// the label of the main queue
static const char main_label[] = "com.apple.main-thread";

// the constructor posts this many global-queue blocks and one main block
#define GLOBAL_BLOCKS 2

static const char *ctor_label;
static const char *main_block_label;
static long main_block_runs;
static long global_block_runs;
static long ctor_group_wait_result;

__attribute__((constructor))
static void
client_constructor(void)
{
	ctor_label = dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL);
	dispatch_assert_queue(dispatch_get_main_queue());

	dispatch_async(dispatch_get_main_queue(), ^{
		main_block_label = dispatch_queue_get_label(
				DISPATCH_CURRENT_QUEUE_LABEL);
		main_block_runs++;
	});

	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		global_block_runs++;
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	dispatch_release(semaphore);

	dispatch_group_t group = dispatch_group_create();
	dispatch_group_async(group, dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		global_block_runs++;
	});
	// 1 s is far longer than the work needs
	ctor_group_wait_result = dispatch_group_wait(group,
			dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
	dispatch_release(group);
}

static void
check(void *context)
{
	(void)context;
	test_ptr_notnull("constructor reads its current queue label",
			ctor_label);
	test_long("constructor runs with the main queue label",
			strcmp(ctor_label ? ctor_label : "", main_label), 0);
	test_long("timed group wait in the constructor returns 0 within 1 s",
			ctor_group_wait_result, 0);
	test_long("constructor waits run both global-queue blocks",
			global_block_runs, GLOBAL_BLOCKS);
	test_long("deferred main-queue block of the constructor runs once",
			main_block_runs, 1);
	test_ptr_notnull("deferred main-queue block reads its current queue "
			"label", main_block_label);
	test_long("deferred main-queue block runs with the main queue label",
			strcmp(main_block_label ? main_block_label : "",
			main_label), 0);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Constructor Main Queue");

	// Blocking waits never drain the main queue. So the main-queue block of
	// the constructor runs first in dispatch_main(). This check runs after
	// it, because the main queue is FIFO.
	dispatch_async_f(dispatch_get_main_queue(), NULL, check);
	dispatch_main();
}
