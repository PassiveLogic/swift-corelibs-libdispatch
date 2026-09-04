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
 * dispatch_async_and_wait runs its workitem inline on the calling thread
 * (with the queue's width/barrier acquired) whenever the queue hierarchy
 * allows. Work submitted from inside the body must not run on that stack:
 * it runs at the next pump point, after the wait has released the queue.
 * Both entry shapes are covered:
 *
 *  - a plain block funnels through _dispatch_async_and_wait_f
 *  - a dispatch_block_create() (private-data) block takes a separate funnel,
 *    _dispatch_async_and_wait_block_with_privdata, whose recurse runs inside
 *    the _dispatch_fake_wlh ANON region; a block that ran there would die
 *    with the internal-bug crash "Lingering DISPATCH_WLH_ANON". Swift
 *    reaches this path via DispatchQueue.asyncAndWait(execute:).
 *
 * After each return a contended dispatch_sync on qb pumps the queued block.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

// declared in private/workloop_private.h in this tree
extern void dispatch_async_and_wait(dispatch_queue_t queue,
		dispatch_block_t block);
extern void dispatch_barrier_async_and_wait(dispatch_queue_t queue,
		dispatch_block_t block);

int
main(void)
{
	dispatch_queue_t qa = dispatch_queue_create("v2.aaw.outer", NULL);
	dispatch_queue_t qb = dispatch_queue_create("v2.aaw.other", NULL);
	__block int plain_done = 0;
	__block int benign_done = 0;
	__block int nested_done = 0;
	__block int barrier_done = 0;

	// control: the plain-block funnel; the nested sync-back runs at the
	// pump, after the inline invoke released qa
	dispatch_async_and_wait(qa, ^{
		dispatch_async(qb, ^{
			dispatch_sync(qa, ^{ plain_done = 1; });
		});
	});
	if (plain_done) {
		printf("FAIL: plain-block nested async ran inside async_and_wait\n");
		exit(1);
	}
	dispatch_sync(qb, ^{ });
	if (!plain_done) {
		printf("FAIL: plain-block nested sync-back never ran\n");
		exit(1);
	}

	// privdata, benign body: a lone dispatch_async inside the block must
	// not run inside the fake-ANON wlh region
	dispatch_block_t benign = dispatch_block_create(0, ^{
		dispatch_async(qb, ^{ benign_done = 1; });
	});
	dispatch_async_and_wait(qa, benign);
	dispatch_sync(qb, ^{ });
	if (!benign_done) {
		printf("FAIL: privdata benign async never ran\n");
		exit(1);
	}

	// privdata, full nesting: same shape as the plain-block control
	dispatch_block_t nested = dispatch_block_create(0, ^{
		dispatch_async(qb, ^{
			dispatch_sync(qa, ^{ nested_done = 1; });
		});
	});
	dispatch_async_and_wait(qa, nested);
	dispatch_sync(qb, ^{ });
	if (!nested_done) {
		printf("FAIL: privdata nested sync-back never ran\n");
		exit(1);
	}

	// privdata through the barrier entry point (same funnel)
	dispatch_block_t barrier = dispatch_block_create(0, ^{
		dispatch_async(qb, ^{ barrier_done = 1; });
	});
	dispatch_barrier_async_and_wait(qa, barrier);
	dispatch_sync(qb, ^{ });
	if (!barrier_done) {
		printf("FAIL: privdata barrier async never ran\n");
		exit(1);
	}

	printf("async and wait OK\n");
	return 0;
}
