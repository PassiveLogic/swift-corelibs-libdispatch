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
 * A dispatch_once initializer waits, and the wait pumps an item that enters
 * the same dispatch_once. On threads the item would block until the
 * initializer returns. On the sole thread the initializer sits under the
 * item on the stack, so the once gate wait must crash with the named
 * single-threaded deadlock diagnostic, not with "trying to lock
 * recursively".
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

static dispatch_once_t pred;

int
main(void)
{
	dispatch_queue_t other = dispatch_queue_create("wasi.pumped.other", NULL);
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_once(&pred, ^{
		dispatch_async(other, ^{
			dispatch_semaphore_signal(sem);
			puts("pumped item enters the same dispatch_once");
			fflush(stdout);
			dispatch_once(&pred, ^{ puts("FAIL: initializer ran twice"); });
		});
		dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	});
	puts("FAIL: once returned");
	return 1;
}
