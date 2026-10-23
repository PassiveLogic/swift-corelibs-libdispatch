/*
 * This source file is part of the Swift.org open source project
 *
 * Copyright (c) 2026 Apple Inc. and the Swift project authors
 *
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See https://swift.org/LICENSE.txt for license information
 * See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
 *
 */
/*
 * A client constructor sees an initialized libdispatch. WASI links
 * libdispatch statically. wasm-ld runs default-priority constructors in link
 * order, and client objects come first. So _libdispatch_init runs at priority
 * 101, and a default-priority client constructor sees a bound main queue.
 * A shared libdispatch on Linux and libSystem on Darwin give the same result.
 *
 * The constructor runs on the main queue (dispatch_assert_queue passes, and
 * the current label is the main queue label), and the blocks it queues on
 * the main queue and on a global queue run under dispatch_main(), the main
 * one with the main queue label.
 */
#include <dispatch/dispatch.h>
#include <string.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static const char *const main_label = "com.apple.main-thread";
static const char *ctor_label;
static const char *main_block_label;
static long main_block_runs;
static long global_block_runs;

static void check(void *context);

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
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		global_block_runs++;
		// The main queue ran its block in an earlier step, so this check
		// runs after both blocks.
		dispatch_async_f(dispatch_get_main_queue(), NULL, check);
	});
}

static void
check(void *context)
{
	(void)context;
	test_ptr_notnull("constructor reads its current queue label",
			ctor_label);
	test_long("constructor runs with the main queue label",
			strcmp(ctor_label ? ctor_label : "", main_label), 0);
	test_long("main-queue block of the constructor runs once",
			main_block_runs, 1);
	test_ptr_notnull("main-queue block of the constructor reads its "
			"current queue label", main_block_label);
	test_long("main-queue block of the constructor runs with the main "
			"queue label", strcmp(main_block_label ? main_block_label : "",
			main_label), 0);
	test_long("global-queue block of the constructor runs once",
			global_block_runs, 1);
	test_stop();
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Constructor Main Queue");
	dispatch_main();
}
