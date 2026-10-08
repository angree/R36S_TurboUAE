/* Puts the Workbench on the RTG (uaegfx / Picasso96) board, without anyone clicking through
 * ScreenMode prefs - the R36S has no keyboard and a stick for a mouse.
 *
 *   rtgmode [width height depth]        default 640 480 8 (the R36S panel, 256 colours)
 *
 * Finds the display mode whose name starts with "UAEGFX" (case-insensitive) and has exactly
 * that size and depth (nearest otherwise), and writes ENVARC:Sys/screenmode.prefs and
 * ENV:Sys/screenmode.prefs. IPrefs notices the ENV: copy and reopens the Workbench screen.
 * Does nothing when no UAEGFX mode exists (RTG board off), so it is safe at every boot.
 * Result and reason go to Work:rtgmode.txt. No sprintf (broken in this libc). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

static int prefix_ci(const char *s, const char *p)
{
    while (*p) { if (toupper((unsigned char)*s) != toupper((unsigned char)*p)) return 0; s++; p++; }
    return 1;
}

static void put32(unsigned char *b, ULONG v) { b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v; }
static void put16(unsigned char *b, UWORD v) { b[0] = v >> 8; b[1] = v; }

static int write_prefs(const char *path, ULONG id, UWORD w, UWORD h, UWORD d)
{
    /* FORM PREF { PRHD (6 bytes), SCRM (28 bytes) } */
    unsigned char f[62];
    BPTR fh;
    memset(f, 0, sizeof f);
    memcpy(f, "FORM", 4); put32(f + 4, 54);                                      /* 62 bytes in all */
    memcpy(f + 8, "PREF", 4);
    memcpy(f + 12, "PRHD", 4); put32(f + 16, 6);          /* version 0, type 0, flags 0 */
    memcpy(f + 26, "SCRM", 4); put32(f + 30, 28);
    /* 4 reserved ULONGs at f+34 */
    put32(f + 50, id); put16(f + 54, w); put16(f + 56, h); put16(f + 58, d);
    /* f+60: sm_Control = 0 (SCRM = 4 reserved ULONGs, DisplayID, Width, Height, Depth, Control) */
    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) return 0;
    Write(fh, f, 62);
    Close(fh);
    return 1;
}

int main(int argc, char **argv)
{
    UWORD want_w = 640, want_h = 480, want_d = 8;
    ULONG id = INVALID_ID, best = INVALID_ID;
    long best_err = 0x7fffffff;
    UWORD bw = 0, bh = 0, bd = 0;
    FILE *log = fopen("Work:rtgmode.txt", "w");

    if (argc >= 4) { want_w = atoi(argv[1]); want_h = atoi(argv[2]); want_d = atoi(argv[3]); }

    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct NameInfo name;
        struct DimensionInfo dims;
        DisplayInfoHandle h = FindDisplayInfo(id);
        long w, ht, d, err;
        if (!h) continue;
        if (!GetDisplayInfoData(h, (UBYTE *)&name, sizeof name, DTAG_NAME, 0)) name.Name[0] = 0;
        if (!GetDisplayInfoData(h, (UBYTE *)&dims, sizeof dims, DTAG_DIMS, 0)) continue;
        w = dims.Nominal.MaxX - dims.Nominal.MinX + 1;
        ht = dims.Nominal.MaxY - dims.Nominal.MinY + 1;
        d = dims.MaxDepth;
        if (log) fprintf(log, "mode %08lx '%s' %ldx%ldx%ld\n", (unsigned long)id, (char *)name.Name, w, ht, d);
        /* native chipset monitors are 0x0000xxxx-0x000Axxxx; everything above is a card.
           Picasso96's "P96Mode" entries are per-depth placeholders, not real modes. */
        if ((id >> 16) <= 0x000A || strstr((char *)name.Name, "P96Mode")) continue;
        err = labs(w - want_w) + labs(ht - want_h) + 100 * labs(d - want_d);
        if (err < best_err) { best_err = err; best = id; bw = w; bh = ht; bd = want_d <= d ? want_d : d; }
    }
    if (best == INVALID_ID) {
        if (log) { fprintf(log, "no UAEGFX mode - RTG board not enabled, nothing changed\n"); fclose(log); }
        return 5;
    }
    UnLock(CreateDir((STRPTR)"ENVARC:Sys"));
    UnLock(CreateDir((STRPTR)"ENV:Sys"));
    if (log) fprintf(log, "chosen %08lx %ux%ux%u\n", (unsigned long)best, bw, bh, bd);
    if (!write_prefs("ENVARC:Sys/screenmode.prefs", best, bw, bh, bd) && log) fprintf(log, "ENVARC write failed\n");
    if (!write_prefs("ENV:Sys/screenmode.prefs", best, bw, bh, bd) && log) fprintf(log, "ENV write failed\n");
    if (log) { fprintf(log, "done\n"); fclose(log); }
    return 0;
}
