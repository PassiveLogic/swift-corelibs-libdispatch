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
 * dispatch_sync and dispatch_barrier_sync run inline on the sole WASI thread
 * without requiring dispatch_main() or any cooperative drain.
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static void
inc(void *context)
{
	(*(long *)context)++;
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Sync Inline");

	long count = 0;
	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_sync_f(global, &count, inc);
	test_long("dispatch_sync_f on a global queue runs inline", count, 1);

	dispatch_queue_t serial = dispatch_queue_create("wasi.sync.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync_f(serial, &count, inc);
	test_long("dispatch_sync_f on a serial queue runs inline", count, 2);

	dispatch_queue_t concurrent = dispatch_queue_create(
			"wasi.sync.concurrent", DISPATCH_QUEUE_CONCURRENT);
	__block long barrier_ran = 0;
	dispatch_barrier_sync(concurrent, ^{ barrier_ran = 1; });
	test_long("dispatch_barrier_sync runs inline", barrier_ran, 1);

	dispatch_release(serial);
	dispatch_release(concurrent);
	test_stop();
	return 0;
}
