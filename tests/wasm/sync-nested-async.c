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
 * simply blocks until the outer sync returns. On the cooperative port, pokes
 * from inside an inline-executed sync body must DEFER (the submitting stack
 * holds the queue's barrier lock) and flush after the sync completes —
 * running them eagerly on the same stack would make the inner sync a
 * spurious "queue already owned by current thread" crash for a program that
 * is correct everywhere else.
 *
 * Same class, dispose flavor: releasing a queue whose specifics carry
 * destructors submits the destructor batch mid-dispose; that push must also
 * defer past the dispose instead of running client code inside it.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

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
	// the deferred qb block flushes once the outer sync has fully completed
	if (!inner_ran) {
		// allow one explicit drain point in case flushing is asynchronous
		dispatch_sync(qb, ^{ });
	}
	if (!inner_ran) {
		printf("FAIL: nested sync-after-async never ran\n");
		exit(1);
	}

	dispatch_queue_t dq = dispatch_queue_create("v2.sync.dispose", NULL);
	dispatch_queue_set_specific(dq, &skey, (void *)1, dispose_destructor);
	dispatch_release(dq);
	if (!dispose_destructor_ran) {
		dispatch_sync(qb, ^{ });
	}
	if (!dispose_destructor_ran) {
		printf("FAIL: dispose-path specific destructor never ran\n");
		exit(1);
	}

	printf("sync nested async OK\n");
	return 0;
}
