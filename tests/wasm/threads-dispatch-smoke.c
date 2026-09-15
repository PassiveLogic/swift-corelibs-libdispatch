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

// Proof of concept for multi-threaded dispatch on wasm32-wasip1-threads:
// dispatch_async blocks must run on worker threads (not the main thread),
// dispatch_semaphore must block and wake across threads, dispatch_sync must
// funnel across threads, and dispatch_after must fire through the manager
// thread. Run under a wasi-threads host (wasmtime v24: -S threads).

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define ASYNC_N 8

static pthread_t main_thread;
static _Atomic int async_ran;
static _Atomic int on_worker;
static _Atomic int timer_fired;
static _Atomic int sync_ran;

int
main(void)
{
	main_thread = pthread_self();
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	dispatch_queue_t gq =
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

	for (int i = 0; i < ASYNC_N; i++) {
		dispatch_async(gq, ^{
			if (!pthread_equal(pthread_self(), main_thread)) {
				atomic_fetch_add(&on_worker, 1);
			}
			atomic_fetch_add(&async_ran, 1);
			dispatch_semaphore_signal(done);
		});
	}
	for (int i = 0; i < ASYNC_N; i++) {
		if (dispatch_semaphore_wait(done,
				dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
			printf("FAIL: async completion timeout (i=%d ran=%d)\n", i,
					atomic_load(&async_ran));
			return 1;
		}
	}

	dispatch_queue_t sq = dispatch_queue_create("smoke.serial", NULL);
	dispatch_sync(sq, ^{
		atomic_store(&sync_ran, 1);
	});

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), gq, ^{
		atomic_store(&timer_fired, 1);
		dispatch_semaphore_signal(done);
	});
	if (dispatch_semaphore_wait(done,
			dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
		printf("FAIL: dispatch_after timeout\n");
		return 1;
	}

	int ran = atomic_load(&async_ran);
	int workers = atomic_load(&on_worker);
	int timer = atomic_load(&timer_fired);
	int sync = atomic_load(&sync_ran);
	int pass = ran == ASYNC_N && workers == ASYNC_N && timer && sync;
	printf("%s: async=%d/%d on-worker-thread=%d/%d sync=%d timer=%d\n",
			pass ? "PASS" : "FAIL", ran, ASYNC_N, workers, ASYNC_N, sync,
			timer);
	return !pass;
}
