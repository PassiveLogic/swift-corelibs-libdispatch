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
#include "wasi-test.h"

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
	uint64_t start = wasi_test_now_ns();
	result = dispatch_semaphore_wait(unsignaled,
			dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
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
