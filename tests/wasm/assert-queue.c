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
 * dispatch_assert_queue must find the correct queue owner on the sole WASI
 * thread. After DLOCK_OWNER_MASK, the thread id must stay different from
 * DLOCK_OWNER_NULL (see _dispatch_tid_self in src/shims/lock.h). If the
 * masked id is zero, every unlocked queue looks owned by the current thread.
 * Then dispatch_assert_queue passes off-queue, and dispatch_assert_queue_not
 * traps on a correct call.
 *
 * The test checks two cases:
 *  - on-queue: dispatch_assert_queue passes, and dispatch_assert_queue_not
 *    passes for a different queue
 *  - off-queue: dispatch_assert_queue traps with the standard diagnostic
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int main(void)
{
	dispatch_queue_t serial = dispatch_queue_create("v2.assertq",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t other = dispatch_queue_create("v2.assertq.other",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync(serial, ^{
		dispatch_assert_queue(serial);
		dispatch_assert_queue_not(other);
	});
	printf("assert on-queue OK\n");
	fflush(stdout);
	// Not on this queue: must trap, never pass silently.
	dispatch_assert_queue(serial);
	printf("FAIL: off-queue dispatch_assert_queue did not trap\n");
	return 1;
}
