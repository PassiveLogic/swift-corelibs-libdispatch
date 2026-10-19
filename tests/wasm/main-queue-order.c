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
 * The thread-bound main queue drains in strict serial FIFO order. Global
 * queue work and a main-queue timer run between the main-queue items.
 */
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// the number of main-queue items
#define MAIN_STEPS 5
// the delay of the timer that checks the result, after all items ran
#define FINISH_DELAY (30 * NSEC_PER_MSEC)

static int g_step = 0;
static int g_violations = 0;

static void main_step(void *c)
{
	int n = (int)(intptr_t)c;
	if (n != g_step) g_violations++;
	printf("main step %d (expected %d)%s\n", n, g_step,
			n == g_step ? "" : "  <-- ORDER VIOLATION");
	g_step++;
}

static void global_work(void *c) { (void)c; printf("global work ran\n"); }

static void finish(void *c)
{
	(void)c;
	if (g_step == MAIN_STEPS && !g_violations) {
		printf("main queue order OK\n");
		exit(0);
	}
	printf("FAIL: steps=%d violations=%d\n", g_step, g_violations);
	exit(1);
}

int main(void)
{
	dispatch_queue_t mq = dispatch_get_main_queue();
	dispatch_queue_t gq = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	for (intptr_t i = 0; i < MAIN_STEPS; i++) {
		dispatch_async_f(mq, (void *)i, main_step);
	}
	dispatch_async_f(gq, NULL, global_work);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, FINISH_DELAY),
			mq, NULL, finish);
	dispatch_main();
}
