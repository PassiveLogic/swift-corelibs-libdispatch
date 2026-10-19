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

/*
 * A smoke test of the core Dispatch APIs on single-threaded WASI. Each test
 * prints PASS when its checks hold. The last line counts the passed tests.
 */

// the number of tests that main() runs
#define TEST_COUNT 9
// the number of blocks that each FIFO test queues
#define FIFO_ITEMS 10
// the value that the outer block of test_nested_fifo records
#define OUTER_MARK 100

static int failures;
#define CHECK(value) do { if (!(value)) { failures++; \
	printf("check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); } } while (0)

static uint64_t
now_ms(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return ((uint64_t)time.tv_sec * NSEC_PER_SEC + (uint64_t)time.tv_nsec) /
			NSEC_PER_MSEC;
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
	static int order[FIFO_ITEMS];
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo", NULL);
	for (int i = 0; i < FIFO_ITEMS; i++) {
		// the bounds check stops an extra item from writing past the end of
		// order. The count check below still catches it.
		dispatch_async(queue, ^{
			if (count < FIFO_ITEMS) order[count] = i;
			count++;
		});
	}
	CHECK(count == 0); // nothing runs before a pump
	dispatch_sync(queue, ^{ }); // contended: pumps the ten items first
	CHECK(count == FIFO_ITEMS);
	for (int i = 0; i < FIFO_ITEMS; i++) CHECK(order[i] == i);
	dispatch_release(queue);
}

static void
test_nested_fifo(void)
{
	__block int count = 0;
	// the outer block, then the ten nested items
	static int order[FIFO_ITEMS + 1];
	const int capacity = (int)(sizeof(order) / sizeof(order[0]));
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo", NULL);
	dispatch_async(queue, ^{
		for (int i = 0; i < FIFO_ITEMS; i++) {
			// the bounds check stops an extra item from writing past the end
			// of order. The count check below still catches it.
			dispatch_async(queue, ^{
				if (count < capacity) order[count] = i;
				count++;
			});
		}
		if (count < capacity) order[count] = OUTER_MARK;
		count++;
	});
	// The first contended sync is queued ahead of the ten nested items. It
	// pumps only the outer block. The second sync pumps the ten.
	dispatch_sync(queue, ^{ });
	CHECK(count == 1 && order[0] == OUTER_MARK);
	dispatch_sync(queue, ^{ });
	CHECK(count == capacity && order[0] == OUTER_MARK);
	for (int i = 0; i < FIFO_ITEMS; i++) CHECK(order[i + 1] == i);
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
	// the timeout of a wait that nothing signals
	const uint64_t wait_ms = 100;
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = now_ms();
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, wait_ms * NSEC_PER_MSEC));
	uint64_t elapsed = now_ms() - start;
	CHECK(result != 0 && elapsed >= wait_ms);
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
	// The wait returns once the group is empty. The notify block is queued
	// before the waiter wakes, but it can still be pending, as on threaded
	// platforms. A sync on its queue runs after it.
	dispatch_sync(queue, ^{});
	CHECK(notified == 1);
	dispatch_release(group);
	dispatch_release(queue);
}

// A 20 ms dispatch_after, then a timer source with a 20 ms interval that
// cancels itself after TIMER_FIRES fires. The 500 ms wait timeout is far
// longer than the dispatch_after delay.
#define TIMER_FIRES 3

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
		if (++fires == TIMER_FIRES) dispatch_source_cancel(timer);
	});
	dispatch_source_set_cancel_handler(timer, ^{
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_resume(timer);
	CHECK(dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER) == 0);
	CHECK(fires >= TIMER_FIRES);
	dispatch_release(timer);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

static void
test_apply(void)
{
	// the sum of the indexes 0 to 7 is 28
	const size_t iterations = 8;
	__block size_t sum = 0;
	dispatch_apply(iterations, dispatch_get_global_queue(0, 0), ^(size_t index) {
		sum += index;
	});
	CHECK(sum == iterations * (iterations - 1) / 2);
}

static void
test_data(void)
{
	static const char first[] = "01234567";
	static const char second[] = "abcdefgh";
	static const char want[] = "01234567abcdefgh";
	dispatch_data_t one = dispatch_data_create(first, sizeof(first) - 1, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t two = dispatch_data_create(second, sizeof(second) - 1,
			NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t joined = dispatch_data_create_concat(one, two);
	const void *bytes;
	size_t size;
	dispatch_data_t mapped = dispatch_data_create_map(joined, &bytes, &size);
	CHECK(size == sizeof(want) - 1 && memcmp(bytes, want, size) == 0);
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
	printf("ALL PASS (%d/%d)\n", passed, TEST_COUNT);
	return passed == TEST_COUNT ? 0 : 1;
}
