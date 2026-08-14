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
 * A read source on stdin. The runner writes the payload only after a delay,
 * so passing requires dispatch_main() to genuinely park in the host poll
 * until the descriptor becomes readable — not to find data already buffered.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.read", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			0 /* stdin */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		char buf[64] = { 0 };
		ssize_t n = read(0, buf, sizeof(buf) - 1);
		if (n > 0 && strncmp(buf, "ping", 4) == 0) {
			printf("read source OK\n");
			exit(0);
		}
		printf("FAIL: read %zd bytes\n", n);
		exit(1);
	});
	dispatch_resume(source);
	printf("read source armed\n");
	fflush(stdout);
	dispatch_main();
}
