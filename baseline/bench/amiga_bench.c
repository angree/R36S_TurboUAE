/* R36S emulator speed test, Amiga side. Runs Dhrystone 2.1 for about 8 seconds, twice, and
 * appends the result to Work:result.txt (with the argument QUIT it then ends the emulator). No keyboard, mouse
 * or screen is needed, so a test is: start the port, wait, pull the card.
 *
 * Integer only (the emulated machine has no FPU) and no sprintf (broken in this libc).
 * Time is DateStamp(), 1/50 s. The launcher on the host stamps the wall clock when the
 * START and RESULT lines appear, which catches an emulated clock that runs fast or slow. */
#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
#include <dos/dos.h>

extern int dhry_run(int runs);

static long ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Days % 100) * 4320000L + d.ds_Minute * 3000L + d.ds_Tick;
}

static void out(const char *line, long a, long b, long c)
{
    FILE *f = fopen("Work:result.txt", "a");
    if (!f) return;
    fprintf(f, line, a, b, c);
    fclose(f);
}

int main(int argc, char **argv)
{
    long runs = 2000, t, dt = 0;
    long pass;
    int ok = 1;

    out("BOOTED tick=%ld\n", ticks(), 0, 0);

    /* calibrate: double the run count until it takes at least half a second */
    for (;;) {
        t = ticks();
        ok &= dhry_run((int)runs);
        dt = ticks() - t;
        if (dt >= 25 || runs >= 100000000L) break;
        runs *= 2;
    }
    out("CALIB runs=%ld ticks=%ld ok=%ld\n", runs, dt, (long)ok);
    if (dt < 1) dt = 1;
    runs = (runs / dt) * 400;              /* about 8 seconds */

    for (pass = 1; pass <= 2; pass++) {
        long per_s, mips100;
        out("START pass=%ld runs=%ld\n", pass, runs, 0);
        t = ticks();
        ok = dhry_run((int)runs);
        dt = ticks() - t;
        if (dt < 1) dt = 1;
        per_s = (runs / dt) * 50 + ((runs % dt) * 50) / dt;
        mips100 = (per_s / 1757) * 100 + ((per_s % 1757) * 100) / 1757;
        out("RESULT pass=%ld runs=%ld ticks=%ld", pass, runs, dt);
        out(" dhrystones_per_s=%ld vax_mips_x100=%ld ok=%ld\n", per_s, mips100, (long)ok);
    }
    out("DONE\n", 0, 0, 0);

    /* "dhrybench QUIT" also ends the emulator: uaelib function 13 = ExitEmu */
    if (argc > 1 && strcmp(argv[1], "QUIT") == 0)
        ((unsigned long (*)(unsigned long))0xF0FF60)(13);
    return 0;
}
