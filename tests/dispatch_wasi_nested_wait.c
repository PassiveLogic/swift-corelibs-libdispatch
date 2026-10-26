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
 * An indefinite semaphore wait issued from inside a drained work item must
 * crash at once with "Deadlock: semaphore wait from a work item". On
 * the sole WASI thread nested waits pump nothing, so nothing can ever signal
 * the semaphore: crashing is the only answer that does not hang. A 10 s
 * timer on the same queue must not turn the crash into a park; the 5 s test
 * timeout catches a wait that sleeps toward the timer. The work item prints
 * a marker before it waits, so the test proves the crash came from the
 * nested wait.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.nested.wait",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC),
			queue, ^{ });
	dispatch_async(queue, ^{
		puts("nested wait started");
		fflush(stdout);
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	});
	// dispatch_main() drains the queued item; its endless wait must crash
	dispatch_main();
}
