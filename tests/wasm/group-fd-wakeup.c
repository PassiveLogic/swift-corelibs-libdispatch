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
 * A blocking dispatch_group_wait(FOREVER) satisfied by file-descriptor
 * readiness: the group is entered up front, the sole thread parks in the
 * host poll, the runner writes to stdin after a delay, and the read
 * source's handler leaves the group. The semaphore sibling is
 * fd-wakeup-wait.c; this pins that group waits wake on fd events too.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int payload_ok;

int
main(void)
{
	dispatch_group_t group = dispatch_group_create();
	dispatch_queue_t queue = dispatch_queue_create("wasi.groupwake", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			0 /* stdin */, 0, queue);
	if (!source) return 1;
	dispatch_group_enter(group);
	dispatch_source_set_event_handler(source, ^{
		char buf[64] = { 0 };
		ssize_t n = read(0, buf, sizeof(buf) - 1);
		if (n > 0 && strncmp(buf, "ping", 4) == 0) {
			payload_ok = 1;
		} else {
			printf("FAIL: read %zd bytes\n", n);
		}
		dispatch_source_cancel(source);
		dispatch_group_leave(group);
	});
	dispatch_resume(source);
	printf("waiting on group\n");
	fflush(stdout);
	if (dispatch_group_wait(group, DISPATCH_TIME_FOREVER) != 0) {
		printf("FAIL: group_wait returned nonzero\n");
		return 1;
	}
	if (!payload_ok) return 1;
	printf("group fd wakeup OK\n");
	return 0;
}
