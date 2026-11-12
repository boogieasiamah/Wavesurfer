/* Minimal libXcursor declarations (ABI-compatible subset) for building JUCE on headless Linux CI. */
#ifndef _XCURSOR_H_
#define _XCURSOR_H_
#include <X11/Xlib.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int XcursorBool; typedef unsigned int XcursorUInt; typedef XcursorUInt XcursorDim; typedef XcursorUInt XcursorPixel;
typedef struct _XcursorImage {
    XcursorUInt version; XcursorDim size, width, height, xhot, yhot; XcursorUInt delay; XcursorPixel* pixels;
} XcursorImage;
XcursorImage* XcursorImageCreate (int, int);
void XcursorImageDestroy (XcursorImage*);
Cursor XcursorImageLoadCursor (Display*, const XcursorImage*);
XcursorBool XcursorSupportsARGB (Display*);
#ifdef __cplusplus
}
#endif
#endif
