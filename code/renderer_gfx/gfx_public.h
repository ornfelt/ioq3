/*
gfx_public.h - what the client sees of renderer_gfx (new file, no renderergl1 counterpart).

renderer_gfx is a static library linked into ioquake3, not a renderer dll: CL_InitRef calls
GfxGetRefAPI directly when cl_renderer is "gfx", and loads renderer_<name> dlls otherwise.
GetRefAPI has its own name here because the client's dlopen code declares a local GetRefAPI.
*/

#ifndef GFX_PUBLIC_H
#define GFX_PUBLIC_H

#include "../renderercommon/tr_public.h"

struct gfx_window;

// the cl_renderer value that selects renderer_gfx
#define GFX_RENDERER_NAME	"gfx"

refexport_t *GfxGetRefAPI( int apiVersion, refimport_t *rimp );

// the gfx window renderer_gfx's GLimp_Init created, which the client reads its input from; NULL
// before GLimp_Init, after GLimp_Shutdown and while another renderer is loaded
struct gfx_window *GfxGLimp_GetWindow( void );

#endif
