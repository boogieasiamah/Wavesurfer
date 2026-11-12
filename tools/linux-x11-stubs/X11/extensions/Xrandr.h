/* Minimal libXrandr declarations (ABI-compatible subset) for building JUCE on headless Linux CI. */
#ifndef _XRANDR_H_
#define _XRANDR_H_
#include <X11/extensions/randr.h>
#include <X11/Xlib.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef XID RROutput; typedef XID RRCrtc; typedef XID RRMode; typedef XID RRProvider;
typedef unsigned long XRRModeFlags;
typedef struct _XRRModeInfo {
    RRMode id; unsigned int width, height; unsigned long dotClock;
    unsigned int hSyncStart, hSyncEnd, hTotal, hSkew, vSyncStart, vSyncEnd, vTotal;
    char* name; unsigned int nameLength; XRRModeFlags modeFlags;
} XRRModeInfo;
typedef struct _XRRScreenResources {
    Time timestamp, configTimestamp; int ncrtc; RRCrtc* crtcs; int noutput; RROutput* outputs; int nmode; XRRModeInfo* modes;
} XRRScreenResources;
typedef struct _XRROutputInfo {
    Time timestamp; RRCrtc crtc; char* name; int nameLen; unsigned long mm_width, mm_height;
    Connection connection; SubpixelOrder subpixel_order; int ncrtc; RRCrtc* crtcs; int nclone; RROutput* clones;
    int nmode, npreferred; RRMode* modes;
} XRROutputInfo;
typedef struct _XRRCrtcInfo {
    Time timestamp; int x, y; unsigned int width, height; RRMode mode; Rotation rotation; int noutput; RROutput* outputs;
    Rotation rotations; int npossible; RROutput* possible;
} XRRCrtcInfo;
Bool XRRQueryExtension (Display*, int*, int*);
Status XRRQueryVersion (Display*, int*, int*);
XRRScreenResources* XRRGetScreenResources (Display*, Window);
void XRRFreeScreenResources (XRRScreenResources*);
XRROutputInfo* XRRGetOutputInfo (Display*, XRRScreenResources*, RROutput);
void XRRFreeOutputInfo (XRROutputInfo*);
XRRCrtcInfo* XRRGetCrtcInfo (Display*, XRRScreenResources*, RRCrtc);
void XRRFreeCrtcInfo (XRRCrtcInfo*);
RROutput XRRGetOutputPrimary (Display*, Window);
void XRRSelectInput (Display*, Window, int);
#ifdef __cplusplus
}
#endif
#endif
