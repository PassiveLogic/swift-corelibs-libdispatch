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
 * Pins the submission semantics of the cooperative port. dispatch_async
 * never runs its block before the call returns: a poke only records pending
 * work, and the work runs at the next pump point (a blocking wait or
 * dispatch_main()). Threaded platforms do not promise this: a worker thread
 * can run the block before dispatch_async returns. A blocking wait pumps
 * root-queue work in FIFO order within one queue, and never the main queue:
 * as on Darwin and Linux, main-queue work waits for dispatch_main().
 *
 *  1. At top level, a submitted block has not run when dispatch_async
 *     returns. It runs at the next blocking wait.
 *  2. Root items submitted from inside a running work item run in the outer
 *     drain. A main-queue item submitted there stays pending across the
 *     wait and runs in dispatch_main().
 *  3. A group notify installed while the group is still busy runs after the
 *     root-queue items that were already queued, in FIFO order.
 *  4. The pending main-queue item runs first in dispatch_main().
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// each item records its submission number (1, 2, 3) in the order it runs
static long order[3];
static long order_count;
static long main_ran;

static void
push(long step)
{
	if (order_count < (long)(sizeof(order) / sizeof(order[0]))) {
		order[order_count] = step;
	}
	order_count++;
}

static void
reset_order(void)
{
	order_count = 0;
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Deferred Submission");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

	// 1. top-level submission does not run before dispatch_async returns,
	//    and runs at the next blocking wait
	__block long ran = 0;
	dispatch_async(global, ^{ ran = 1; });
	test_long("top-level dispatch_async does not run the block before it "
			"returns", ran, 0);
	dispatch_async(global, ^{ dispatch_semaphore_signal(semaphore); });
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	test_long("top-level dispatch_async runs the block at the next "
			"blocking wait", ran, 1);

	// 2. nested root submissions run in the outer drain; a nested
	//    main-queue submission stays pending, because a wait never drains
	//    the main queue
	dispatch_async(global, ^{
		push(1);
		dispatch_async(dispatch_get_main_queue(), ^{ main_ran = 1; });
		dispatch_async(global, ^{ push(2); });
		dispatch_async(global, ^{
			dispatch_semaphore_signal(semaphore);
		});
	});
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	test_long("outer item and the nested root item run before the wait "
			"returns", order_count, 2);
	test_long("outer item runs first", order[0], 1);
	test_long("nested root item runs second", order[1], 2);
	test_long("nested main-queue item does not run during the wait",
			main_ran, 0);
	reset_order();

	// 3. the group is still busy when the notify is installed, so the
	//    notify queues behind the root item submitted after it (FIFO)
	dispatch_group_t group = dispatch_group_create();
	dispatch_group_async(group, global, ^{ push(1); });
	dispatch_group_notify(group, global, ^{
		push(2);
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_async(global, ^{ push(3); });
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	test_long("group item, later root item and notify run before the wait "
			"returns", order_count, 3);
	test_long("group item runs first", order[0], 1);
	test_long("root item submitted after the notify was installed runs "
			"before it", order[1], 3);
	test_long("group notify runs last, after earlier root submissions",
			order[2], 2);

	dispatch_release(group);
	dispatch_release(semaphore);

	// 4. the main-queue item from step 2 is still pending; dispatch_main()
	//    runs it before anything queued on the main queue afterwards
	test_long("nested main-queue item stays pending across later waits",
			main_ran, 0);
	dispatch_async(dispatch_get_main_queue(), ^{
		test_long("dispatch_main() runs the pending main-queue item "
				"first", main_ran, 1);
		test_stop();
	});
	dispatch_main();
}
