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
 * dispatch_async never runs its block before the call returns. Threaded
 * platforms give the same contract. The poke (the wakeup that a submission
 * sends) only records pending work. The work runs at the next pump point. In
 * this test that is a blocking wait. Other pump points are dispatch_main()
 * and a registered host turn.
 *
 * The pump runs pending work in category order: due timers, the manager
 * queue, the main queue, then root queues by QoS. Work in one queue runs in
 * FIFO order. A blocking wait skips the main queue.
 *
 *  1. At top level, a submitted block has not run when dispatch_async
 *     returns. It runs at the next blocking wait.
 *  2. Blocks submitted from inside a running work item run in the outer
 *     drain. A blocking wait never drains the main queue. So a main-queue
 *     item submitted there runs later, in dispatch_main().
 *  3. A group notify installed while the group is still busy runs after the
 *     root-queue items that were already queued, in FIFO order.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The run order, one letter per block. It holds more than the longest
// expected string. push() keeps the last byte for the NUL terminator.
static char order[16];
static size_t order_n;

static void
push(char c)
{
	if (order_n < sizeof(order) - 1) order[order_n++] = c;
}

static void
expect(const char *want, const char *what)
{
	if (strcmp(order, want) != 0) {
		printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, order, want);
		exit(1);
	}
	order_n = 0;
	memset(order, 0, sizeof(order));
}

int
main(void)
{
	dispatch_queue_t gq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);

	// 1. top-level submission does not run before dispatch_async returns,
	//    and runs at the next blocking wait
	__block int ran = 0;
	dispatch_async(gq, ^{ ran = 1; });
	if (ran) {
		printf("FAIL: top-level dispatch_async ran before it returned\n");
		exit(1);
	}
	dispatch_async(gq, ^{ dispatch_semaphore_signal(sem); });
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	if (!ran) {
		printf("FAIL: top-level dispatch_async did not run at the wait\n");
		exit(1);
	}

	// 2. nested submissions run in the outer drain. The main-queue item
	//    does not run at the top-level wait.
	__block int main_ran = 0;
	dispatch_async(gq, ^{
		push('A');
		dispatch_async(gq, ^{ push('G'); });
		dispatch_async(dispatch_get_main_queue(), ^{ main_ran = 1; });
		dispatch_async(gq, ^{ dispatch_semaphore_signal(sem); });
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	expect("AG", "nested submissions drain in the outer wait");
	if (main_ran) {
		printf("FAIL: main-queue item ran inside a top-level wait\n");
		exit(1);
	}

	// 3. the group is still busy when the notify is installed, so the notify
	//    queues behind the root item submitted after it (FIFO)
	dispatch_group_t g = dispatch_group_create();
	dispatch_group_async(g, gq, ^{ push('W'); });
	dispatch_group_notify(g, gq, ^{
		push('N');
		dispatch_semaphore_signal(sem);
	});
	dispatch_async(gq, ^{ push('P'); });
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	expect("WPN", "group notify follows earlier root submissions");

	// the main-queue item from step 2 runs first in dispatch_main()
	dispatch_async(dispatch_get_main_queue(), ^{
		if (!main_ran) {
			printf("FAIL: main-queue item did not run in dispatch_main()\n");
			exit(1);
		}
		printf("deferred submission OK\n");
		exit(0);
	});
	dispatch_main();
}
