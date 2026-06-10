#include <dispatch/dispatch.h>
#include <stdio.h>
static int g_n=0; static void inc(void*c){(void)c; g_n++;}
int main(void){
  printf("sync: start\n");
  dispatch_sync_f(dispatch_get_global_queue(0,0), NULL, inc);
  printf("sync on global ok, n=%d\n", g_n);
  dispatch_queue_t s = dispatch_queue_create("s", DISPATCH_QUEUE_SERIAL);
  dispatch_sync_f(s, NULL, inc);
  printf("sync on serial ok, n=%d\n", g_n);
  printf("SYNC OK n=%d\n", g_n);
  return 0;
}
