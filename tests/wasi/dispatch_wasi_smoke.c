// WASI smoke test: dispatch_async + dispatch_group(+notify) + dispatch_after,
// driven by dispatch_main() on the global concurrent queue. Uses the _f API so
// the Blocks runtime is optional. Exits from the timer callback.
//
// Expected output:
//   smoke: start
//   [async] grouped work
//   [async] plain async
//   [group-notify] group complete
//   [after] 100ms timer -> exit
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>

static void work(void *c)      { printf("[async] %s\n", (const char *)c); fflush(stdout); }
static void notify_cb(void *c) { printf("[group-notify] %s\n", (const char *)c); fflush(stdout); }
static void after_cb(void *c)  { printf("[after] %s -> exit\n", (const char *)c); fflush(stdout); exit(0); }

int main(void) {
    printf("smoke: start\n"); fflush(stdout);
    dispatch_queue_t gq = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

    dispatch_group_t g = dispatch_group_create();
    dispatch_group_async_f(g, gq, (void *)"grouped work", work);
    dispatch_group_notify_f(g, gq, (void *)"group complete", notify_cb);
    dispatch_async_f(gq, (void *)"plain async", work);
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                     gq, (void *)"100ms timer", after_cb);

    dispatch_main();
    return 0;
}
