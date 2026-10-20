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
 * root queue. The destructor must run at a later pump point, never on the
 * submitting stack under dqsh_lock: a destructor that touches the same
 * queue's specifics would otherwise deadlock on the lock its own caller
 * still holds.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

static int specific_key;
static dispatch_queue_t queue;
static long destructor_context;

static void
old_value_destructor(void *context)
{
	// re-enter the same queue's specifics; this deadlocks if the destructor
	// runs under dqsh_lock
	void *value = dispatch_queue_get_specific(queue, &specific_key);
	test_ptr_notnull("replacement value is visible in the old value's "
			"destructor", value);
	destructor_context = (long)(intptr_t)context;
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Specific Destructor");

	queue = dispatch_queue_create("wasi.specific", DISPATCH_QUEUE_SERIAL);
	dispatch_queue_set_specific(queue, &specific_key, (void *)1,
			old_value_destructor);
	// replace: submits old_value_destructor((void *)1)
	dispatch_queue_set_specific(queue, &specific_key, (void *)2, NULL);
	test_long("old value's destructor does not run inside "
			"dispatch_queue_set_specific", destructor_context, 0);
	// the destructor was queued before the pump's signal block
	wasi_test_pump();
	test_long("old value's destructor runs at the next pump with the old "
			"value", destructor_context, 1);
	// the current value has no destructor, so disposing the specifics
	// submits nothing
	dispatch_release(queue);
	test_stop();
	return 0;
}
