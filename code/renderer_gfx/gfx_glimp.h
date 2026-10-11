/*
gfx_glimp.h - renderer_gfx's window and device on the gfx library (new file; gfx_glimp.c
replaces code/sdl/sdl_glimp.c and sdl_gamma.c for renderer_gfx).
*/

#ifndef GFX_GLIMP_H
#define GFX_GLIMP_H

#include "gfx/window.h"

// r_gfxDevice's default: gl4 is the device every feature is brought up and tested on first
#define R_GFX_DEFAULT_DEVICE	GFX_DEVICE_GL4
// r_gfxWindow's default: SDL, the window system the GL renderers use
#define R_GFX_DEFAULT_WINDOW	GFX_WINDOW_SDL

void GfxGLimp_Backends( enum gfx_device_backend *device, enum gfx_window_backend *window );
void GfxGLimp_PrintBackends( void );

#endif
