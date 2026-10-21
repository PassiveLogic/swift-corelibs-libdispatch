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
 * Shared helpers for the focused WASI tests.
 */
#ifndef WASI_TEST_H
#define WASI_TEST_H

#include <dispatch/dispatch.h>
#include <stdint.h>

// the uptime clock, in nanoseconds
static inline uint64_t
wasi_test_now_ns(void)
{
	return dispatch_time(DISPATCH_TIME_NOW, 0);
}

/*
 * A top-level blocking wait drains pending Dispatch work. The signal block is
 * queued on the default root queue after everything the test submitted
 * there, so the wait returns once that earlier work ran.
 */
static inline void
wasi_test_pump(void)
{
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	dispatch_release(semaphore);
}

#endif /* WASI_TEST_H */
