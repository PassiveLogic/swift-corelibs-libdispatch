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
 * After each return a contended dispatch_sync on the other queue pumps the
 * queued block.
 */
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <Block.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Async And Wait");

	dispatch_queue_t outer = dispatch_queue_create("wasi.aaw.outer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t other = dispatch_queue_create("wasi.aaw.other",
			DISPATCH_QUEUE_SERIAL);
	__block long plain_ran = 0;
	__block long benign_ran = 0;
	__block long nested_ran = 0;
	__block long barrier_ran = 0;

	// control: the plain-block funnel; the nested sync-back runs at the
	// pump, after the inline invoke released the outer queue
	dispatch_async_and_wait(outer, ^{
		dispatch_async(other, ^{
			dispatch_sync(outer, ^{ plain_ran = 1; });
		});
	});
	test_long("plain-block nested async does not run inside "
			"dispatch_async_and_wait", plain_ran, 0);
	dispatch_sync(other, ^{ });
	test_long("contended dispatch_sync runs the plain-block nested "
			"sync-back", plain_ran, 1);

	// privdata, benign body: a lone dispatch_async inside the block must
	// not run inside the fake-ANON wlh region
	dispatch_block_t benign = dispatch_block_create(0, ^{
		dispatch_async(other, ^{ benign_ran = 1; });
	});
	dispatch_async_and_wait(outer, benign);
	dispatch_sync(other, ^{ });
	test_long("contended dispatch_sync runs the nested async of a "
			"dispatch_block_create block", benign_ran, 1);

	// privdata, full nesting: same shape as the plain-block control
	dispatch_block_t nested = dispatch_block_create(0, ^{
		dispatch_async(other, ^{
			dispatch_sync(outer, ^{ nested_ran = 1; });
		});
	});
	dispatch_async_and_wait(outer, nested);
	dispatch_sync(other, ^{ });
	test_long("contended dispatch_sync runs the nested sync-back of a "
			"dispatch_block_create block", nested_ran, 1);

	// privdata through the barrier entry point (same funnel)
	dispatch_block_t barrier = dispatch_block_create(0, ^{
		dispatch_async(other, ^{ barrier_ran = 1; });
	});
	dispatch_barrier_async_and_wait(outer, barrier);
	dispatch_sync(other, ^{ });
	test_long("contended dispatch_sync runs the nested async of a "
			"dispatch_block_create block passed to "
			"dispatch_barrier_async_and_wait", barrier_ran, 1);

	Block_release(benign);
	Block_release(nested);
	Block_release(barrier);
	dispatch_release(outer);
	dispatch_release(other);
	test_stop();
	return 0;
}
