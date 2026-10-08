/* Presses a gadget in another program's window, from inside the Amiga.
 *
 *   sysclick <title part | CUSTOM> <gadget text> [x y]
 *
 * The R36S has no mouse and no keyboard, and neither stick-as-mouse route gave a pointer
 * inside the stock Amiberry. So SysInfo is driven the way the WinUAE tests drive a game:
 * from inside the guest. This waits for a window whose title (or whose screen's title)
 * contains the first argument, looks for a gadget whose text contains the second, moves the
 * pointer onto it and clicks, all through input.device. If no gadget carries that text, the
 * optional x y (pixels inside the window) are clicked instead.
 *
 * Title "CUSTOM" means the first screen that is not the Workbench screen: SysInfo 3.24 opens
 * an untitled screen of its own. That screen ends up BEHIND Workbench, because Work:run is
 * executed from User-Startup, before LoadWB - on the device this looked like "SysInfo opened
 * and quit". So the tool waits until Workbench is up (a speed test started during boot would
 * be measured against the boot), brings the target screen to the front, clicks, and keeps it
 * in front for a while.
 *
 * Everything it sees is written to Work:gadgets.txt, so a miss can be fixed from the log.
 * Integer only; no sprintf (broken in this libc). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <exec/types.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

static FILE *lg;

static int has(const char *hay, const char *needle)
{
    size_t n = strlen(needle), i, j;
    if (!hay) return 0;
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < n && hay[i + j] && toupper((unsigned char)hay[i + j]) == toupper((unsigned char)needle[j]); j++) ;
        if (j == n) return 1;
    }
    return 0;
}

static void send(struct IOStdReq *io, struct InputEvent *ev)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = (APTR)ev;
    io->io_Length = sizeof(struct InputEvent);
    DoIO((struct IORequest *)io);
}

int main(int argc, char **argv)
{
    struct MsgPort *port;
    struct IOStdReq *io;
    struct Screen *scr, *tscr = NULL;
    struct Window *win, *twin = NULL;
    struct Gadget *g;
    long gx = -1, gy = -1, fx = -1, fy = -1;
    int tries, custom, wb = 0;
    ULONG lock;

    if (argc < 3) { printf("usage: sysclick <title|CUSTOM> <gadget text> [x y]\n"); return 20; }
    if (argc >= 5) { fx = atol(argv[3]); fy = atol(argv[4]); }
    custom = strcmp(argv[1], "CUSTOM") == 0;
    lg = fopen("Work:gadgets.txt", "w");

    /* wait up to 60 s for the window */
    for (tries = 0; tries < 120 && !twin; tries++) {
        lock = LockIBase(0);
        for (scr = IntuitionBase->FirstScreen; scr && !twin; scr = scr->NextScreen) {
            if (custom && (scr->Flags & SCREENTYPE) == WBENCHSCREEN) continue;
            for (win = scr->FirstWindow; win; win = win->NextWindow)
                if (custom || has((const char *)win->Title, argv[1]) || has((const char *)scr->Title, argv[1])
                    || has((const char *)scr->DefaultTitle, argv[1])) { twin = win; tscr = scr; break; }
        }
        if (twin) {
            if (lg) fprintf(lg, "screen '%s' %ldx%ld  window '%s' at %ld,%ld size %ldx%ld\n",
                            tscr->Title ? (const char *)tscr->Title : "", (long)tscr->Width, (long)tscr->Height,
                            twin->Title ? (const char *)twin->Title : "", (long)twin->LeftEdge, (long)twin->TopEdge,
                            (long)twin->Width, (long)twin->Height);
            for (g = twin->FirstGadget; g; g = g->NextGadget) {
                const char *txt = "";
                if (!(g->Flags & GFLG_LABELMASK) && g->GadgetText && g->GadgetText->IText)
                    txt = (const char *)g->GadgetText->IText;
                if (lg) fprintf(lg, "gadget id=%ld at %ld,%ld size %ldx%ld type=%lx text='%s'\n", (long)g->GadgetID,
                                (long)g->LeftEdge, (long)g->TopEdge, (long)g->Width, (long)g->Height,
                                (unsigned long)g->GadgetType, txt);
                if (gx < 0 && has(txt, argv[2])) {
                    gx = twin->LeftEdge + g->LeftEdge + g->Width / 2;
                    gy = twin->TopEdge + g->TopEdge + g->Height / 2;
                }
            }
            if (gx < 0 && fx >= 0) { gx = twin->LeftEdge + fx; gy = twin->TopEdge + fy; }
        }
        UnlockIBase(lock);
        if (!twin) Delay(25);
    }
    if (!twin) { if (lg) { fprintf(lg, "no window matching '%s'\n", argv[1]); fclose(lg); } return 10; }
    if (gx < 0) { if (lg) { fprintf(lg, "no gadget with text '%s' and no fallback position\n", argv[2]); fclose(lg); } return 5; }
    if (lg) fprintf(lg, "will click at screen position %ld,%ld\n", gx, gy);

    /* wait (up to 60 s) for Workbench's own window, then let the boot settle */
    for (tries = 0; tries < 120 && !wb; tries++) {
        lock = LockIBase(0);
        for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen)
            for (win = scr->FirstWindow; win; win = win->NextWindow)
                if (win->Flags & WFLG_WBENCHWINDOW) wb = 1;
        UnlockIBase(lock);
        if (!wb) Delay(25);
    }
    if (lg) fprintf(lg, "workbench window %s after %ld polls\n", wb ? "seen" : "NOT seen", (long)tries);
    Delay(300);
    ScreenToFront(tscr);
    ActivateWindow(twin);
    Delay(50);

    port = CreateMsgPort();
    io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq)) : NULL;
    if (io && OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest *)io, 0) == 0) {
        struct InputEvent ev;
        struct IEPointerPixel pp;

        memset(&ev, 0, sizeof ev);
        pp.iepp_Screen = tscr;
        pp.iepp_Position.X = (WORD)gx;
        pp.iepp_Position.Y = (WORD)gy;
        ev.ie_Class = IECLASS_NEWPOINTERPOS;
        ev.ie_SubClass = IESUBCLASS_PIXEL;
        ev.ie_Code = IECODE_NOBUTTON;
        ev.ie_EventAddress = (APTR)&pp;
        send(io, &ev);
        Delay(10);

        memset(&ev, 0, sizeof ev);
        ev.ie_Class = IECLASS_RAWMOUSE;
        ev.ie_Code = IECODE_LBUTTON;
        ev.ie_Qualifier = IEQUALIFIER_LEFTBUTTON | IEQUALIFIER_RELATIVEMOUSE;
        send(io, &ev);
        Delay(8);
        memset(&ev, 0, sizeof ev);
        ev.ie_Class = IECLASS_RAWMOUSE;
        ev.ie_Code = IECODE_LBUTTON | IECODE_UP_PREFIX;
        ev.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
        send(io, &ev);

        CloseDevice((struct IORequest *)io);
        if (lg) fprintf(lg, "click sent\n");
    } else if (lg) fprintf(lg, "input.device did not open\n");
    if (lg) { fclose(lg); lg = NULL; }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);

    for (tries = 0; tries < 60; tries++) {          /* stay in front while the test runs */
        if (IntuitionBase->FirstScreen != tscr) ScreenToFront(tscr);
        Delay(50);
    }
    return 0;
}
