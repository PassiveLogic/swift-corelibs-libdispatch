/*
 * A read source on a file descriptor that is not open must fail loudly at
 * registration (the fstat guardrail in the WASI event backend), never
 * register silently and hang.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.badfd", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			999, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{ puts("unexpected handler"); });
	dispatch_resume(source);
	puts("invalid-fd source did not crash");
	return 0;
}
