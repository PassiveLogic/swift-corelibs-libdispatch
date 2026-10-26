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
 * Broad Dispatch API surface smoke for single-threaded WASI: exercises the
 * object, queue-attribute, block, data, group, and source families end to
 * end in one binary. The synchronous calls are checked in main; the
 * deferred work runs under dispatch_main() as a chain (group notify, then a
 * user-data source, then a timer, then a walltime dispatch_after), and the
 * last link checks the deferred results and stops the test.
 */
#include <dispatch/dispatch.h>
#include <Block.h>
#include <string.h>

#include <bsdtests.h>
#include "dispatch_test.h"

static dispatch_once_t once_predicate;
static long once_count;
static int specific_key;
static int specific_value = 42;

// results of the deferred work, checked by the last link of the chain
static long finalizer_count;
static long inactive_ran;
static long resumed_ran;
static long source_registered;
static long source_events;
static long source_cancelled;

static void
count_once(void *context)
{
	(void)context;
	once_count++;
}

static void
finalizer(void *context)
{
	(void)context;
	finalizer_count++;
}

static void
test_deferred_results(void)
{
	test_long("initially inactive queue runs its item after activation",
			inactive_ran, 1);
	test_long("suspended queue runs its item after resume",
			resumed_ran, 1);
	test_long("finalizer runs once after the queue release",
			finalizer_count, 1);
	test_long("source registration handler runs", source_registered, 1);
	test_long("user-data source delivers one event", source_events, 1);
	test_long("source cancel handler runs", source_cancelled, 1);
}

int
main(void)
{
	dispatch_test_start("Dispatch WASI API Surface");

	dispatch_once_f(&once_predicate, NULL, count_once);
	dispatch_once_f(&once_predicate, NULL, count_once);
	test_long("dispatch_once_f runs its function once", once_count, 1);
	test_long("dispatch_time returns a non-zero time",
			!dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC), 0);
	test_long("dispatch_walltime returns a non-zero time",
			!dispatch_walltime(NULL, 0), 0);

	dispatch_queue_t serial = dispatch_queue_create("wasi.api.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t concurrent = dispatch_queue_create("wasi.api.conc",
			DISPATCH_QUEUE_CONCURRENT);
	test_long("dispatch_queue_get_label returns the queue label",
			strcmp(dispatch_queue_get_label(serial),
			"wasi.api.serial"), 0);
	dispatch_queue_t targeted = dispatch_queue_create_with_target(
			"wasi.api.targeted", DISPATCH_QUEUE_SERIAL, serial);
	dispatch_set_target_queue(concurrent, dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));

	dispatch_queue_set_specific(serial, &specific_key, &specific_value,
			NULL);
	test_ptr("dispatch_queue_get_specific returns the value set",
			dispatch_queue_get_specific(serial, &specific_key),
			&specific_value);
	__block void *specific = NULL;
	dispatch_sync(serial, ^{
		specific = dispatch_get_specific(&specific_key);
		dispatch_assert_queue(serial);
		dispatch_assert_queue_not(concurrent);
	});
	test_ptr("dispatch_get_specific on the queue returns its value",
			specific, &specific_value);

	__block long barrier_ran = 0;
	dispatch_barrier_sync(concurrent, ^{ barrier_ran = 1; });
	test_long("dispatch_barrier_sync runs its block", barrier_ran, 1);

	__block long applied = 0;
	dispatch_apply(5, concurrent, ^(size_t index) {
		(void)index;
		applied++;
	});
	test_long("dispatch_apply runs every iteration", applied, 5);

	const char *message = "hello dispatch";
	dispatch_data_t head = dispatch_data_create(message, 5, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t tail = dispatch_data_create(message + 5,
			strlen(message) - 5, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t joined = dispatch_data_create_concat(head, tail);
	test_sizet("dispatch_data_create_concat joins both regions",
			dispatch_data_get_size(joined), strlen(message));
	dispatch_data_t subrange = dispatch_data_create_subrange(joined, 6, 8);
	const void *map_bytes;
	size_t map_size;
	dispatch_data_t map = dispatch_data_create_map(subrange, &map_bytes,
			&map_size);
	test_sizet("mapped subrange has the subrange size", map_size, 8);
	test_long("mapped subrange holds the subrange bytes",
			memcmp(map_bytes, "dispatch", 8), 0);
	__block size_t applied_size = 0;
	dispatch_data_apply(joined, ^bool(dispatch_data_t region,
			size_t offset, const void *buffer, size_t size) {
		(void)region;
		(void)offset;
		(void)buffer;
		applied_size += size;
		return true;
	});
	test_sizet("dispatch_data_apply visits every byte", applied_size,
			strlen(message));
	size_t region_offset;
	dispatch_data_t region = dispatch_data_copy_region(joined, 6,
			&region_offset);
	test_sizet("dispatch_data_copy_region returns the tail region offset",
			region_offset, 5);
	test_sizet("dispatch_data_copy_region returns the tail region",
			dispatch_data_get_size(region), strlen(message) - 5);
	dispatch_release(region);
	dispatch_release(map);
	dispatch_release(subrange);
	dispatch_release(joined);
	dispatch_release(tail);
	dispatch_release(head);

	__block long block_count = 0;
	dispatch_block_t block = dispatch_block_create(0, ^{ block_count++; });
	dispatch_block_perform(0, ^{ block_count += 10; });
	test_long("dispatch_block_perform runs its block inline",
			block_count, 10);
	test_long("a new block is not cancelled",
			dispatch_block_testcancel(block), 0);
	dispatch_block_cancel(block);
	test_long("dispatch_block_cancel marks the block cancelled",
			!dispatch_block_testcancel(block), 0);
	Block_release(block);

	dispatch_semaphore_t semaphore = dispatch_semaphore_create(1);
	long result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW);
	test_long("dispatch_semaphore_wait(NOW) takes an available count",
			result, 0);
	dispatch_semaphore_signal(semaphore);
	dispatch_release(semaphore);

	dispatch_queue_t finalized = dispatch_queue_create("wasi.api.final",
			DISPATCH_QUEUE_SERIAL);
	dispatch_set_context(finalized, (void *)"ctx");
	test_long("dispatch_get_context returns the context set",
			strcmp(dispatch_get_context(finalized), "ctx"), 0);
	dispatch_set_finalizer_f(finalized, finalizer);
	dispatch_release(finalized);

	dispatch_queue_t inactive = dispatch_queue_create("wasi.api.inactive",
			dispatch_queue_attr_make_initially_inactive(
			DISPATCH_QUEUE_SERIAL));
	dispatch_async(inactive, ^{ inactive_ran = 1; });
	dispatch_activate(inactive);

	dispatch_suspend(serial);
	dispatch_async(serial, ^{ resumed_ran = 1; });
	dispatch_resume(serial);

	dispatch_group_t group = dispatch_group_create();
	__block long group_count = 0;
	dispatch_group_async(group, concurrent, ^{ group_count++; });
	dispatch_group_enter(group);
	dispatch_async(concurrent, ^{
		group_count++;
		dispatch_group_leave(group);
	});
	dispatch_group_notify(group, serial, ^{
		test_long("group notify runs after both group items",
				group_count, 2);
		dispatch_source_t source = dispatch_source_create(
				DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, serial);
		dispatch_source_set_registration_handler(source, ^{
			source_registered = 1;
		});
		dispatch_source_set_event_handler(source, ^{
			test_sizet("user-data source delivers the merged value",
					dispatch_source_get_data(source), 7);
			source_events++;
			dispatch_source_set_cancel_handler(source, ^{
				source_cancelled = 1;
				dispatch_source_t timer;
				timer = dispatch_source_create(
						DISPATCH_SOURCE_TYPE_TIMER,
						0, 0, serial);
				dispatch_time_t start;
				start = dispatch_time(DISPATCH_TIME_NOW,
						20 * NSEC_PER_MSEC);
				dispatch_source_set_timer(timer, start,
						DISPATCH_TIME_FOREVER, 0);
				dispatch_source_set_event_handler(timer, ^{
					dispatch_source_cancel(timer);
					dispatch_time_t deadline;
					deadline = dispatch_walltime(NULL,
							30 * NSEC_PER_MSEC);
					dispatch_after(deadline, serial, ^{
						test_deferred_results();
						dispatch_release(timer);
						dispatch_release(source);
						dispatch_release(group);
						dispatch_release(inactive);
						dispatch_release(targeted);
						dispatch_release(concurrent);
						dispatch_release(serial);
						test_stop();
					});
				});
				dispatch_resume(timer);
			});
			dispatch_source_cancel(source);
		});
		dispatch_resume(source);
		dispatch_source_merge_data(source, 7);
	});
	dispatch_main();
}
