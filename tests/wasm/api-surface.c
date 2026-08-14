/*
 * Broad Dispatch API surface smoke for single-threaded WASI: exercises the
 * object, queue-attribute, block, data, group, and source families end to
 * end in one binary. Derived from the probe program used to compare the two
 * WASI port candidates (PRs #1 and #2); every check here passed on the
 * cooperative eager-drain implementation this branch adopts.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, name) do { \
		if (!(cond)) { printf("FAIL: %s\n", name); exit(1); } \
	} while (0)

static dispatch_once_t once_tok;
static int once_n = 0;
static void once_fn(void *ctx) { (void)ctx; once_n++; }

static int g_finalized = 0;
static void finalizer(void *ctx) { (void)ctx; g_finalized++; }

static int skey;
static int sval = 42;

int main(void)
{
	dispatch_once_f(&once_tok, NULL, once_fn);
	dispatch_once_f(&once_tok, NULL, once_fn);
	CHECK(once_n == 1, "dispatch_once");
	CHECK(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC) != 0, "dispatch_time");
	CHECK(dispatch_walltime(NULL, 0) != 0, "dispatch_walltime");

	dispatch_queue_t serial = dispatch_queue_create("v2.api.serial",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t conc = dispatch_queue_create("v2.api.conc",
			DISPATCH_QUEUE_CONCURRENT);
	CHECK(!strcmp(dispatch_queue_get_label(serial), "v2.api.serial"),
			"dispatch_queue_get_label");
	dispatch_queue_t targeted = dispatch_queue_create_with_target(
			"v2.api.targeted", DISPATCH_QUEUE_SERIAL, serial);
	(void)targeted;
	dispatch_set_target_queue(conc,
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));

	dispatch_queue_set_specific(serial, &skey, &sval, NULL);
	CHECK(dispatch_queue_get_specific(serial, &skey) == &sval,
			"dispatch_queue_get_specific");
	__block int gotspec = 0;
	dispatch_sync(serial, ^{
		if (dispatch_get_specific(&skey) == &sval) gotspec = 1;
		dispatch_assert_queue(serial);
		dispatch_assert_queue_not(conc);
	});
	CHECK(gotspec, "dispatch_get_specific");

	__block int barrier_ran = 0;
	dispatch_barrier_sync(conc, ^{ barrier_ran = 1; });
	CHECK(barrier_ran, "dispatch_barrier_sync");

	__block int applied = 0;
	dispatch_apply(5, conc, ^(size_t i) { (void)i; applied++; });
	CHECK(applied == 5, "dispatch_apply");

	const char *msg = "hello dispatch";
	dispatch_data_t d1 = dispatch_data_create(msg, 5, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t d2 = dispatch_data_create(msg + 5, strlen(msg) - 5, NULL,
			DISPATCH_DATA_DESTRUCTOR_DEFAULT);
	dispatch_data_t cat = dispatch_data_create_concat(d1, d2);
	CHECK(dispatch_data_get_size(cat) == strlen(msg), "dispatch_data_concat");
	dispatch_data_t sub = dispatch_data_create_subrange(cat, 6, 8);
	const void *buf; size_t sz;
	dispatch_data_t map = dispatch_data_create_map(sub, &buf, &sz);
	(void)map;
	CHECK(sz == 8 && !memcmp(buf, "dispatch", 8), "dispatch_data_subrange/map");
	__block size_t total = 0;
	dispatch_data_apply(cat,
			^bool(dispatch_data_t r, size_t off, const void *loc, size_t s) {
		(void)r; (void)off; (void)loc;
		total += s;
		return true;
	});
	CHECK(total == strlen(msg), "dispatch_data_apply");
	size_t roff;
	dispatch_data_t reg = dispatch_data_copy_region(cat, 6, &roff);
	(void)reg;

	__block int block_n = 0;
	dispatch_block_t blk = dispatch_block_create(0, ^{ block_n++; });
	dispatch_block_perform(0, ^{ block_n += 10; });
	CHECK(block_n == 10, "dispatch_block_perform");
	CHECK(dispatch_block_testcancel(blk) == 0, "dispatch_block_testcancel");
	dispatch_block_cancel(blk);
	CHECK(dispatch_block_testcancel(blk) != 0, "dispatch_block_cancel");

	dispatch_semaphore_t sem = dispatch_semaphore_create(1);
	CHECK(dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0,
			"dispatch_semaphore fast path");
	dispatch_semaphore_signal(sem);

	dispatch_queue_t fq = dispatch_queue_create("v2.api.final",
			DISPATCH_QUEUE_SERIAL);
	dispatch_set_context(fq, (void *)"ctx");
	CHECK(!strcmp((char *)dispatch_get_context(fq), "ctx"),
			"dispatch_set/get_context");
	dispatch_set_finalizer_f(fq, finalizer);
	dispatch_release(fq);

	__block int inactive_ran = 0;
	dispatch_queue_t inact = dispatch_queue_create("v2.api.inactive",
			dispatch_queue_attr_make_initially_inactive(DISPATCH_QUEUE_SERIAL));
	dispatch_async(inact, ^{ inactive_ran = 1; });
	dispatch_activate(inact);

	__block int resumed_ran = 0;
	dispatch_suspend(serial);
	dispatch_async(serial, ^{ resumed_ran = 1; });
	dispatch_resume(serial);

	dispatch_group_t g = dispatch_group_create();
	__block int gn = 0;
	dispatch_group_async(g, conc, ^{ gn++; });
	dispatch_group_enter(g);
	dispatch_async(conc, ^{ gn++; dispatch_group_leave(g); });
	__block int source_events = 0;
	__block int source_cancelled = 0;
	__block int source_registered = 0;
	dispatch_group_notify(g, serial, ^{
		CHECK(gn == 2, "dispatch_group_notify");
		dispatch_source_t src = dispatch_source_create(
				DISPATCH_SOURCE_TYPE_DATA_ADD, 0, 0, serial);
		dispatch_source_set_registration_handler(src, ^{
			source_registered = 1;
		});
		dispatch_source_set_event_handler(src, ^{
			CHECK(dispatch_source_get_data(src) == 7,
					"dispatch_source_get_data");
			source_events++;
			dispatch_source_set_cancel_handler(src, ^{
				source_cancelled = 1;
				__block dispatch_source_t tim;
				tim = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
						0, 0, serial);
				dispatch_source_set_timer(tim,
						dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
						DISPATCH_TIME_FOREVER, 0);
				dispatch_source_set_event_handler(tim, ^{
					dispatch_source_cancel(tim);
					dispatch_after(dispatch_walltime(NULL,
							30 * NSEC_PER_MSEC), serial, ^{
						CHECK(inactive_ran, "initially_inactive+activate");
						CHECK(resumed_ran, "dispatch_suspend/resume");
						CHECK(g_finalized == 1, "dispatch_set_finalizer_f");
						CHECK(source_registered, "source registration handler");
						CHECK(source_events == 1, "user-data source event");
						CHECK(source_cancelled, "source cancel handler");
						printf("api surface OK\n");
						exit(0);
					});
				});
				dispatch_resume(tim);
			});
			dispatch_source_cancel(src);
		});
		dispatch_resume(src);
		dispatch_source_merge_data(src, 7);
	});
	dispatch_main();
}
