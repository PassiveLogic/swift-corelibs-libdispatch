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
 * A barrier submitted between two concurrent-queue items orders them: the
 * final barrier observes all three, then exits from inside dispatch_main().
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static int count;
static int order[3];

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.barrier",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		dispatch_async(queue, ^{ order[count++] = 1; });
		dispatch_barrier_async(queue, ^{ order[count++] = 2; });
		dispatch_async(queue, ^{ order[count++] = 3; });
		dispatch_barrier_async(queue, ^{
			if (count != 3 || order[0] != 1 || order[1] != 2 ||
					order[2] != 3) {
				printf("FAIL: count=%d order=%d,%d,%d\n", count, order[0],
						order[1], order[2]);
				exit(1);
			}
			puts("barrier order OK");
			exit(0);
		});
	});
	dispatch_main();
}
