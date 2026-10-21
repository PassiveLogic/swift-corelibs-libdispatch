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
 * An indefinite semaphore wait issued from inside a drained work item must
 * crash at once with "Deadlock: semaphore wait from a work item". On
 * the sole WASI thread nested waits pump nothing, so nothing can ever signal
 * the semaphore: crashing is the only answer that does not hang. A 10 s
 * timer on the same queue must not turn the crash into a park; the 5 s test
 * timeout catches a wait that sleeps toward the timer. The work item prints
 * a marker before it waits, so the test proves the crash came from the
 * nested wait.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.nested.wait",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC),
			queue, ^{ });
	dispatch_async(queue, ^{
		puts("nested wait started");
		fflush(stdout);
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	});
	// dispatch_main() drains the queued item; its endless wait must crash
	dispatch_main();
}
