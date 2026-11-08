/* Headless build: no window. Keys come from SSO_KEYS only. */
#include "frontend.h"
int  fe_available(void) { return 0; }
#include <stdio.h>
int  fe_run(void (*start_guest)(void), volatile int *guest_done)
{
    (void)guest_done;
    fprintf(stderr, "headless build (no SDL2): no window; input only from SSO_KEYS/SSO_KEYSEQ, stops after SSO_SECONDS (default 20).\n"
                    "Install the SDL2 dev package and rebuild for the playable version.\n");
    start_guest(); return 0;
}
void fe_submit_frame(const uint8_t *rgb, int w, int h) { (void)rgb; (void)w; (void)h; }
int  fe_pop_key(void) { return -1; }
int  fe_quit_requested(void) { return 0; }
void fe_push_key(int k) { (void)k; }
int  fe_pop_raw(void) { return -1; }
void fe_push_raw(int c) { (void)c; }
