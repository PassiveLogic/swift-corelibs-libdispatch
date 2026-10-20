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
 * An indefinite-wait deadlock diagnostic names the public call that
 * blocked: a dispatch_sync that waits behind queued work, inside a drained
 * work item, so nothing can satisfy it. dispatch_wasi_nested_wait.c covers
 * the dispatch_semaphore_wait case.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_queue_t queue = dispatch_queue_create("wasi.wait.diag",
			DISPATCH_QUEUE_SERIAL);
	dispatch_async(global, ^{
		puts("nested sync starting");
		fflush(stdout);
		dispatch_async(queue, ^{});
		dispatch_sync(queue, ^{});
	});
	dispatch_main();
}
