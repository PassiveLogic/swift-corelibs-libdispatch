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
 * A top-level blocking wait pumps pending work on the sole thread. Each
 * pumped root-queue turn runs as a worker: the item does not inherit the
 * queue drain locks, the thread frames, or the main-queue ownership of the
 * code that waits. On threaded platforms the item runs on a worker thread,
 * so every assertion below holds there too. The queue assertions are
 * dispatch_assert_queue calls, so a wrong identity crashes the test.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

// the number of pumped items that the four cases run
#define PUMPED_ITEMS 4

static void
check_worker(dispatch_queue_t held, dispatch_queue_t own)
{
	dispatch_assert_queue_not(held);
	dispatch_assert_queue_not(dispatch_get_main_queue());
	dispatch_assert_queue(own);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Pumped Ownership");

	dispatch_queue_t serial = dispatch_queue_create("wasi.pumped.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t concurrent = dispatch_queue_create(
			"wasi.pumped.concurrent", DISPATCH_QUEUE_CONCURRENT);
	dispatch_queue_t other = dispatch_queue_create("wasi.pumped.other",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_group_t group = dispatch_group_create();
	__block long ran = 0;

	// semaphore wait inside a serial dispatch_sync body
	dispatch_sync(serial, ^{
		dispatch_async(other, ^{
			check_worker(serial, other);
			ran++;
			dispatch_semaphore_signal(semaphore);
		});
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
		// the waiting frame gets its own identity back after the pump
		dispatch_assert_queue(serial);
	});

	// group wait two sync levels deep. The concurrent queue takes no drain
	// lock, so only a leaked frame chain could make it look current.
	dispatch_sync(concurrent, ^{
		dispatch_sync(serial, ^{
			dispatch_group_async(group, other, ^{
				check_worker(concurrent, other);
				dispatch_assert_queue_not(serial);
				ran++;
			});
			dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		});
	});

	// contended dispatch_sync from a sync body pumps the earlier item
	dispatch_sync(serial, ^{
		dispatch_async(other, ^{
			check_worker(serial, other);
			ran++;
		});
		dispatch_sync(other, ^{ });
	});

	// a root-queue item at top level is not on the main queue
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_assert_queue_not(dispatch_get_main_queue());
		ran++;
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

	// top-level code keeps the main-thread identity
	dispatch_assert_queue(dispatch_get_main_queue());
	test_long("the four cases pump 4 root-queue items", ran,
			PUMPED_ITEMS);

	dispatch_release(group);
	dispatch_release(semaphore);
	dispatch_release(other);
	dispatch_release(concurrent);
	dispatch_release(serial);
	test_stop();
	return 0;
}
