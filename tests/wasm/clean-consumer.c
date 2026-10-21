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
 * A program that uses Dispatch without ever pumping (no dispatch_main(), no
 * blocking wait) exits 0: an uncontended dispatch_sync runs inline, and
 * releasing the queue submits nothing that a pump must run.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Clean Consumer");

	__block long value = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.consumer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync(queue, ^{ value = 1; });
	dispatch_release(queue);
	test_long("uncontended dispatch_sync runs the block inline", value, 1);
	test_stop();
	return 0;
}
