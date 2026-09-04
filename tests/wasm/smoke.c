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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(value) do { if (!(value)) { failures++; \
	printf("check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); } } while (0)

static uint64_t
now_ms(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

static int once_count;
static void once(void *context) { (void)context; once_count++; }
static char specific_key, specific_value;

static void
test_once_and_specifics(void)
{
	static dispatch_once_t predicate;
	dispatch_once_f(&predicate, NULL, once);
	dispatch_once_f(&predicate, NULL, once);
	CHECK(once_count == 1);
	dispatch_queue_t queue = dispatch_queue_create("wasi.specifics", NULL);
	dispatch_queue_set_specific(queue, &specific_key, &specific_value, NULL);
	CHECK(dispatch_get_specific(&specific_key) == NULL);
	__block void *value;
	dispatch_sync(queue, ^{ value = dispatch_get_specific(&specific_key); });
	CHECK(value == &specific_value);
	dispatch_release(queue);
}

static void
test_fifo(void)
{
	__block int count = 0;
	static int order[10];
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo", NULL);
	for (int i = 0; i < 10; i++) {
		dispatch_async(queue, ^{ order[count++] = i; });
	}
	CHECK(count == 0); // nothing runs before a pump
	dispatch_sync(queue, ^{ }); // contended: pumps the ten items first
	CHECK(count == 10);
	for (int i = 0; i < 10; i++) CHECK(order[i] == i);
	dispatch_release(queue);
}

static void
test_nested_fifo(void)
{
	__block int count = 0;
	static int order[11];
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo", NULL);
	dispatch_async(queue, ^{
		for (int i = 0; i < 10; i++) {
			dispatch_async(queue, ^{ order[count++] = i; });
		}
		order[count++] = 100;
	});
	// the first contended sync is queued ahead of the ten nested items and
	// pumps only the outer block; the second pumps the ten
	dispatch_sync(queue, ^{ });
	CHECK(count == 1 && order[0] == 100);
	dispatch_sync(queue, ^{ });
	CHECK(count == 11 && order[0] == 100);
	for (int i = 0; i < 10; i++) CHECK(order[i + 1] == i);
	dispatch_release(queue);
}

static void
test_sync_and_barrier(void)
{
	__block int value = 0;
	dispatch_queue_t serial = dispatch_queue_create("wasi.sync", NULL);
	dispatch_async(serial, ^{ value = 1; });
	dispatch_sync(serial, ^{ CHECK(value == 1); value = 2; });
	CHECK(value == 2);
	dispatch_queue_t concurrent = dispatch_queue_create("wasi.barrier",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_barrier_sync(concurrent, ^{ value = 3; });
	CHECK(value == 3);
	dispatch_release(concurrent);
	dispatch_release(serial);
}

static void
test_semaphore(void)
{
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = now_ms();
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC));
	uint64_t elapsed = now_ms() - start;
	CHECK(result != 0 && elapsed >= 100);
	dispatch_semaphore_signal(semaphore);
	CHECK(dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER) == 0);
	dispatch_release(semaphore);
}

static void
test_group(void)
{
	__block int notified = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.group", NULL);
	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_group_notify(group, queue, ^{ notified++; });
	dispatch_async(queue, ^{ dispatch_group_leave(group); });
	CHECK(dispatch_group_wait(group, DISPATCH_TIME_FOREVER) == 0);
	CHECK(notified == 1);
	dispatch_release(group);
	dispatch_release(queue);
}

static void
test_timers(void)
{
	__block int fires = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.timer", NULL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	CHECK(dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC)) == 0);
	dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
			0, 0, queue);
	dispatch_source_set_timer(timer,
			dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
			20 * NSEC_PER_MSEC, 0);
	dispatch_source_set_event_handler(timer, ^{
		if (++fires == 3) dispatch_source_cancel(timer);
	});
	dispatch_source_set_cancel_handler(timer, ^{
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_resume(timer);
	CHECK(dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER) == 0);
	CHECK(fires >= 3);
	dispatch_release(timer);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

static void
test_apply(void)
{
	__block size_t sum = 0;
	dispatch_apply(8, dispatch_get_global_queue(0, 0), ^(size_t index) {
		sum += index;
	});
	CHECK(sum == 28);
}

static void
test_data(void)
{
	static const char first[] = "01234567";
	static const char second[] = "abcdefgh";
	dispatch_data_t one = dispatch_data_create(first, 8, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t two = dispatch_data_create(second, 8, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t joined = dispatch_data_create_concat(one, two);
	const void *bytes;
	size_t size;
	dispatch_data_t mapped = dispatch_data_create_map(joined, &bytes, &size);
	CHECK(size == 16 && memcmp(bytes, "01234567abcdefgh", 16) == 0);
	dispatch_release(mapped);
	dispatch_release(joined);
	dispatch_release(two);
	dispatch_release(one);
}

static void
run(const char *name, void (*test)(void), int *passed)
{
	int before = failures;
	test();
	if (before == failures) { printf("PASS %s\n", name); (*passed)++; }
}

int
main(void)
{
	int passed = 0;
	run("once_and_specifics", test_once_and_specifics, &passed);
	run("fifo", test_fifo, &passed);
	run("fifo_nested_backlog", test_nested_fifo, &passed);
	run("sync_and_barrier", test_sync_and_barrier, &passed);
	run("semaphore_timing", test_semaphore, &passed);
	run("group_wait_notify", test_group, &passed);
	run("after_timers_cancel", test_timers, &passed);
	run("apply", test_apply, &passed);
	run("data", test_data, &passed);
	if (failures) return 1;
	printf("ALL PASS (%d/9)\n", passed);
	return passed == 9 ? 0 : 1;
}
