/*
 * This source file is part of the Swift.org open source project
 *
 * Copyright (c) 2026 Apple Inc. and the Swift project authors
 *
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See https://swift.org/LICENSE.txt for license information
 * See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
 *
 */

/*
 * An allocation failure in libdispatch must crash with a named diagnostic on
 * single-threaded WASI. It must not hang. Threaded platforms sleep and retry,
 * because another thread can free memory meanwhile. WASI has no other
 * thread, and the module memory is at its maximum. So a retry never succeeds.
 *
 * The test fills the heap, then asks libdispatch to allocate.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

// The module memory limit. It must match -Wl,--max-memory in CMakeLists.txt.
#define MAX_MEMORY_BYTES (16u << 20)

// The smallest block. Each block stores a pointer to the previous block, so
// each block must hold one pointer.
#define MIN_BLOCK_SIZE 8
_Static_assert(MIN_BLOCK_SIZE >= sizeof(void *),
		"a heap block must hold one pointer");

// The head of the linked list of all blocks. The test never frees the
// blocks, so the heap stays full. The volatile store stops the compiler from
// removing the malloc calls.
static void *volatile blocks;

static void
work(void *context)
{
	(void)context;
}

static void
exhaust_heap(void)
{
	// Block sizes, largest first: 1 MiB, 64 KiB, 4 KiB, 256 B, 64 B, 16 B,
	// and 8 B. Large blocks fill most of the heap fast. Small blocks then
	// fill the gaps that remain.
	static const size_t block_sizes[] = {
		1u << 20, 1u << 16, 1u << 12, 1u << 8, 64, 16, MIN_BLOCK_SIZE,
	};
	size_t count = sizeof(block_sizes) / sizeof(block_sizes[0]);
	for (size_t i = 0; i < count; i++) {
		size_t size = block_sizes[i];
		// The loop ends when malloc returns NULL. The memory limit
		// makes that happen before the count limit. The count limit
		// is a backstop.
		for (size_t n = 0; n < MAX_MEMORY_BYTES / size; n++) {
			void **block = malloc(size);
			if (block == NULL) break;
			// size >= MIN_BLOCK_SIZE, so this write stays inside
			// the block
			*block = blocks;
			blocks = block;
		}
	}
}

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.alloc_failure",
			DISPATCH_QUEUE_SERIAL);
	exhaust_heap();
	puts("heap exhausted");
	fflush(stdout);
#if ALLOC_FAILURE_ASYNC
	// continuation allocator (_dispatch_malloc_continuation_alloc)
	dispatch_async_f(queue, NULL, work);
#else
	// object allocator (_os_object_alloc_realized)
	(void)queue;
	(void)dispatch_queue_create("wasi.alloc_failure.second",
			DISPATCH_QUEUE_SERIAL);
	(void)work;
#endif
	// the allocation above must crash, so this line never runs
	puts("FAIL: allocation returned");
	return 1;
}
