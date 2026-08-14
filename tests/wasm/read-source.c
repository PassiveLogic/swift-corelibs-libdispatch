/*
 * A read source on stdin. The runner writes the payload only after a delay,
 * so passing requires dispatch_main() to genuinely park in the host poll
 * until the descriptor becomes readable — not to find data already buffered.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	dispatch_queue_t queue = dispatch_queue_create("wasi.read", NULL);
	dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			0 /* stdin */, 0, queue);
	if (!source) return 1;
	dispatch_source_set_event_handler(source, ^{
		char buf[64] = { 0 };
		ssize_t n = read(0, buf, sizeof(buf) - 1);
		if (n > 0 && strncmp(buf, "ping", 4) == 0) {
			printf("read source OK\n");
			exit(0);
		}
		printf("FAIL: read %zd bytes\n", n);
		exit(1);
	});
	dispatch_resume(source);
	printf("read source armed\n");
	fflush(stdout);
	dispatch_main();
}
