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
 * A walltime deadline follows a forward host wall-clock change while the
 * loop is busy with queue work and no other timer fires. The runner moves
 * the wall clock forward by 60 s after 300 ms. The 30 s walltime timer must
 * fire at the next drain step, not after 30 s of uptime.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
keep_busy(void *context)
{
	usleep(10000);
	dispatch_async_f(dispatch_get_main_queue(), context, keep_busy);
}

int
main(void)
{
	dispatch_queue_t main_q = dispatch_get_main_queue();
	dispatch_after(dispatch_walltime(NULL, 30 * NSEC_PER_SEC), main_q, ^{
		puts("wall timer followed the forward clock change");
		exit(0);
	});
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
			main_q, ^{
		puts("FAIL: wall timer did not follow the forward clock change");
		exit(1);
	});
	dispatch_async_f(main_q, NULL, keep_busy);
	dispatch_main();
}
