/* Minimal libXcomposite declarations for building JUCE on headless Linux CI. */
#ifndef _XCOMPOSITE_H_
#define _XCOMPOSITE_H_
#include <X11/Xlib.h>
#include <X11/extensions/composite.h>
#ifdef __cplusplus
extern "C" {
#endif
Bool XCompositeQueryExtension (Display*, int*, int*);
Status XCompositeQueryVersion (Display*, int*, int*);
Window XCompositeGetOverlayWindow (Display*, Window);
void XCompositeReleaseOverlayWindow (Display*, Window);
#ifdef __cplusplus
}
#endif
#endif
