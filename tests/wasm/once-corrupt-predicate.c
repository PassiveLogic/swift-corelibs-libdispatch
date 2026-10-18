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
 * On the sole WASI thread, a lock word that names an owner other than
 * this thread is corrupt: no other thread exists to release it. A garbage
 * dispatch_once_t is the case that a client can make. dispatch_once must
 * crash with a named diagnostic. It must not spin forever.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

static dispatch_once_t predicate;

int
main(void)
{
	// owner bits 0x100: not 0, not the WASI owner value, not DONE
	*(volatile dispatch_once_t *)&predicate = 0x100;
	puts("garbage predicate set");
	fflush(stdout);
	dispatch_once(&predicate, ^{
		puts("initializer ran");
	});
	puts("dispatch_once returned");
	return 0;
}
