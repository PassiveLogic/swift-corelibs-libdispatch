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
 * allows, so on the cooperative port pokes from inside the body must defer
 * exactly as they do for dispatch_sync. Both entry shapes are covered:
 *
 *  - a plain block funnels through the bracketed _dispatch_async_and_wait_f
 *  - a dispatch_block_create() (private-data) block takes a separate funnel,
 *    _dispatch_async_and_wait_block_with_privdata; an unbracketed recurse
 *    there lets a mere dispatch_async inside the body eager-drain within the
 *    _dispatch_fake_wlh ANON region and die with the internal-bug crash
 *    "Lingering DISPATCH_WLH_ANON". Swift reaches this path via
 *    DispatchQueue.asyncAndWait(execute: DispatchWorkItem).
 *
 * The deferred pokes flush before async_and_wait returns (same contract the
 * sync bracket has), so completion is asserted directly after each return.
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

	// control: the plain-block funnel is bracketed; the nested sync-back
	// defers past the inline invoke and flushes before this call returns
	dispatch_async_and_wait(qa, ^{
		dispatch_async(qb, ^{
			dispatch_sync(qa, ^{ plain_done = 1; });
		});
	});
	if (!plain_done) {
		printf("FAIL: plain-block nested sync-back never ran\n");
		exit(1);
	}

	// privdata, benign body: a lone dispatch_async inside the block must
	// not eager-drain inside the fake-ANON wlh region
	dispatch_block_t benign = dispatch_block_create(0, ^{
		dispatch_async(qb, ^{ benign_done = 1; });
	});
	dispatch_async_and_wait(qa, benign);
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
	if (!nested_done) {
		printf("FAIL: privdata nested sync-back never ran\n");
		exit(1);
	}

	// privdata through the barrier entry point (same funnel)
	dispatch_block_t barrier = dispatch_block_create(0, ^{
		dispatch_async(qb, ^{ barrier_done = 1; });
	});
	dispatch_barrier_async_and_wait(qa, barrier);
	if (!barrier_done) {
		printf("FAIL: privdata barrier async never ran\n");
		exit(1);
	}

	printf("async and wait OK\n");
	return 0;
}
