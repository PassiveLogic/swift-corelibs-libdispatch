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
 * Root queues drain highest QoS first: a HIGH item submitted after a LOW
 * item still runs before it. The LOW item runs last and exits from inside
 * dispatch_main().
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static int count;
static int order[2];

int
main(void)
{
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{
			order[count++] = 1;
			if (count != 2 || order[0] != 2 || order[1] != 1) {
				printf("FAIL: count=%d order=%d,%d\n", count, order[0],
						order[1]);
				exit(1);
			}
			puts("qos order OK");
			exit(0);
		});
		dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
			order[count++] = 2;
		});
	});
	dispatch_main();
}
