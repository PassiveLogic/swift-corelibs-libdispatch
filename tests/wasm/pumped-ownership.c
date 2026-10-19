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
 * A top-level blocking wait pumps pending work on the sole thread. Each
 * pumped root-queue turn runs as a worker: the item does not inherit the
 * queue drain locks, the thread frames, or the main-queue ownership of the
 * code that waits. On threaded platforms the item runs on a worker thread,
 * so every assertion below holds there too.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

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
	dispatch_queue_t sq = dispatch_queue_create("wasi.pumped.serial", NULL);
	dispatch_queue_t cq = dispatch_queue_create("wasi.pumped.concurrent",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_queue_t other = dispatch_queue_create("wasi.pumped.other", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_group_t group = dispatch_group_create();
	__block int ran = 0;

	// semaphore wait inside a serial dispatch_sync body
	dispatch_sync(sq, ^{
		dispatch_async(other, ^{
			check_worker(sq, other);
			ran++;
			dispatch_semaphore_signal(sem);
		});
		dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
		// the waiting frame gets its own identity back after the pump
		dispatch_assert_queue(sq);
	});

	// group wait two sync levels deep. The concurrent queue takes no drain
	// lock, so only a leaked frame chain could make it look current.
	dispatch_sync(cq, ^{
		dispatch_sync(sq, ^{
			dispatch_group_async(group, other, ^{
				check_worker(cq, other);
				dispatch_assert_queue_not(sq);
				ran++;
			});
			dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		});
	});

	// contended dispatch_sync from a sync body pumps the earlier item
	dispatch_sync(sq, ^{
		dispatch_async(other, ^{
			check_worker(sq, other);
			ran++;
		});
		dispatch_sync(other, ^{ });
	});

	// a root-queue item at top level is not on the main queue
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_assert_queue_not(dispatch_get_main_queue());
		ran++;
		dispatch_semaphore_signal(sem);
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

	// top-level code keeps the main-thread identity
	dispatch_assert_queue(dispatch_get_main_queue());
	if (ran != PUMPED_ITEMS) {
		printf("FAIL: ran=%d, expected %d\n", ran, PUMPED_ITEMS);
		return 1;
	}
	printf("pumped ownership OK\n");
	return 0;
}
