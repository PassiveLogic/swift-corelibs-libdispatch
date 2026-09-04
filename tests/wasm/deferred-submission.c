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
 * Pins the submission semantics of the cooperative port. dispatch_async
 * never runs its block before the call returns: a poke only records pending
 * work, and the work runs at the next pump point (a blocking wait here;
 * dispatch_main() or a registered host turn elsewhere). This is the contract
 * threaded platforms give too. The pump runs pending work in category
 * priority order (due timers, the manager queue, the main queue, then root
 * queues by QoS) and in FIFO order within one queue:
 *
 *  1. At top level, a submitted block has not run when dispatch_async
 *     returns. It runs at the next blocking wait.
 *  2. Blocks submitted from inside a running work item run in the outer
 *     drain in category priority order, so a main-queue item submitted after
 *     a root-queue item still runs first.
 *  3. A group notify installed while the group is still busy runs after the
 *     root-queue items that were already queued, in FIFO order.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

	// 2. nested submissions run in the outer drain in category priority
	//    order (main queue before root queues), not submission order
	dispatch_async(gq, ^{
		push('A');
		dispatch_async(gq, ^{ push('G'); });
		dispatch_async(dispatch_get_main_queue(), ^{ push('M'); });
		dispatch_async(gq, ^{ dispatch_semaphore_signal(sem); });
	});
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	expect("AMG", "nested submissions drain main-before-root");

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

	printf("deferred submission OK\n");
	return 0;
}
