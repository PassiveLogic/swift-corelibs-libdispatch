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
 * A read source on a file descriptor that is not open must fail loudly at
 * registration (the fstat guardrail in the WASI event backend), never
 * register silently and hang.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.badfd", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			999, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{ puts("unexpected handler"); });
	dispatch_resume(source);
	puts("invalid-fd source did not crash");
	return 0;
}
