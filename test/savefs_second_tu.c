/*
 * A second translation unit that includes wc_sdl_savefs.h WITHOUT defining
 * WC_SAVEFS_IMPLEMENTATION -- exactly how a real port includes it from more than
 * one file (OpenTyrian does: its cart entry points and its file shim both need it).
 *
 * This file exists because that case was broken. When the state was plain
 * `static` in the header, each including file got its own private filesystem:
 * whichever one called wc_savefs_init() worked, and the other was left with
 * cap == 0, where fopen SUCCEEDS but every fwrite short-writes. Saves vanished
 * while the game printed "failed to write".
 *
 * If the header ever regresses to per-file state, this writes into the wrong copy
 * and the check in savefs_test.c fails.
 */
#include "wc_sdl_savefs.h"

int savefs_other_tu_write(const char *name, const void *data, unsigned len)
{
	FILE *f = wc_savefs_fopen(name, "wb");
	if (f == NULL)
		return -1;
	const unsigned n = (unsigned)fwrite(data, 1, len, f);
	wc_savefs_fclose(f);
	return n == len ? 0 : -2;
}

int savefs_other_tu_exists(const char *name)
{
	return wc_savefs_exists(name);
}
