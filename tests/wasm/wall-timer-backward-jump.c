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
 * A walltime deadline never fires early after a backward host wall-clock
 * change. The runner moves the wall clock back by 60 s after 100 ms. The
 * 500 ms walltime timer must not fire before the 1.5 s uptime check. A
 * 50 ms uptime timer provides the wakes.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	dispatch_queue_t main_q = dispatch_get_main_queue();
	dispatch_source_t ticker = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, main_q);
	dispatch_source_set_timer(ticker, DISPATCH_TIME_NOW,
			50 * NSEC_PER_MSEC, 0);
	dispatch_source_set_event_handler(ticker, ^{ });
	dispatch_resume(ticker);

	dispatch_after(dispatch_walltime(NULL, 500 * NSEC_PER_MSEC), main_q, ^{
		puts("FAIL: wall timer fired before the backward clock change "
				"caught up");
		exit(1);
	});
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC),
			main_q, ^{
		puts("wall timer followed the backward clock change");
		exit(0);
	});
	dispatch_main();
}
