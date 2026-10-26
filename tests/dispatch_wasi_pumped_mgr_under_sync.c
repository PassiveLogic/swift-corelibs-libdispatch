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
 * A top-level wait inside an uncontended dispatch_sync block pumps. The
 * sync frame of the main thread stays on the frame chain while the pump
 * runs. Two resumed timer sources give the manager queue two items, so the
 * manager drain must drain both. It must not narrow because the
 * interrupted frame belongs to the main queue. Narrowing there crashes with
 * "Interrupted drain on manager queue".
 */
#include <dispatch/dispatch.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Pumped Manager Under Sync");

	dispatch_queue_t q = dispatch_queue_create("wasi.sync.q",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t sema = dispatch_semaphore_create(0);
	dispatch_source_t timers[2];
	for (int i = 0; i < 2; i++) {
		timers[i] = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
				dispatch_get_main_queue());
		dispatch_source_set_timer(timers[i], dispatch_time(DISPATCH_TIME_NOW,
				60 * NSEC_PER_SEC), DISPATCH_TIME_FOREVER, 0);
		dispatch_source_set_event_handler(timers[i], ^{ });
		dispatch_resume(timers[i]);
	}
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,
			0), ^{ dispatch_semaphore_signal(sema); });
	__block long r = -1;
	dispatch_sync(q, ^{
		r = dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
	});
	test_long("the wait in the sync block returns after the signal", r, 0);

	for (int i = 0; i < 2; i++) {
		dispatch_source_cancel(timers[i]);
		dispatch_release(timers[i]);
	}
	dispatch_release(sema);
	dispatch_release(q);
	test_stop();
}
