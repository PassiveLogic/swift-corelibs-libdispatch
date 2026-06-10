// WASI test: the (thread-bound) main queue drains under dispatch_main() with
// strict serial FIFO ordering, interleaved with global-queue work, plus a
// main-queue timer. Exits 0 only if all five main-queue items ran in order.
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int g_step = 0;

static void main_step(void *c) {
    int n = (int)(intptr_t)c;
    printf("main step %d (expected %d)%s\n", n, g_step,
           n == g_step ? "" : "  <-- ORDER VIOLATION");
    fflush(stdout);
    g_step++;
}
static void global_work(void *c) { (void)c; printf("global work ran\n"); fflush(stdout); }
static void finish(void *c) {
    (void)c;
    printf("finish: main steps run = %d (expected 5)\n", g_step);
    fflush(stdout);
    exit(g_step == 5 ? 0 : 1);
}

int main(void) {
    dispatch_queue_t mq = dispatch_get_main_queue();
    dispatch_queue_t gq = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    for (intptr_t i = 0; i < 5; i++) {
        dispatch_async_f(mq, (void *)i, main_step);
    }
    dispatch_async_f(gq, NULL, global_work);
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC), mq, NULL, finish);
    dispatch_main();
    return 0;
}
