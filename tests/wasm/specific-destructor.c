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
 * Replacing a queue-specific value submits the old value's destructor to a
 * root queue. Under the eager drain that submission can run the destructor
 * immediately on the submitting stack, so it must happen after dqsh_lock is
 * dropped: a destructor that touches the same queue's specifics would
 * otherwise deadlock on the lock its own caller still holds (see
 * WAIT-PUMPING-AUDIT.md).
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static int skey;
static dispatch_queue_t q;
static int destructor_ran;

static void
old_value_destructor(void *ctxt)
{
	// re-enter the same queue's specifics from inside the destructor; this
	// must not observe a held dqsh_lock
	if (dispatch_queue_get_specific(q, &skey) == NULL) {
		printf("FAIL: replacement value not visible in destructor\n");
		exit(1);
	}
	destructor_ran = (int)(intptr_t)ctxt;
}

int
main(void)
{
	q = dispatch_queue_create("wasi.specific", NULL);
	dispatch_queue_set_specific(q, &skey, (void *)1, old_value_destructor);
	// replace: submits old_value_destructor((void *)1)
	dispatch_queue_set_specific(q, &skey, (void *)2, NULL);
	if (destructor_ran != 1) {
		// eager drain runs it during the replace; if policy ever changes to
		// deferred, drain via a blocking wait before failing
		dispatch_semaphore_t sem = dispatch_semaphore_create(0);
		dispatch_async(dispatch_get_global_queue(
				DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
			dispatch_semaphore_signal(sem);
		});
		dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	}
	if (destructor_ran != 1) {
		printf("FAIL: old-value destructor never ran\n");
		exit(1);
	}
	printf("specific destructor OK\n");
	return 0;
}
