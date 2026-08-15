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
 * Pipe EOF against a read source whose handler observes the 0-byte read but
 * (wrongly) never cancels. The runner feeds stdin, then closes the write
 * end. The handler must fire at EOF so the client can observe read() == 0 -
 * and then the park must not become a silent hot loop:
 *
 * - On hosts that report the poll_oneoff hangup flag (Node's uvwasi), the
 *   harvest delivers EOF and stops watching the descriptor (the epoll
 *   EPOLLHUP discipline); with the source gone, dispatch_main() traps as
 *   truly idle.
 * - On hosts that never report hangup (wasmtime 47 for pipes; emulated by
 *   the runner's --suppress-poll-hangup), EOF is indistinguishable from
 *   readiness, so the port converts the resulting permanently-instantly-
 *   ready park into a named crash instead of spinning silently.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <unistd.h>

static unsigned long fires;
static int eof_seen;

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.eof", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			0 /* stdin */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		char buf[64];
		ssize_t n = read(0, buf, sizeof(buf));
		fires++;
		if (n == 0 && !eof_seen) {
			eof_seen = 1;
			printf("pipe EOF observed\n");
			fflush(stdout);
		}
		// deliberately no dispatch_source_cancel(): the buggy-client shape
	});
	dispatch_resume(source);
	printf("read source armed\n");
	fflush(stdout);
	dispatch_main();
}
