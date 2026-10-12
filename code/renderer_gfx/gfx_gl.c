/*
gfx_gl.c - renderer_gfx's GL calls on the gfx device (new file). It replaces the qgl function
pointers of code/renderercommon/qgl.h, and their loading in code/sdl/sdl_glimp.c, for
renderer_gfx.

The back end of the copied renderergl1 files keeps its GL-shaped calls (qglEnable,
qglBindTexture, qglVertexPointer, qglDrawElements, ...): tr_local.h declares them as the
functions below, so the front end and most of the back end stay as in renderergl1. This file
implements the subset of GL they use on gfx: GL's loose state (capabilities, blend / depth /
alpha / stencil functions, matrices, client arrays, texture units, immediate-mode vertices) is
kept in gfxGl as plain data, and the calls that act - clears, viewport, scissor - go to the gfx
device directly. Draws and texture uploads are recorded and dropped until GFX_STATUS.md's phases
3 (images, the 2D path) and 4 (the shader-stage drawing) turn them into gfx textures, pipelines
and draws.

Everything runs on the main thread inside the renderer, as the GL calls it replaces.
*/

#include "gfx_gl.h"
#include "gfx_glimp.h"

#if GFX_GL_MAX_TEXTURES <= MAX_DRAWIMAGES
#error "GFX_GL_MAX_TEXTURES must leave room for every image's texture name"
#endif

gfxGl_t gfxGl;

// qgl.h's pointers: the extension entry points GLimp_InitExtensions sets (gfx_glimp.c), and the
// GL 3 call renderergl1's gfxinfo looks for (never set: gfx_gl emulates GL 1.3)
void (APIENTRYP qglActiveTextureARB) (GLenum texture);
void (APIENTRYP qglClientActiveTextureARB) (GLenum texture);
void (APIENTRYP qglMultiTexCoord2fARB) (GLenum target, GLfloat s, GLfloat t);
void (APIENTRYP qglLockArraysEXT) (GLint first, GLsizei count);
void (APIENTRYP qglUnlockArraysEXT) (void);

#define GLE(ret, name, ...) name##proc * qgl##name = NULL;
QGL_3_0_PROCS;
#undef GLE

// renderer_gfx has no GL context, so the versions stay 0 (qgl.h's QGL_VERSION_ATLEAST reads them)
int qglMajorVersion, qglMinorVersion;
int qglesMajorVersion, qglesMinorVersion;

static const float gfxGlIdentity[16] = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

/*
===============
GfxGL_SetError

GL keeps the first error until glGetError reads it
===============
*/
static void GfxGL_SetError( GLenum error )
{
	if ( gfxGl.error == GL_NO_ERROR ) {
		gfxGl.error = error;
	}
}

static void GfxGL_SetColor( float red, float green, float blue, float alpha )
{
	gfxGl.currentColor[0] = red;
	gfxGl.currentColor[1] = green;
	gfxGl.currentColor[2] = blue;
	gfxGl.currentColor[3] = alpha;
}

static gfxGlMatrixStack_t *GfxGL_Matrix( void )
{
	return gfxGl.matrixMode == GL_PROJECTION ? &gfxGl.projection : &gfxGl.modelview;
}

static void GfxGL_ApplyViewport( void )
{
	if ( !gfxGl.device ) {
		return;
	}
	gfx_set_viewport( gfxGl.device, gfxGl.viewport[0], gfxGl.viewport[1],
		(uint32_t)MAX( gfxGl.viewport[2], 0 ), (uint32_t)MAX( gfxGl.viewport[3], 0 ),
		gfxGl.depthRange[0], gfxGl.depthRange[1] );
}

/*
===============
GfxGL_BoundTexture

The texture bound to a unit, NULL for a name that is not live
===============
*/
static gfxGlTexture_t *GfxGL_BoundTexture( int unit )
{
	GLuint name = gfxGl.boundTextures[unit];

	if ( name >= (GLuint)gfxGl.numTextures || !gfxGl.textures[name].live ) {
		return NULL;
	}
	return &gfxGl.textures[name];
}

static void GfxGL_InitTexture( gfxGlTexture_t *texture )
{
	Com_Memset( texture, 0, sizeof( *texture ) );
	texture->live = qtrue;
	texture->wrapS = GL_REPEAT;
	texture->wrapT = GL_REPEAT;
	texture->minFilter = GL_NEAREST_MIPMAP_LINEAR;
	texture->magFilter = GL_LINEAR;
	texture->anisotropy = 1.0f;
}

/*
===============
GfxGL_Reset

GL's defaults
===============
*/
static void GfxGL_Reset( void )
{
	int i;

	Com_Memset( &gfxGl, 0, sizeof( gfxGl ) );

	gfxGl.error = GL_NO_ERROR;
	gfxGl.clearDepth = 1.0f;
	gfxGl.depthRange[1] = 1.0f;
	gfxGl.blendSrc = GL_ONE;
	gfxGl.blendDst = GL_ZERO;
	gfxGl.depthFunc = GL_LESS;
	gfxGl.depthMask = qtrue;
	for ( i = 0; i < 4; i++ ) {
		gfxGl.colorMask[i] = qtrue;
	}
	gfxGl.cullFace = GL_BACK;
	gfxGl.alphaFunc = GL_ALWAYS;
	gfxGl.polygonMode = GL_FILL;
	gfxGl.stencilFunc = GL_ALWAYS;
	gfxGl.stencilFuncMask = ~0u;
	gfxGl.stencilWriteMask = ~0u;
	for ( i = 0; i < 3; i++ ) {
		gfxGl.stencilOp[i] = GL_KEEP;
	}
	Com_Memcpy( gfxGl.clipPlane0Modelview, gfxGlIdentity, sizeof( gfxGlIdentity ) );
	gfxGl.lineWidth = 1.0f;

	gfxGl.matrixMode = GL_MODELVIEW;
	Com_Memcpy( gfxGl.modelview.top, gfxGlIdentity, sizeof( gfxGlIdentity ) );
	Com_Memcpy( gfxGl.projection.top, gfxGlIdentity, sizeof( gfxGlIdentity ) );

	for ( i = 0; i < GFX_GL_TEXTURE_UNITS; i++ ) {
		gfxGl.texEnv[i] = GL_MODULATE;
	}

	GfxGL_SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	// texture name 0 is GL's default texture: present, never deleted
	GfxGL_InitTexture( &gfxGl.textures[0] );
	gfxGl.numTextures = 1;
}

/*
===============
GfxGL_MaxTextureSize

GL_MAX_TEXTURE_SIZE per device: the size each API guarantees (GL 4.1+ / D3D11 / D3D12 16384,
Vulkan 4096, GLES 3.0 2048); GL 3 is given 8192, what every GL 3 driver in use has. gfx has no
capability query.
===============
*/
static GLint GfxGL_MaxTextureSize( enum gfx_device_backend backend )
{
	switch ( backend ) {
	case GFX_DEVICE_GL4:
	case GFX_DEVICE_D3D11:
	case GFX_DEVICE_D3D12:
		return 16384;
	case GFX_DEVICE_GL3:
		return 8192;
	case GFX_DEVICE_VK:
		return 4096;
	default:
		return 2048;
	}
}

/*
===============
GfxGL_Init

GLimp_Init's half of the GL state: the gfx window the back end draws to, and GL's defaults
===============
*/
void GfxGL_Init( struct gfx_window *window, enum gfx_device_backend backend )
{
	GfxGL_Reset();

	gfxGl.window = window;
	gfxGl.device = gfx_get_device( window );
	gfxGl.deviceBackend = backend;
	gfxGl.maxTextureSize = GfxGL_MaxTextureSize( backend );
	gfxGl.viewport[2] = (GLint)gfx_window_get_width( window );
	gfxGl.viewport[3] = (GLint)gfx_window_get_height( window );
	Com_Memcpy( gfxGl.scissor, gfxGl.viewport, sizeof( gfxGl.scissor ) );

	GfxGL_BeginFrame();
}

/*
===============
GfxGL_Shutdown

GLimp_Shutdown's half: every gfx texture still held is deleted while the device lives, and the
state goes back to GL's defaults
===============
*/
void GfxGL_Shutdown( void )
{
	int i;

	if ( gfxGl.device ) {
		for ( i = 0; i < gfxGl.numTextures; i++ ) {
			if ( gfxGl.textures[i].texture ) {
				gfx_delete_texture( gfxGl.device, gfxGl.textures[i].texture );
			}
		}
	}

	GfxGL_Reset();
}

/*
===============
GfxGL_BeginFrame

Each frame starts on the window's framebuffer (GLimp_Init, and GLimp_EndFrame after the swap)
===============
*/
void GfxGL_BeginFrame( void )
{
	if ( !gfxGl.device ) {
		return;
	}
	gfx_bind_framebuffer( gfxGl.device, NULL );
	GfxGL_ApplyViewport();
}

/*
==============================================================================

The qgl calls of renderer_gfx's back end, with the GL signatures of qgl.h's lists

==============================================================================
*/

GLenum APIENTRY qglGetError( void )
{
	GLenum error = gfxGl.error;

	gfxGl.error = GL_NO_ERROR;
	return error;
}

/*
===============
qglGetString

What GLimp_Init copies into glConfig and gfxinfo prints: the strings name gfx, GL_EXTENSIONS is
the emulated list
===============
*/
const GLubyte * APIENTRY qglGetString( GLenum name )
{
	switch ( name ) {
	case GL_VENDOR:
		return (const GLubyte *)"gfx";
	case GL_RENDERER:
		return (const GLubyte *)"renderer_gfx";
	case GL_VERSION:
		return (const GLubyte *)"1.3 renderer_gfx";
	case GL_EXTENSIONS:
		return (const GLubyte *)GFX_GL_EXTENSIONS;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return NULL;
	}
}

void APIENTRY qglGetIntegerv( GLenum pname, GLint *params )
{
	switch ( pname ) {
	case GL_MAX_TEXTURE_SIZE:
		*params = gfxGl.maxTextureSize;
		break;
	case GL_MAX_TEXTURE_UNITS_ARB:
		*params = GFX_GL_TEXTURE_UNITS;
		break;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		*params = GFX_GL_MAX_ANISOTROPY;
		break;
	case GL_PACK_ALIGNMENT:
		// glReadPixels rows are packed tightly
		*params = 1;
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		break;
	}
}

void APIENTRY qglGetBooleanv( GLenum pname, GLboolean *params )
{
	int i;

	if ( pname != GL_COLOR_WRITEMASK ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	for ( i = 0; i < 4; i++ ) {
		params[i] = gfxGl.colorMask[i] ? GL_TRUE : GL_FALSE;
	}
}

static unsigned GfxGL_CapBit( GLenum cap )
{
	switch ( cap ) {
	case GL_BLEND:					return GFX_GL_CAP_BLEND;
	case GL_DEPTH_TEST:				return GFX_GL_CAP_DEPTH_TEST;
	case GL_CULL_FACE:				return GFX_GL_CAP_CULL_FACE;
	case GL_ALPHA_TEST:				return GFX_GL_CAP_ALPHA_TEST;
	case GL_SCISSOR_TEST:			return GFX_GL_CAP_SCISSOR_TEST;
	case GL_STENCIL_TEST:			return GFX_GL_CAP_STENCIL_TEST;
	case GL_POLYGON_OFFSET_FILL:	return GFX_GL_CAP_POLYGON_OFFSET_FILL;
	case GL_CLIP_PLANE0:			return GFX_GL_CAP_CLIP_PLANE0;
	default:						return 0;
	}
}

void APIENTRY qglEnable( GLenum cap )
{
	unsigned bit;

	if ( cap == GL_TEXTURE_2D ) {
		gfxGl.texture2D[gfxGl.activeTexture] = qtrue;
		return;
	}
	bit = GfxGL_CapBit( cap );
	if ( !bit ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	gfxGl.caps |= bit;
}

void APIENTRY qglDisable( GLenum cap )
{
	unsigned bit;

	if ( cap == GL_TEXTURE_2D ) {
		gfxGl.texture2D[gfxGl.activeTexture] = qfalse;
		return;
	}
	bit = GfxGL_CapBit( cap );
	if ( !bit ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	gfxGl.caps &= ~bit;
}

static gfxGlClientArray_t *GfxGL_ClientArray( GLenum cap )
{
	switch ( cap ) {
	case GL_VERTEX_ARRAY:
		return &gfxGl.vertexArray;
	case GL_COLOR_ARRAY:
		return &gfxGl.colorArray;
	case GL_TEXTURE_COORD_ARRAY:
		return &gfxGl.texCoordArrays[gfxGl.clientActiveTexture];
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return NULL;
	}
}

void APIENTRY qglEnableClientState( GLenum cap )
{
	gfxGlClientArray_t *array = GfxGL_ClientArray( cap );

	if ( array ) {
		array->enabled = qtrue;
	}
}

void APIENTRY qglDisableClientState( GLenum cap )
{
	gfxGlClientArray_t *array = GfxGL_ClientArray( cap );

	if ( array ) {
		array->enabled = qfalse;
	}
}

static void GfxGL_SetArray( gfxGlClientArray_t *array, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	array->size = size;
	array->type = type;
	array->stride = stride;
	array->ptr = ptr;
}

void APIENTRY qglVertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	GfxGL_SetArray( &gfxGl.vertexArray, size, type, stride, ptr );
}

void APIENTRY qglColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	GfxGL_SetArray( &gfxGl.colorArray, size, type, stride, ptr );
}

void APIENTRY qglTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	GfxGL_SetArray( &gfxGl.texCoordArrays[gfxGl.clientActiveTexture], size, type, stride, ptr );
}

/*
===============
GfxGL_TextureUnit

The unit a GL_TEXTUREn_ARB names, or -1
===============
*/
static int GfxGL_TextureUnit( GLenum texture )
{
	if ( texture < GL_TEXTURE0_ARB || texture >= GL_TEXTURE0_ARB + GFX_GL_TEXTURE_UNITS ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return -1;
	}
	return (int)( texture - GL_TEXTURE0_ARB );
}

void APIENTRY GfxGL_ActiveTextureARB( GLenum texture )
{
	int unit = GfxGL_TextureUnit( texture );

	if ( unit >= 0 ) {
		gfxGl.activeTexture = unit;
	}
}

void APIENTRY GfxGL_ClientActiveTextureARB( GLenum texture )
{
	int unit = GfxGL_TextureUnit( texture );

	if ( unit >= 0 ) {
		gfxGl.clientActiveTexture = unit;
	}
}

void APIENTRY GfxGL_LockArraysEXT( GLint first, GLsizei count )
{
	gfxGl.locked = qtrue;
	gfxGl.lockedFirst = first;
	gfxGl.lockedCount = count;
}

void APIENTRY GfxGL_UnlockArraysEXT( void )
{
	gfxGl.locked = qfalse;
}

/*
==============================================================================

Fixed-function state: recorded, read by the draws (phase 4)

==============================================================================
*/

void APIENTRY qglBlendFunc( GLenum sfactor, GLenum dfactor )
{
	gfxGl.blendSrc = sfactor;
	gfxGl.blendDst = dfactor;
}

void APIENTRY qglDepthFunc( GLenum func )
{
	gfxGl.depthFunc = func;
}

void APIENTRY qglDepthMask( GLboolean flag )
{
	gfxGl.depthMask = flag != GL_FALSE;
}

void APIENTRY qglColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha )
{
	gfxGl.colorMask[0] = red != GL_FALSE;
	gfxGl.colorMask[1] = green != GL_FALSE;
	gfxGl.colorMask[2] = blue != GL_FALSE;
	gfxGl.colorMask[3] = alpha != GL_FALSE;
}

void APIENTRY qglCullFace( GLenum mode )
{
	gfxGl.cullFace = mode;
}

void APIENTRY qglAlphaFunc( GLenum func, GLclampf ref )
{
	gfxGl.alphaFunc = func;
	gfxGl.alphaRef = ref;
}

void APIENTRY qglPolygonMode( GLenum face, GLenum mode )
{
	gfxGl.polygonMode = mode;
}

void APIENTRY qglPolygonOffset( GLfloat factor, GLfloat units )
{
	gfxGl.polygonOffset[0] = factor;
	gfxGl.polygonOffset[1] = units;
}

void APIENTRY qglShadeModel( GLenum mode )
{
	// the shaders interpolate colors (GL_SMOOTH); renderergl1 never sets GL_FLAT
}

void APIENTRY qglStencilFunc( GLenum func, GLint ref, GLuint mask )
{
	gfxGl.stencilFunc = func;
	gfxGl.stencilRef = ref;
	gfxGl.stencilFuncMask = mask;
}

void APIENTRY qglStencilOp( GLenum fail, GLenum zfail, GLenum zpass )
{
	gfxGl.stencilOp[0] = fail;
	gfxGl.stencilOp[1] = zfail;
	gfxGl.stencilOp[2] = zpass;
}

void APIENTRY qglStencilMask( GLuint mask )
{
	gfxGl.stencilWriteMask = mask;
}

/*
===============
qglClipPlane

GL keeps the plane in eye coordinates (times the inverse of the modelview it was set under); the
plane and that modelview (s_flipMatrix in RB_BeginDrawingView) are kept for the draws to apply
===============
*/
void APIENTRY qglClipPlane( GLenum plane, const GLdouble *equation )
{
	if ( plane != GL_CLIP_PLANE0 ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	Com_Memcpy( gfxGl.clipPlane0, equation, sizeof( gfxGl.clipPlane0 ) );
	Com_Memcpy( gfxGl.clipPlane0Modelview, gfxGl.modelview.top, sizeof( gfxGl.clipPlane0Modelview ) );
}

void APIENTRY qglLineWidth( GLfloat width )
{
	gfxGl.lineWidth = width;
}

void APIENTRY qglTexEnvf( GLenum target, GLenum pname, GLfloat param )
{
	if ( target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	gfxGl.texEnv[gfxGl.activeTexture] = (GLenum)param;
}

/*
==============================================================================

Clears, viewport, scissor: straight to gfx

==============================================================================
*/

void APIENTRY qglClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha )
{
	gfxGl.clearColor[0] = red;
	gfxGl.clearColor[1] = green;
	gfxGl.clearColor[2] = blue;
	gfxGl.clearColor[3] = alpha;
}

void APIENTRY qglClearDepth( GLclampd depth )
{
	gfxGl.clearDepth = (float)depth;
}

void APIENTRY qglClearStencil( GLint s )
{
	gfxGl.clearStencil = (byte)s;
}

/*
===============
qglClear

glClear on the window. gfx clears depth and stencil together, so a GL_DEPTH_BUFFER_BIT clear
resets the stencil too (renderergl1 only reads the stencil inside the frame that set it).
===============
*/
void APIENTRY qglClear( GLbitfield mask )
{
	struct gfx_clear_color color;

	if ( !gfxGl.device ) {
		return;
	}
	if ( mask & GL_COLOR_BUFFER_BIT ) {
		color.r = gfxGl.clearColor[0];
		color.g = gfxGl.clearColor[1];
		color.b = gfxGl.clearColor[2];
		color.a = gfxGl.clearColor[3];
		gfx_clear_color( gfxGl.device, NULL, 0, &color );
	}
	if ( mask & ( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT ) ) {
		gfx_clear_depth_stencil( gfxGl.device, NULL, gfxGl.clearDepth, gfxGl.clearStencil );
	}
}

void APIENTRY qglViewport( GLint x, GLint y, GLsizei width, GLsizei height )
{
	gfxGl.viewport[0] = x;
	gfxGl.viewport[1] = y;
	gfxGl.viewport[2] = width;
	gfxGl.viewport[3] = height;
	GfxGL_ApplyViewport();
}

void APIENTRY qglDepthRange( GLclampd near_val, GLclampd far_val )
{
	gfxGl.depthRange[0] = (float)near_val;
	gfxGl.depthRange[1] = (float)far_val;
	GfxGL_ApplyViewport();
}

void APIENTRY qglScissor( GLint x, GLint y, GLsizei width, GLsizei height )
{
	gfxGl.scissor[0] = x;
	gfxGl.scissor[1] = y;
	gfxGl.scissor[2] = width;
	gfxGl.scissor[3] = height;
	if ( gfxGl.device ) {
		gfx_set_scissor( gfxGl.device, x, y, (uint32_t)MAX( width, 0 ), (uint32_t)MAX( height, 0 ) );
	}
}

void APIENTRY qglDrawBuffer( GLenum mode )
{
	// gfx always draws to the back buffer; r_drawBuffer GL_FRONT only skips the swap
	// (GLimp_EndFrame)
}

void APIENTRY qglFinish( void )
{
	// gfx has no glFinish; the swap is the only synchronization point
}

/*
===============
qglReadPixels

gfx has no read back of its framebuffers, so GL_RGB / GL_RGBA bytes are read from the window on
the screen (GfxGLimp_ReadWindowPixels: the frame presented last, Windows only); anything else,
or a failed read, gives zeros and one warning. GL_PACK_ALIGNMENT is 1 (qglGetIntegerv), so the
rows are packed.
===============
*/
void APIENTRY qglReadPixels( GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels )
{
	int components, componentSize;
	size_t size;
	byte *bgra, *out;
	int i, numPixels;

	switch ( format ) {
	case GL_RGBA:
		components = 4;
		break;
	case GL_RGB:
		components = 3;
		break;
	case GL_DEPTH_COMPONENT:
	case GL_STENCIL_INDEX:
		components = 1;
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}

	switch ( type ) {
	case GL_UNSIGNED_BYTE:
		componentSize = 1;
		break;
	case GL_FLOAT:
		componentSize = 4;
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}

	if ( width <= 0 || height <= 0 ) {
		return;
	}
	numPixels = width * height;
	size = (size_t)numPixels * components * componentSize;

	if ( ( format == GL_RGB || format == GL_RGBA ) && type == GL_UNSIGNED_BYTE ) {
		bgra = ri.Hunk_AllocateTempMemory( numPixels * 4 );
		if ( GfxGLimp_ReadWindowPixels( x, y, width, height, bgra ) ) {
			out = (byte *)pixels;
			for ( i = 0; i < numPixels; i++ ) {
				out[0] = bgra[i * 4 + 2];
				out[1] = bgra[i * 4 + 1];
				out[2] = bgra[i * 4 + 0];
				if ( components == 4 ) {
					out[3] = 255;
				}
				out += components;
			}
			ri.Hunk_FreeTempMemory( bgra );
			return;
		}
		ri.Hunk_FreeTempMemory( bgra );
	}

	if ( !gfxGl.warnedReadPixels ) {
		gfxGl.warnedReadPixels = qtrue;
		ri.Printf( PRINT_WARNING, "gfx: no read back of this framebuffer, glReadPixels returns zeros\n" );
	}
	Com_Memset( pixels, 0, size );
}

/*
==============================================================================

Matrices

==============================================================================
*/

void APIENTRY qglMatrixMode( GLenum mode )
{
	if ( mode != GL_MODELVIEW && mode != GL_PROJECTION ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	gfxGl.matrixMode = mode;
}

void APIENTRY qglLoadIdentity( void )
{
	Com_Memcpy( GfxGL_Matrix()->top, gfxGlIdentity, sizeof( gfxGlIdentity ) );
}

void APIENTRY qglLoadMatrixf( const GLfloat *m )
{
	Com_Memcpy( GfxGL_Matrix()->top, m, sizeof( gfxGlIdentity ) );
}

/*
===============
GfxGL_MultMatrix

The current matrix times b, both column-major (glMultMatrix's order)
===============
*/
static void GfxGL_MultMatrix( const float *b )
{
	float *a = GfxGL_Matrix()->top;
	float out[16];
	int col, row, k;

	for ( col = 0; col < 4; col++ ) {
		for ( row = 0; row < 4; row++ ) {
			out[col * 4 + row] = 0.0f;
			for ( k = 0; k < 4; k++ ) {
				out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
			}
		}
	}
	Com_Memcpy( a, out, sizeof( out ) );
}

void APIENTRY qglOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val )
{
	float l = (float)left, r = (float)right, b = (float)bottom;
	float t = (float)top, n = (float)near_val, f = (float)far_val;
	float ortho[16];

	Com_Memset( ortho, 0, sizeof( ortho ) );
	ortho[0] = 2.0f / ( r - l );
	ortho[5] = 2.0f / ( t - b );
	ortho[10] = -2.0f / ( f - n );
	ortho[12] = -( r + l ) / ( r - l );
	ortho[13] = -( t + b ) / ( t - b );
	ortho[14] = -( f + n ) / ( f - n );
	ortho[15] = 1.0f;
	GfxGL_MultMatrix( ortho );
}

void APIENTRY qglTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
	float translate[16];

	Com_Memcpy( translate, gfxGlIdentity, sizeof( translate ) );
	translate[12] = x;
	translate[13] = y;
	translate[14] = z;
	GfxGL_MultMatrix( translate );
}

void APIENTRY qglPushMatrix( void )
{
	gfxGlMatrixStack_t *matrix = GfxGL_Matrix();

	if ( matrix->depth == GFX_GL_MATRIX_STACK_DEPTH ) {
		GfxGL_SetError( GL_STACK_OVERFLOW );
		return;
	}
	Com_Memcpy( matrix->saved[matrix->depth], matrix->top, sizeof( matrix->top ) );
	matrix->depth++;
}

void APIENTRY qglPopMatrix( void )
{
	gfxGlMatrixStack_t *matrix = GfxGL_Matrix();

	if ( matrix->depth == 0 ) {
		GfxGL_SetError( GL_STACK_UNDERFLOW );
		return;
	}
	matrix->depth--;
	Com_Memcpy( matrix->top, matrix->saved[matrix->depth], sizeof( matrix->top ) );
}

/*
==============================================================================

Textures: names and parameters recorded; the gfx textures come in phase 3

==============================================================================
*/

void APIENTRY qglGenTextures( GLsizei n, GLuint *textures )
{
	int i, name;

	name = 1;
	for ( i = 0; i < n; i++ ) {
		// reuse a deleted name, else a new one at the end
		while ( name < gfxGl.numTextures && gfxGl.textures[name].live ) {
			name++;
		}
		if ( name == GFX_GL_MAX_TEXTURES ) {
			ri.Error( ERR_DROP, "qglGenTextures: more than %d textures", GFX_GL_MAX_TEXTURES );
		}
		if ( name == gfxGl.numTextures ) {
			gfxGl.numTextures++;
		}
		GfxGL_InitTexture( &gfxGl.textures[name] );
		textures[i] = (GLuint)name;
	}
}

void APIENTRY qglDeleteTextures( GLsizei n, const GLuint *textures )
{
	gfxGlTexture_t *texture;
	int i, unit;
	GLuint name;

	for ( i = 0; i < n; i++ ) {
		name = textures[i];
		if ( name == 0 || name >= (GLuint)gfxGl.numTextures || !gfxGl.textures[name].live ) {
			continue;
		}
		texture = &gfxGl.textures[name];
		if ( texture->texture && gfxGl.device ) {
			gfx_delete_texture( gfxGl.device, texture->texture );
		}
		texture->texture = NULL;
		texture->live = qfalse;

		// GL unbinds a deleted texture from every unit
		for ( unit = 0; unit < GFX_GL_TEXTURE_UNITS; unit++ ) {
			if ( gfxGl.boundTextures[unit] == name ) {
				gfxGl.boundTextures[unit] = 0;
			}
		}
	}
}

void APIENTRY qglBindTexture( GLenum target, GLuint texture )
{
	if ( texture >= (GLuint)gfxGl.numTextures || !gfxGl.textures[texture].live ) {
		GfxGL_SetError( GL_INVALID_VALUE );
		return;
	}
	gfxGl.boundTextures[gfxGl.activeTexture] = texture;
}

void APIENTRY qglTexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	gfxGlTexture_t *texture = GfxGL_BoundTexture( gfxGl.activeTexture );

	if ( !texture ) {
		return;
	}
	switch ( pname ) {
	case GL_TEXTURE_MIN_FILTER:
		texture->minFilter = (GLenum)param;
		break;
	case GL_TEXTURE_MAG_FILTER:
		texture->magFilter = (GLenum)param;
		break;
	case GL_TEXTURE_WRAP_S:
		texture->wrapS = (GLenum)param;
		break;
	case GL_TEXTURE_WRAP_T:
		texture->wrapT = (GLenum)param;
		break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:
		texture->anisotropy = param;
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		break;
	}
}

void APIENTRY qglTexParameteri( GLenum target, GLenum pname, GLint param )
{
	qglTexParameterf( target, pname, (GLfloat)param );
}

void APIENTRY qglTexImage2D( GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	gfxGlTexture_t *texture = GfxGL_BoundTexture( gfxGl.activeTexture );

	if ( !texture ) {
		return;
	}
	if ( level == 0 ) {
		texture->width = width;
		texture->height = height;
	}
	texture->levels = MAX( texture->levels, level + 1 );
}

void APIENTRY qglTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
}

/*
==============================================================================

Draws: dropped until phase 4 draws them

==============================================================================
*/

void APIENTRY qglDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
}

void APIENTRY qglArrayElement( GLint i )
{
}

/*
==============================================================================

Immediate mode: the vertices are collected, glEnd draws them (phase 4)

==============================================================================
*/

void APIENTRY qglBegin( GLenum mode )
{
	gfxGl.inBegin = qtrue;
	gfxGl.immediateMode = mode;
	gfxGl.numImmediate = 0;
}

void APIENTRY qglEnd( void )
{
	gfxGl.inBegin = qfalse;
	gfxGl.numImmediate = 0;
}

static void GfxGL_ImmediateVertex( float x, float y, float z )
{
	gfxGlVertex_t *vertex;
	int unit;

	if ( gfxGl.numImmediate == GFX_GL_IMMEDIATE_VERTICES ) {
		if ( !gfxGl.warnedImmediate ) {
			gfxGl.warnedImmediate = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: more than %d vertices between glBegin and glEnd, the rest dropped\n",
				GFX_GL_IMMEDIATE_VERTICES );
		}
		return;
	}
	vertex = &gfxGl.immediate[gfxGl.numImmediate++];
	VectorSet( vertex->xyz, x, y, z );
	for ( unit = 0; unit < GFX_GL_TEXTURE_UNITS; unit++ ) {
		vertex->st[unit][0] = gfxGl.currentSt[unit][0];
		vertex->st[unit][1] = gfxGl.currentSt[unit][1];
	}
	Vector4Copy( gfxGl.currentColor, vertex->rgba );
}

void APIENTRY qglVertex2f( GLfloat x, GLfloat y )
{
	GfxGL_ImmediateVertex( x, y, 0.0f );
}

void APIENTRY qglVertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	GfxGL_ImmediateVertex( x, y, z );
}

void APIENTRY qglVertex3fv( const GLfloat *v )
{
	GfxGL_ImmediateVertex( v[0], v[1], v[2] );
}

void APIENTRY qglColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	GfxGL_SetColor( red, green, blue, 1.0f );
}

void APIENTRY qglColor4f( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha )
{
	GfxGL_SetColor( red, green, blue, alpha );
}

void APIENTRY qglColor4ubv( const GLubyte *v )
{
	GfxGL_SetColor( v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f );
}

void APIENTRY qglTexCoord2f( GLfloat s, GLfloat t )
{
	gfxGl.currentSt[0][0] = s;
	gfxGl.currentSt[0][1] = t;
}

void APIENTRY qglTexCoord2fv( const GLfloat *v )
{
	gfxGl.currentSt[0][0] = v[0];
	gfxGl.currentSt[0][1] = v[1];
}

void APIENTRY GfxGL_MultiTexCoord2fARB( GLenum target, GLfloat s, GLfloat t )
{
	int unit = GfxGL_TextureUnit( target );

	if ( unit >= 0 ) {
		gfxGl.currentSt[unit][0] = s;
		gfxGl.currentSt[unit][1] = t;
	}
}
