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
 * Pins the eager-drain submission semantics as SPECIFIED behavior rather than
 * an accident of the implementation. This is the port's one deliberate
 * divergence from threaded Dispatch and the price of supporting embedded
 * hosts that never call dispatch_main():
 *
 *  1. At top level (outside any drain), a submitted block runs to completion
 *     ON THE SUBMITTING STACK before dispatch_async returns.
 *  2. Blocks submitted from inside a running work item defer to the outer
 *     drain (no nested drains), which resumes in category priority order:
 *     due timers, manager queue, main queue, then root queues - so a
 *     main-queue item submitted after a root-queue item still runs first.
 *  3. Consequence for groups: group_async at top level empties the group
 *     before dispatch_group_notify is even installed, so the notify runs
 *     before any subsequently submitted block - threaded platforms may order
 *     these the other way.
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

	// 1. top-level submission runs before dispatch_async returns
	__block int ran = 0;
	dispatch_async(gq, ^{ ran = 1; });
	if (!ran) {
		printf("FAIL: top-level dispatch_async did not run eagerly\n");
		exit(1);
	}

	// 2. nested submissions defer to the outer drain, which resumes in
	//    category priority order (main queue before root queues), not
	//    submission order
	dispatch_async(gq, ^{
		push('A');
		dispatch_async(gq, ^{ push('G'); });
		dispatch_async(dispatch_get_main_queue(), ^{ push('M'); });
	});
	expect("AMG", "nested submissions drain main-before-root");

	// 3. group empties at submit, so notify precedes later submissions
	dispatch_group_t g = dispatch_group_create();
	dispatch_group_async(g, gq, ^{ push('W'); });
	dispatch_group_notify(g, gq, ^{ push('N'); });
	dispatch_async(gq, ^{ push('P'); });
	expect("WNP", "group notify precedes later submissions");

	printf("eager drain OK\n");
	return 0;
}
