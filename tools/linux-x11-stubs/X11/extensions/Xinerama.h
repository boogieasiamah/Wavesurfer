/* Minimal libXinerama declarations for building JUCE on headless Linux CI. */
#ifndef _Xinerama_h
#define _Xinerama_h
#include <X11/Xlib.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int screen_number; short x_org, y_org, width, height; } XineramaScreenInfo;
Bool XineramaQueryExtension (Display*, int*, int*);
Status XineramaQueryVersion (Display*, int*, int*);
Bool XineramaIsActive (Display*);
XineramaScreenInfo* XineramaQueryScreens (Display*, int*);
#ifdef __cplusplus
}
#endif
#endif
