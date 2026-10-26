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
 * DispatchIO stream channel over a non-regular descriptor (stdin): the
 * stream path rides fd-readiness sources, and the runner writes the payload
 * only after a delay, so completing the read proves the channel parks on
 * readiness rather than spinning or failing at registration.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char collected[16];
static size_t collected_len;

int
main(void)
{
	dispatch_queue_t q = dispatch_queue_create("v2.iostream", NULL);
	dispatch_io_t ch = dispatch_io_create(DISPATCH_IO_STREAM, 0 /* stdin */,
			q, ^(int error) {
		printf("channel cleanup error=%d\n", error);
	});
	if (!ch) {
		printf("FAIL: dispatch_io_create returned NULL\n");
		exit(1);
	}
	dispatch_io_set_low_water(ch, 1);
	dispatch_io_read(ch, 0, 4, q, ^(bool done, dispatch_data_t data, int error) {
		if (error) {
			printf("FAIL: read error %d\n", error);
			exit(1);
		}
		if (data) {
			dispatch_data_apply(data, ^bool(dispatch_data_t region,
					size_t offset, const void *buffer, size_t size) {
				(void)region; (void)offset;
				if (collected_len + size < sizeof(collected)) {
					memcpy(collected + collected_len, buffer, size);
					collected_len += size;
				}
				return true;
			});
		}
		if (done) {
			if (collected_len >= 4 && strncmp(collected, "ping", 4) == 0) {
				printf("dispatch io stream OK\n");
				exit(0);
			}
			printf("FAIL: read %zu bytes: \"%s\"\n", collected_len, collected);
			exit(1);
		}
	});
	dispatch_main();
}
