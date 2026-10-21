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
 * A dispatch_sync body submits async work that itself syncs back onto the
 * outer queue. On threaded platforms the async block runs on a worker and
 * simply blocks until the outer sync returns. On the cooperative port a poke
 * never runs the block on the submitting stack (which holds the queue's
 * barrier lock); the block runs at the next pump point, here a contended
 * dispatch_sync, after the outer sync has released the lock. Running it
 * inline would make the inner sync a spurious "queue already owned by
 * current thread" crash for a program that is correct everywhere else.
 *
 * Same class, dispose flavor: releasing a queue whose specifics carry
 * destructors submits the destructor batch mid-dispose; that batch must run
 * at a later pump, never inside the dispose.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

static int specific_key;
static long destructor_ran;

static void
dispose_destructor(void *context)
{
	(void)context;
	destructor_ran = 1;
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Sync Nested Async");

	dispatch_queue_t outer = dispatch_queue_create("wasi.sync.outer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t other = dispatch_queue_create("wasi.sync.other",
			DISPATCH_QUEUE_SERIAL);
	__block long inner_ran = 0;

	dispatch_sync(outer, ^{
		dispatch_async(other, ^{
			// must not run while the frame below holds the outer
			// queue's barrier lock
			dispatch_sync(outer, ^{ inner_ran = 1; });
		});
	});
	test_long("async block submitted inside dispatch_sync does not run "
			"while the outer queue is held", inner_ran, 0);
	// the other queue has a pending item, so this sync is contended and
	// pumps it
	dispatch_sync(other, ^{ });
	test_long("contended dispatch_sync runs the pending async block and "
			"its nested sync", inner_ran, 1);

	dispatch_queue_t disposed = dispatch_queue_create("wasi.sync.dispose",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_set_specific(disposed, &specific_key, (void *)1,
			dispose_destructor);
	dispatch_release(disposed);
	test_long("specific destructor does not run inside the queue dispose",
			destructor_ran, 0);
	wasi_test_pump();
	test_long("specific destructor runs at the next pump",
			destructor_ran, 1);

	dispatch_release(outer);
	dispatch_release(other);
	test_stop();
	return 0;
}
