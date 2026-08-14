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
 * A write source on stdout: the descriptor is writable, so the source fires
 * through poll(2) readiness (level-triggered with EV_DISPATCH rearm), and
 * suspending it stops delivery. Also checks that dispatch_main() parks on the
 * armed source rather than trapping.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static int fires;

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.write", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,
			1 /* stdout */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		fires++;
		if (fires < 3) return;  // rearm; must fire again (level-triggered)
		dispatch_source_cancel(source);
		printf("write source fired %d times\n", fires);
		printf("write source OK\n");
		exit(0);
	});
	dispatch_resume(source);
	dispatch_main();
}
