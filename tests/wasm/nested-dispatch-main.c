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
 * A dispatch_main() call from within a drained work item must crash at
 * once: drains do not nest. An armed timer keeps the outer loop parked. The
 * timer must not turn the crash into a sleep or a spin.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC),
			dispatch_get_main_queue(), ^{
		puts("timer must not fire");
	});
	dispatch_async(dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		puts("nested dispatch_main starting");
		fflush(stdout);
		dispatch_main();
	});
	dispatch_main();
}
