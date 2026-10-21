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
 * dispatch_assert_queue correctness. The lock-owner encoding must keep the
 * sole thread's tid distinct from DLOCK_OWNER_NULL after DLOCK_OWNER_MASK
 * (see _dispatch_tid_self in shims/lock.h); if the tid masks to zero, every
 * unlocked queue looks owned by the current thread, dispatch_assert_queue
 * silently passes off-queue, and dispatch_assert_queue_not traps spuriously.
 *
 * Expected behavior verified here:
 *  - inside a block on the queue: dispatch_assert_queue(queue) passes and
 *    dispatch_assert_queue_not(other) passes
 *  - outside the queue: dispatch_assert_queue(queue) crashes with "Block was
 *    expected to execute on queue"
 *
 * The marker "on-queue assertions passed" is printed between the two parts,
 * so a spurious crash in the on-queue part cannot pass the test. If the
 * off-queue assertion returns, main returns 1 without the diagnostic, and
 * the crash test fails.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.assert_queue",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t other = dispatch_queue_create(
			"wasi.assert_queue.other", DISPATCH_QUEUE_SERIAL);
	dispatch_sync(queue, ^{
		dispatch_assert_queue(queue);
		dispatch_assert_queue_not(other);
	});
	puts("on-queue assertions passed");
	fflush(stdout);
	dispatch_assert_queue(queue);
	return 1;
}
