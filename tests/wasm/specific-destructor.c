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
 * Replacing a queue-specific value submits the destructor of the old value
 * to a root queue. The destructor must run at a later pump point, never on
 * the submitting stack under dqsh_lock. Otherwise a destructor that reads
 * the specifics of the same queue deadlocks on the lock that its own caller
 * still holds.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include "wasi-test-pump.h"

static int skey;
static dispatch_queue_t q;
static int destructor_ran;

static void
old_value_destructor(void *ctxt)
{
	// read the specifics of the same queue from inside the destructor. This
	// must not find dqsh_lock held.
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
	if (destructor_ran) {
		printf("FAIL: old-value destructor ran inside set_specific\n");
		exit(1);
	}
	// the destructor was queued before the pump's signal block
	wasi_test_pump();
	if (destructor_ran != 1) {
		printf("FAIL: old-value destructor never ran\n");
		exit(1);
	}
	printf("specific destructor OK\n");
	return 0;
}
