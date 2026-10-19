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
 * A pumped item syncs onto a queue that the waiting frame below it holds.
 * On threads the item would block until that frame finishes. On the sole
 * thread the frame sits under the item on the stack and can never finish
 * first, so the inner sync must crash with the named single-threaded
 * deadlock diagnostic, not with a generic "already owned by current
 * thread" crash.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t held = dispatch_queue_create("wasi.pumped.held", NULL);
	dispatch_queue_t other = dispatch_queue_create("wasi.pumped.other", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_sync(held, ^{
		dispatch_async(other, ^{
			dispatch_semaphore_signal(sem);
			puts("pumped item syncs onto the held queue");
			fflush(stdout);
			dispatch_sync(held, ^{ });
		});
		dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	});
	puts("FAIL: inner sync returned");
	return 1;
}
