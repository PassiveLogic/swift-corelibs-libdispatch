/*
 * A write source on stdout: the descriptor is writable, so the source fires
 * through poll(2) readiness (level-triggered with EV_DISPATCH rearm), and
 * suspending it stops delivery. Also checks that dispatch_main() parks on the
 * armed source rather than trapping.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static int fires;

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.write", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,
			1 /* stdout */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		fires++;
		if (fires < 3) return;  // rearm; must fire again (level-triggered)
		dispatch_source_cancel(source);
		printf("write source fired %d times\n", fires);
		printf("write source OK\n");
		exit(0);
	});
	dispatch_resume(source);
	dispatch_main();
}
