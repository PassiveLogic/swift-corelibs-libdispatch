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
 * Core Dispatch smoke for single-threaded WASI, pumped only by top-level
 * blocking waits: dispatch_once and queue specifics, serial FIFO order (also
 * with a backlog submitted from inside a running item), sync and barrier
 * sync, semaphore timing, group wait and notify, dispatch_after and a
 * repeating timer, dispatch_apply, and dispatch_data concatenation.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <string.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

static long once_count;
static char specific_key, specific_value;

static void
count_once(void *context)
{
	(void)context;
	once_count++;
}

static void
test_once_and_specifics(void)
{
	static dispatch_once_t predicate;
	dispatch_once_f(&predicate, NULL, count_once);
	dispatch_once_f(&predicate, NULL, count_once);
	test_long("dispatch_once_f runs its function once", once_count, 1);
	dispatch_queue_t queue = dispatch_queue_create("wasi.specifics",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_set_specific(queue, &specific_key, &specific_value,
			NULL);
	test_ptr_null("dispatch_get_specific off the queue returns NULL",
			dispatch_get_specific(&specific_key));
	__block void *value = NULL;
	dispatch_sync(queue, ^{
		value = dispatch_get_specific(&specific_key);
	});
	test_ptr("dispatch_get_specific on the queue returns its value", value,
			&specific_value);
	dispatch_release(queue);
}

// number of leading entries that hold submission numbers 1, 2, 3, ...
static long
in_order_count(const long *order, long count)
{
	long index = 0;
	while (index < count && order[index] == index + 1) {
		index++;
	}
	return index;
}

static void
test_fifo(void)
{
	// each item records its submission number (1 to 10) as it runs
	__block long count = 0;
	static long order[10];
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo",
			DISPATCH_QUEUE_SERIAL);
	for (long i = 1; i <= 10; i++) {
		dispatch_async(queue, ^{ order[count++] = i; });
	}
	test_long("no queued item runs before a pump", count, 0);
	dispatch_sync(queue, ^{ }); // contended: pumps the ten items first
	test_long("contended dispatch_sync runs the ten queued items first",
			count, 10);
	test_long("serial queue runs items in submission order",
			in_order_count(order, 10), 10);
	dispatch_release(queue);
}

static void
test_nested_fifo(void)
{
	// the outer item is submission 1 and records itself after it submits
	// the ten nested items, submissions 2 to 11
	__block long count = 0;
	static long order[11];
	dispatch_queue_t queue = dispatch_queue_create("wasi.fifo.nested",
			DISPATCH_QUEUE_SERIAL);
	dispatch_async(queue, ^{
		for (long i = 2; i <= 11; i++) {
			dispatch_async(queue, ^{ order[count++] = i; });
		}
		order[count++] = 1;
	});
	// the first contended sync is queued ahead of the ten nested items and
	// pumps only the outer block; the second pumps the ten
	dispatch_sync(queue, ^{ });
	test_long("first contended dispatch_sync runs only the outer item",
			count, 1);
	test_long("outer item runs first", order[0], 1);
	dispatch_sync(queue, ^{ });
	test_long("second contended dispatch_sync runs the ten nested items",
			count, 11);
	test_long("nested items run in submission order after the outer item",
			in_order_count(order, 11), 11);
	dispatch_release(queue);
}

static void
test_sync_and_barrier(void)
{
	__block long value = 0;
	dispatch_queue_t serial = dispatch_queue_create("wasi.sync",
			DISPATCH_QUEUE_SERIAL);
	dispatch_async(serial, ^{ value = 1; });
	dispatch_sync(serial, ^{
		test_long("dispatch_sync runs after the item queued ahead of "
				"it", value, 1);
		value = 2;
	});
	test_long("dispatch_sync runs its block before it returns", value, 2);
	dispatch_queue_t concurrent = dispatch_queue_create("wasi.barrier",
			DISPATCH_QUEUE_CONCURRENT);
	dispatch_barrier_sync(concurrent, ^{ value = 3; });
	test_long("dispatch_barrier_sync runs its block before it returns",
			value, 3);
	dispatch_release(concurrent);
	dispatch_release(serial);
}

static void
test_semaphore(void)
{
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	uint64_t start = wasi_test_now_ns();
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC));
	long elapsed_ms = (long)((wasi_test_now_ns() - start) / NSEC_PER_MSEC);
	test_long("timed semaphore wait with no signal times out", !result, 0);
	test_long_greater_than_or_equal("timed semaphore wait lasts at least "
			"its 100 ms timeout", elapsed_ms, 100);
	dispatch_semaphore_signal(semaphore);
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	test_long("dispatch_semaphore_wait(FOREVER) takes the signaled count",
			result, 0);
	dispatch_release(semaphore);
}

static void
test_group(void)
{
	__block long notified = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.group",
			DISPATCH_QUEUE_SERIAL);
	dispatch_group_t group = dispatch_group_create();
	dispatch_group_enter(group);
	dispatch_group_notify(group, queue, ^{ notified++; });
	dispatch_async(queue, ^{ dispatch_group_leave(group); });
	long result = dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	test_long("dispatch_group_wait(FOREVER) returns 0 once the group "
			"empties", result, 0);
	// the notify is queued on the serial queue; the wait does not run it
	dispatch_sync(queue, ^{ });
	test_long("group notify runs once the queue drains", notified, 1);
	dispatch_release(group);
	dispatch_release(queue);
}

static void
test_timers(void)
{
	__block long fires = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.timer",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
			queue, ^{
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
	test_long("dispatch_after signals before the 500 ms wait times out",
			result, 0);
	dispatch_source_t timer = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	dispatch_source_set_timer(timer,
			dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
			20 * NSEC_PER_MSEC, 0);
	dispatch_source_set_event_handler(timer, ^{
		if (++fires == 3) {
			dispatch_source_cancel(timer);
		}
	});
	dispatch_source_set_cancel_handler(timer, ^{
		dispatch_semaphore_signal(semaphore);
	});
	dispatch_resume(timer);
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	test_long("timer cancel handler signals the waiting semaphore",
			result, 0);
	test_long_greater_than_or_equal("repeating timer fires at least three "
			"times", fires, 3);
	dispatch_release(timer);
	dispatch_release(semaphore);
	dispatch_release(queue);
}

static void
test_apply(void)
{
	__block size_t sum = 0;
	dispatch_apply(8, dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(size_t index) {
		sum += index;
	});
	test_sizet("dispatch_apply indices sum to 0 + 1 + ... + 7",
			sum, 28);
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
	dispatch_data_t mapped = dispatch_data_create_map(joined, &bytes,
			&size);
	test_sizet("mapped concatenation has the size of both regions",
			size, 16);
	test_long("mapped concatenation holds both regions in order",
			memcmp(bytes, "01234567abcdefgh", 16), 0);
	dispatch_release(mapped);
	dispatch_release(joined);
	dispatch_release(two);
	dispatch_release(one);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI Smoke");

	test_once_and_specifics();
	test_fifo();
	test_nested_fifo();
	test_sync_and_barrier();
	test_semaphore();
	test_group();
	test_timers();
	test_apply();
	test_data();
	test_stop();
	return 0;
}
