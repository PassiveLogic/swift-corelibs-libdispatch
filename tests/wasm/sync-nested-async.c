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
#include <stdio.h>
#include <stdlib.h>
#include "wasi-test-pump.h"

static int skey;
static int dispose_destructor_ran;

static void
dispose_destructor(void *ctxt)
{
	(void)ctxt;
	dispose_destructor_ran = 1;
}

int
main(void)
{
	dispatch_queue_t qa = dispatch_queue_create("v2.sync.outer", NULL);
	dispatch_queue_t qb = dispatch_queue_create("v2.sync.other", NULL);
	__block int inner_ran = 0;

	dispatch_sync(qa, ^{
		dispatch_async(qb, ^{
			// must not run while qa's barrier lock is held by the frame below
			dispatch_sync(qa, ^{ inner_ran = 1; });
		});
	});
	if (inner_ran) {
		printf("FAIL: async block ran while qa's barrier lock was held\n");
		exit(1);
	}
	// qb has a pending item, so this sync is contended and pumps it
	dispatch_sync(qb, ^{ });
	if (!inner_ran) {
		printf("FAIL: nested sync-after-async never ran\n");
		exit(1);
	}

	dispatch_queue_t dq = dispatch_queue_create("v2.sync.dispose", NULL);
	dispatch_queue_set_specific(dq, &skey, (void *)1, dispose_destructor);
	dispatch_release(dq);
	if (dispose_destructor_ran) {
		printf("FAIL: dispose-path specific destructor ran inside dispose\n");
		exit(1);
	}
	wasi_test_pump();
	if (!dispose_destructor_ran) {
		printf("FAIL: dispose-path specific destructor never ran\n");
		exit(1);
	}

	printf("sync nested async OK\n");
	return 0;
}
