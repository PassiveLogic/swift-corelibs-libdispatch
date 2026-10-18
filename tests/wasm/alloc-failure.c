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

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

// On threaded platforms an allocation failure inside libdispatch sleeps and
// retries, because another thread can free memory meanwhile. The sole WASI
// thread cannot free anything while it sleeps, and the module memory is at
// its declared maximum, so the retry would never succeed. The library must
// crash with a named diagnostic instead of hanging.
//
// The test links with a small --max-memory, takes all heap memory, and then
// asks libdispatch to allocate.

static void
work(void *ctxt)
{
	(void)ctxt;
}

static void
exhaust_heap(void)
{
	static const size_t sizes[] = {
		1u << 20, 1u << 16, 1u << 12, 1u << 8, 64, 16, 8,
	};
	void *hoard = NULL;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		void *p;
		while ((p = malloc(sizes[i])) != NULL) {
			*(void **)p = hoard;
			hoard = p;
		}
	}
	// the hoard is never freed, so the heap stays full
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	dispatch_queue_t queue = dispatch_queue_create("wasi.alloc.failure", NULL);
	exhaust_heap();
	puts("heap exhausted");
#if ALLOC_FAILURE_ASYNC
	// continuation allocator (_dispatch_malloc_continuation_alloc)
	dispatch_async_f(queue, NULL, work);
#else
	// object allocator (_os_object_alloc_realized)
	(void)queue;
	(void)dispatch_queue_create("wasi.alloc.failure.second", NULL);
	(void)work;
#endif
	puts("UNEXPECTED: allocation returned");
	return 0;
}
