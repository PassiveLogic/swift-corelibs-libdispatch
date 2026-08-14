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
 * Signal-source registration takes over the emulated-signal disposition for
 * the source's lifetime; unregistration must RESTORE the application's own
 * signal() handler, not reset to SIG_DFL (which would turn the app's next
 * raise() into process termination).
 */
#include <dispatch/dispatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static int app_hits;

static void
app_handler(int signo)
{
	(void)signo;
	app_hits++;
}

int
main(void)
{
	if (signal(SIGUSR1, app_handler) == SIG_ERR) {
		printf("FAIL: signal() install\n");
		exit(1);
	}
	raise(SIGUSR1);
	if (app_hits != 1) {
		printf("FAIL: app handler did not run before source (%d)\n", app_hits);
		exit(1);
	}

	dispatch_queue_t q = dispatch_queue_create("v2.sigdisp", NULL);
	dispatch_source_t src = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
			SIGUSR1, 0, q);
	dispatch_semaphore_t delivered = dispatch_semaphore_create(0);
	dispatch_semaphore_t cancelled = dispatch_semaphore_create(0);
	dispatch_source_set_event_handler(src, ^{
		dispatch_semaphore_signal(delivered);
	});
	dispatch_source_set_cancel_handler(src, ^{
		dispatch_semaphore_signal(cancelled);
	});
	dispatch_resume(src);

	raise(SIGUSR1); // owned by the source now, not the app handler
	if (dispatch_semaphore_wait(delivered,
			dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) != 0) {
		printf("FAIL: source never received the signal\n");
		exit(1);
	}
	if (app_hits != 1) {
		printf("FAIL: app handler ran while source owned the signal (%d)\n",
				app_hits);
		exit(1);
	}

	dispatch_source_cancel(src);
	if (dispatch_semaphore_wait(cancelled,
			dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) != 0) {
		printf("FAIL: cancellation never completed\n");
		exit(1);
	}
	dispatch_release(src);

	// disposition must be the app's handler again — with SIG_DFL this
	// raise() terminates the process instead
	raise(SIGUSR1);
	if (app_hits != 2) {
		printf("FAIL: app handler not restored after source teardown (%d)\n",
				app_hits);
		exit(1);
	}
	printf("signal disposition OK\n");
	return 0;
}
