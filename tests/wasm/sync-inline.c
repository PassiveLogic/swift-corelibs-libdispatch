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
#include <stdio.h>

static void inc(void *c) { (*(int *)c)++; }

int main(void)
{
	int n = 0;
	dispatch_sync_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
			&n, inc);
	dispatch_queue_t s = dispatch_queue_create("v2.sync.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync_f(s, &n, inc);
	dispatch_queue_t c = dispatch_queue_create("v2.sync.conc",
			DISPATCH_QUEUE_CONCURRENT);
	__block int barrier_ran = 0;
	dispatch_barrier_sync(c, ^{ barrier_ran = 1; });
	if (n != 2 || !barrier_ran) {
		printf("FAIL: n=%d barrier_ran=%d\n", n, barrier_ran);
		return 1;
	}
	printf("sync inline OK\n");
	return 0;
}
