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
 * A wasm trap carries no payload, so the crash reason on stderr is the only
 * diagnostic. LIBDISPATCH_LOG must not hide it: with the log disabled (NO)
 * or sent to a file (file), the reason must still reach stderr.
 *
 * The runner starts the guest with an empty environment, so the test sets
 * LIBDISPATCH_LOG itself before the first libdispatch log call.
 *
 * CRASH_LOG_ENV  value for LIBDISPATCH_LOG
 * CRASH_LOG_ASSERT  0: client crash (over-resume, DISPATCH_CLIENT_CRASH)
 *                   1: client assertion (dispatch_assert_queue off-queue)
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CRASH_LOG_ENV
#define CRASH_LOG_ENV "NO"
#endif
#ifndef CRASH_LOG_ASSERT
#define CRASH_LOG_ASSERT 0
#endif

int
main(void)
{
	if (setenv("LIBDISPATCH_LOG", CRASH_LOG_ENV, 1) != 0) {
		printf("FAIL: setenv\n");
		return 1;
	}
	dispatch_queue_t q = dispatch_queue_create("crash.log.env", NULL);
	printf("crash log env %s armed\n", CRASH_LOG_ENV);
	fflush(stdout);
#if CRASH_LOG_ASSERT
	dispatch_assert_queue(q);
#else
	dispatch_resume(q);
#endif
	printf("FAIL: no crash\n");
	return 1;
}
