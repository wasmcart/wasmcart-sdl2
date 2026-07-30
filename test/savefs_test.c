/*
 * savefs_test.c — does wc_sdl_savefs.h actually persist saves?
 *
 * The check that matters is "files survive a host reload": it copies the region
 * out, zeroes it, copies it back and re-inits, which is exactly what happens
 * between two sessions of a game. Without that, every other assertion here would
 * pass on a layer that only ever worked in memory.
 *
 * Also covers rewriting a file with a smaller size, since that shifts every file
 * after it in the region and is the obvious way to corrupt a neighbour.
 *
 * Build and run (plain host compiler, no emscripten needed):
 *   gcc -I include -o savefs_test test/savefs_test.c && ./savefs_test
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "wc_sdl_savefs.h"

static uint8_t region[WC_SAVEFS_BYTES];
static int fails = 0;
static void ck(const char *what, int ok) {
    printf(ok ? "  ok    %s\n" : "*** FAIL %s\n", what); if (!ok) fails++;
}

int main(void) {
    /* first run: region is all zeroes, like a host that has never saved */
    wc_savefs_init(region, sizeof region);
    ck("no files on a virgin region", !wc_savefs_exists("game.cfg"));

    FILE *f = wc_savefs_fopen("game.cfg", "wb");
    ck("open for write", f != NULL);
    const char *cfg = "volume=7\nfullscreen=1\n";
    fwrite(cfg, 1, strlen(cfg), f);
    wc_savefs_fclose(f);
    ck("exists after write", wc_savefs_exists("game.cfg"));

    /* a second file, to check the table and offsets */
    f = wc_savefs_fopen("progress.sav", "wb");
    uint8_t prog[300]; for (int i=0;i<300;i++) prog[i]=(uint8_t)i;
    fwrite(prog, 1, sizeof prog, f);
    wc_savefs_fclose(f);

    /* read both back */
    char buf[128] = {0};
    f = wc_savefs_fopen("game.cfg", "rb");
    ck("open for read", f != NULL);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    ck("config round-trips exactly", n == strlen(cfg) && memcmp(buf, cfg, n) == 0);

    uint8_t rb[300] = {0};
    f = wc_savefs_fopen("progress.sav", "rb");
    n = fread(rb, 1, sizeof rb, f);
    fclose(f);
    ck("binary save round-trips", n == 300 && memcmp(rb, prog, 300) == 0);

    /* THE test that matters: simulate the host persisting the region and a
       later session loading it back. Nothing else proves saves survive. */
    uint8_t persisted[WC_SAVEFS_BYTES];
    memcpy(persisted, region, sizeof region);
    memset(region, 0, sizeof region);          /* new process, cold memory */
    memcpy(region, persisted, sizeof region);  /* host restores the blob */
    wc_savefs_init(region, sizeof region);

    ck("files survive a host reload", wc_savefs_exists("game.cfg") &&
                                      wc_savefs_exists("progress.sav"));
    memset(buf, 0, sizeof buf);
    f = wc_savefs_fopen("game.cfg", "rb");
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    ck("config content survives", n == strlen(cfg) && memcmp(buf, cfg, n) == 0);

    /* rewriting a file with a DIFFERENT size must not corrupt the one after it */
    f = wc_savefs_fopen("game.cfg", "wb");
    const char *cfg2 = "v=1\n";
    fwrite(cfg2, 1, strlen(cfg2), f);
    wc_savefs_fclose(f);
    memset(rb, 0, sizeof rb);
    f = wc_savefs_fopen("progress.sav", "rb");
    n = fread(rb, 1, sizeof rb, f);
    fclose(f);
    ck("shrinking one file preserves the next", n == 300 && memcmp(rb, prog, 300) == 0);

    ck("unknown file returns NULL", wc_savefs_fopen("nope.dat", "rb") == NULL);

    printf(fails ? "\nFAILED (%d)\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
