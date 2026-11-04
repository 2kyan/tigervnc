/* Copyright 2026 mobile_tigervncviewer contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

/*
 * A read-only XFree86-VidModeExtension for Xvnc.
 *
 * Mesa's glXGetMscRateOML() derives the refresh rate exclusively from
 * XF86VidModeGetModeLine(). Without this extension the call fails and
 * Chromium/Electron retry it on every frame, printing two lines to
 * stderr each time.
 *
 * Xvnc has no monitor, so the timings are synthesised: the visible size
 * is the real screen size, the rest is a CVT modeline at the nominal
 * FrameRate. Only that one mode exists. Requests that would change
 * anything (mode switches, viewport, gamma, mode lines) are refused,
 * except those that ask for what is already the case.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef XF86VIDMODE

#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xproto.h>

#include "misc.h"
#include "scrnintstr.h"
#include "vidmodestr.h"

#ifdef HAVE_LIBXCVT
#include <libxcvt/libxcvt.h>
#endif

#include "RFBGlue.h"
#include "vncVidMode.h"

#define VNC_DEFAULT_REFRESH 60

typedef struct {
    DisplayModeRec mode;
    int width, height, refresh;
} vncVidModeScreenRec;

static vncVidModeScreenRec vncVidModeScreens[MAXSCREENS];

static int
vncVidModeRefresh(void)
{
    char *str;
    int rate;

    /* Matches the server's frame clock (see VNCServerST) */
    str = vncGetParam("FrameRate");
    rate = str ? atoi(str) : 0;
    free(str);

    if (rate <= 0)
        rate = VNC_DEFAULT_REFRESH;

    return rate;
}

/* Rebuilds the screen's single mode if the size or rate has changed */
static DisplayModePtr
vncVidModeCurrent(ScreenPtr pScreen)
{
    vncVidModeScreenRec *s = &vncVidModeScreens[pScreen->myNum];
    DisplayModePtr mode = &s->mode;
    int width = pScreen->width;
    int height = pScreen->height;
    int refresh = vncVidModeRefresh();
#ifdef HAVE_LIBXCVT
    struct libxcvt_mode_info *cvt;
#endif

    if ((s->width == width) && (s->height == height) &&
        (s->refresh == refresh))
        return mode;

    memset(mode, 0, sizeof(*mode));

#ifdef HAVE_LIBXCVT
    cvt = libxcvt_gen_mode_info(width, height, refresh, false, false);
    if (cvt != NULL) {
        mode->Clock      = cvt->dot_clock;    /* kHz */
        mode->HSyncStart = cvt->hsync_start;
        mode->HSyncEnd   = cvt->hsync_end;
        mode->HTotal     = cvt->htotal;
        mode->VSyncStart = cvt->vsync_start;
        mode->VSyncEnd   = cvt->vsync_end;
        mode->VTotal     = cvt->vtotal;
        mode->Flags      = cvt->mode_flags;
        free(cvt);
    }
#endif

    /* No libxcvt (or it failed): no blanking, but the right rate */
    if (mode->HTotal < width || mode->VTotal < height) {
        mode->HSyncStart = mode->HSyncEnd = mode->HTotal = width;
        mode->VSyncStart = mode->VSyncEnd = mode->VTotal = height;
        mode->Clock = (int)(((CARD64) width * height * refresh + 999) / 1000);
        mode->Flags = 0;
    }

    /* libxcvt rounds the width up to a multiple of 8, as in RandR */
    mode->HDisplay = width;
    mode->VDisplay = height;
    mode->VScan = 0;
    mode->name = "current";
    mode->status = MODE_OK;

    s->width = width;
    s->height = height;
    s->refresh = refresh;

    return mode;
}

/*
 * Every timing must match, not just the size: ModModeLine validates the
 * client's timings with CheckModeFor*() and then writes them straight
 * into the mode that GetCurrentModeline() returned, i.e. our cache.
 */
static Bool
vncVidModeIsCurrent(ScreenPtr pScreen, DisplayModePtr mode)
{
    DisplayModePtr cur = vncVidModeCurrent(pScreen);

    return mode->Clock == cur->Clock &&
           mode->HDisplay == cur->HDisplay &&
           mode->HSyncStart == cur->HSyncStart &&
           mode->HSyncEnd == cur->HSyncEnd &&
           mode->HTotal == cur->HTotal &&
           mode->HSkew == cur->HSkew &&
           mode->VDisplay == cur->VDisplay &&
           mode->VSyncStart == cur->VSyncStart &&
           mode->VSyncEnd == cur->VSyncEnd &&
           mode->VTotal == cur->VTotal &&
           mode->Flags == cur->Flags;
}

static Bool
vncVidModeGetCurrentModeline(ScreenPtr pScreen, DisplayModePtr *mode,
                             int *dotClock)
{
    *mode = vncVidModeCurrent(pScreen);
    *dotClock = (*mode)->Clock;
    return TRUE;
}

static Bool
vncVidModeGetNextModeline(ScreenPtr pScreen, DisplayModePtr *mode,
                          int *dotClock)
{
    return FALSE;
}

static int
vncVidModeGetNumOfModes(ScreenPtr pScreen)
{
    return 1;
}

static vidMonitorValue
vncVidModeGetMonitorValue(ScreenPtr pScreen, int valtyp, int indx)
{
    vidMonitorValue ret = { NULL, };
    DisplayModePtr mode = vncVidModeCurrent(pScreen);

    switch (valtyp) {
    case VIDMODE_MON_VENDOR:
        ret.ptr = "TigerVNC";
        break;
    case VIDMODE_MON_MODEL:
        ret.ptr = "Xvnc";
        break;
    case VIDMODE_MON_NHSYNC:
    case VIDMODE_MON_NVREFRESH:
        ret.i = 1;
        break;
    /* The protocol carries these in units of 0.01 kHz / 0.01 Hz */
    case VIDMODE_MON_HSYNC_LO:
    case VIDMODE_MON_HSYNC_HI:
        ret.f = 100.0f * mode->Clock / mode->HTotal;
        break;
    case VIDMODE_MON_VREFRESH_LO:
    case VIDMODE_MON_VREFRESH_HI:
        ret.f = 100.0f * vncVidModeScreens[pScreen->myNum].refresh;
        break;
    }

    return ret;
}

static Bool
vncVidModeDeleteModeline(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Our only mode is the current one, which can't be deleted */
    return FALSE;
}

static Bool
vncVidModeAddModeline(ScreenPtr pScreen, DisplayModePtr mode)
{
    /*
     * The caller hands over ownership and ignores the result. Only an
     * exact copy of the current mode gets this far, so dropping it
     * leaves the mode list as the client expects.
     */
    free(mode);
    return FALSE;
}

static Bool
vncVidModeSwitchMode(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Switching to the mode we're already in is the only switch */
    return vncVidModeIsCurrent(pScreen, mode);
}

static ModeStatus
vncVidModeCheckMode(ScreenPtr pScreen, DisplayModePtr mode)
{
    return vncVidModeIsCurrent(pScreen, mode) ? MODE_OK : MODE_ONE_SIZE;
}

static void
vncVidModeSetCrtcForMode(ScreenPtr pScreen, DisplayModePtr mode)
{
}

static Bool
vncVidModeZoomViewport(ScreenPtr pScreen, int zoom)
{
    /* Only one mode, so cycling through them is a no-op */
    return TRUE;
}

static Bool
vncVidModeGetViewPort(ScreenPtr pScreen, int *x, int *y)
{
    *x = 0;
    *y = 0;
    return TRUE;
}

static Bool
vncVidModeSetViewPort(ScreenPtr pScreen, int x, int y)
{
    /* The viewport always covers the whole screen */
    return x == 0 && y == 0;
}

static Bool
vncVidModeLockZoom(ScreenPtr pScreen, Bool lock)
{
    return TRUE;
}

static int
vncVidModeGetNumOfClocks(ScreenPtr pScreen, Bool *progClock)
{
    /* "Programmable" means any clock is valid and none are listed */
    *progClock = TRUE;
    return 0;
}

static Bool
vncVidModeGetClocks(ScreenPtr pScreen, int *Clocks)
{
    return FALSE;
}

static int
vncVidModeGetDotClock(ScreenPtr pScreen, int Clock)
{
    return Clock;
}

static Bool
vncVidModeSetGamma(ScreenPtr pScreen, float red, float green, float blue)
{
    /* There's no colour lookup table, so only identity is possible */
    return red == 1.0f && green == 1.0f && blue == 1.0f;
}

static Bool
vncVidModeGetGamma(ScreenPtr pScreen, float *red, float *green, float *blue)
{
    *red = *green = *blue = 1.0f;
    return TRUE;
}

static Bool
vncVidModeGammaRamp(ScreenPtr pScreen, int size,
                    CARD16 *red, CARD16 *green, CARD16 *blue)
{
    return FALSE;
}

static int
vncVidModeGetGammaRampSize(ScreenPtr pScreen)
{
    return 0;
}

static Bool
vncVidModeScreenInit(ScreenPtr pScreen)
{
    VidModePtr pVidMode;

    if (pScreen->myNum >= MAXSCREENS)
        return FALSE;

    pVidMode = VidModeInit(pScreen);
    if (pVidMode == NULL)
        return FALSE;

    /* Force a rebuild for this server generation */
    memset(&vncVidModeScreens[pScreen->myNum], 0,
           sizeof(vncVidModeScreens[pScreen->myNum]));

    pVidMode->Flags = 0;
    pVidMode->Next = NULL;

    pVidMode->GetMonitorValue = vncVidModeGetMonitorValue;
    pVidMode->GetCurrentModeline = vncVidModeGetCurrentModeline;
    pVidMode->GetFirstModeline = vncVidModeGetCurrentModeline;
    pVidMode->GetNextModeline = vncVidModeGetNextModeline;
    pVidMode->DeleteModeline = vncVidModeDeleteModeline;
    pVidMode->ZoomViewport = vncVidModeZoomViewport;
    pVidMode->GetViewPort = vncVidModeGetViewPort;
    pVidMode->SetViewPort = vncVidModeSetViewPort;
    pVidMode->SwitchMode = vncVidModeSwitchMode;
    pVidMode->LockZoom = vncVidModeLockZoom;
    pVidMode->GetNumOfClocks = vncVidModeGetNumOfClocks;
    pVidMode->GetClocks = vncVidModeGetClocks;
    pVidMode->CheckModeForMonitor = vncVidModeCheckMode;
    pVidMode->CheckModeForDriver = vncVidModeCheckMode;
    pVidMode->SetCrtcForMode = vncVidModeSetCrtcForMode;
    pVidMode->AddModeline = vncVidModeAddModeline;
    pVidMode->GetDotClock = vncVidModeGetDotClock;
    pVidMode->GetNumOfModes = vncVidModeGetNumOfModes;
    pVidMode->SetGamma = vncVidModeSetGamma;
    pVidMode->GetGamma = vncVidModeGetGamma;
    pVidMode->SetGammaRamp = vncVidModeGammaRamp;
    pVidMode->GetGammaRamp = vncVidModeGammaRamp;
    pVidMode->GetGammaRampSize = vncVidModeGetGammaRampSize;

    pVidMode->First = vncVidModeCurrent(pScreen);

    return TRUE;
}

void
vncVidModeExtensionInit(void)
{
    Bool enabled = FALSE;
    int i;

    for (i = 0; i < screenInfo.numScreens; i++) {
        if (vncVidModeScreenInit(screenInfo.screens[i]))
            enabled = TRUE;
    }

    /* Mode switching is refused anyway, but keep the usual policy */
    if (enabled)
        VidModeAddExtension(FALSE);
}

#endif /* XF86VIDMODE */
