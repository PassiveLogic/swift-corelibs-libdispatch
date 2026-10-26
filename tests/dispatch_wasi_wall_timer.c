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
 * A dispatch_after with a dispatch_walltime() deadline fires on a serial
 * queue while main blocks in a timed semaphore wait, and not before its
 * 20 ms deadline.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Wall Timer");

	dispatch_queue_t queue = dispatch_queue_create("wasi.wall.timer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = dispatch_time(DISPATCH_TIME_NOW, 0);
	dispatch_after(dispatch_walltime(NULL, 20 * NSEC_PER_MSEC), queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
	long elapsed_ms = (long)((dispatch_time(DISPATCH_TIME_NOW, 0) - start) / NSEC_PER_MSEC);
	dispatch_release(semaphore);
	dispatch_release(queue);
	test_long("walltime timer signals before the 500 ms wait times out",
			result, 0);
	test_long_greater_than_or_equal("walltime timer fires no earlier than "
			"20 ms", elapsed_ms, 20);
	test_stop();
	return 0;
}
