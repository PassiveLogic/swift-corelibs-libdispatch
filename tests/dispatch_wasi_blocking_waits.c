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
 * Blocking-wait API contracts on single-threaded WASI:
 *  - dispatch_block_wait on a queued block runs the block and returns success
 *  - dispatch_group_wait(FOREVER) returns 0 once the group empties
 *  - a timed dispatch_semaphore_wait that nothing signals returns non-zero
 *    only after its 250 ms timeout
 *  - dispatch_semaphore_wait(FOREVER) is satisfied by queued work
 */
#include <dispatch/dispatch.h>
#include <Block.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Blocking Waits");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	__block long block_ran = 0;
	dispatch_block_t block = dispatch_block_create(0, ^{ block_ran = 1; });
	dispatch_async(global, block);
	long result = dispatch_block_wait(block,
			dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
	test_long("dispatch_block_wait on a queued block returns success",
			result, 0);
	test_long("dispatch_block_wait runs the queued block", block_ran, 1);

	dispatch_group_t group = dispatch_group_create();
	__block long group_ran = 0;
	dispatch_group_async(group, global, ^{ group_ran = 1; });
	result = dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	test_long("dispatch_group_wait(FOREVER) returns 0 once the group "
			"empties", result, 0);
	test_long("dispatch_group_wait(FOREVER) runs the group work",
			group_ran, 1);

	dispatch_semaphore_t unsignaled = dispatch_semaphore_create(0);
	uint64_t start = dispatch_time(DISPATCH_TIME_NOW, 0);
	result = dispatch_semaphore_wait(unsignaled,
			dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC));
	long elapsed_ms = (long)((dispatch_time(DISPATCH_TIME_NOW, 0) - start) / NSEC_PER_MSEC);
	test_long("timed semaphore wait with no signal times out", !result, 0);
	test_long_greater_than_or_equal("timed semaphore wait lasts at least "
			"its 250 ms timeout", elapsed_ms, 250);

	dispatch_semaphore_t signaled = dispatch_semaphore_create(0);
	dispatch_async(global, ^{ dispatch_semaphore_signal(signaled); });
	result = dispatch_semaphore_wait(signaled, DISPATCH_TIME_FOREVER);
	test_long("dispatch_semaphore_wait(FOREVER) is satisfied by queued "
			"work", result, 0);

	Block_release(block);
	dispatch_release(group);
	dispatch_release(unsignaled);
	dispatch_release(signaled);
	test_stop();
	return 0;
}
