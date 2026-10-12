/*
gfx_gl.c - renderer_gfx's GL calls on the gfx device (new file). It replaces the qgl function
pointers of code/renderercommon/qgl.h, and their loading in code/sdl/sdl_glimp.c, for
renderer_gfx.

The back end of the copied renderergl1 files keeps its GL-shaped calls (qglEnable,
qglBindTexture, qglVertexPointer, qglDrawElements, ...): tr_local.h declares them as the
functions below, so the front end and most of the back end stay as in renderergl1. This file
implements the subset of GL they use on gfx (the port of ioq3-rs's gfx_gl.rs): GL's loose state
(capabilities, blend / depth / alpha / stencil functions, matrices, client arrays, texture units,
immediate-mode vertices) is kept in gfxGl as plain data, and the calls that act go to gfx:
clears, viewport and scissor to the device directly, texture images through a staging buffer
into gfx textures, and draws - glDrawElements, glArrayElement, glBegin / glEnd - as gfxVertex_t
and a gfxDrawState_t to gfx_draw.c.

Everything runs on the main thread inside the renderer, as the GL calls it replaces.
*/

#include "gfx_gl.h"
#include "gfx_glimp.h"

#if GFX_GL_MAX_TEXTURES <= MAX_DRAWIMAGES
#error "GFX_GL_MAX_TEXTURES must leave room for every image's texture name"
#endif

gfxGl_t gfxGl;

// a draw's vertices and indices on their way to gfx_draw.c: the most a glBegin / glEnd block or a
// tess batch gives (a strip of n vertices is 3 * (n - 2) indices)
#define GFX_GL_DRAW_VERTICES	GFX_GL_IMMEDIATE_VERTICES
#define GFX_GL_DRAW_INDICES		( 3 * GFX_GL_IMMEDIATE_VERTICES )

#if GFX_GL_DRAW_VERTICES < SHADER_MAX_VERTEXES || GFX_GL_DRAW_INDICES < SHADER_MAX_INDEXES
#error "the draw scratch must hold a tess batch"
#endif

static gfxVertex_t gfxGlDrawVertices[GFX_GL_DRAW_VERTICES];
static uint32_t gfxGlDrawIndices[GFX_GL_DRAW_INDICES];

// one unit of a 24-bit depth buffer in normalized device depth ([-1, 1] spans 2^24 steps)
#define GFX_GL_NDC_DEPTH_UNIT		( 2.0f / (float)( 1 << 24 ) )
// glPolygonOffset's factor scales the polygon's depth slope, which a vertex shader cannot know:
// a slope of this many depth units per pixel is assumed (GFX_STATUS.md, decisions)
#define GFX_GL_POLYGON_OFFSET_SLOPE	16.0f

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

static byte GfxGL_ColorByte( float c )
{
	return (byte)( Com_Clamp( 0.0f, 1.0f, c ) * 255.0f + 0.5f );
}

static void GfxGL_SetColor( float red, float green, float blue, float alpha )
{
	int i;

	gfxGl.currentColor[0] = red;
	gfxGl.currentColor[1] = green;
	gfxGl.currentColor[2] = blue;
	gfxGl.currentColor[3] = alpha;
	for ( i = 0; i < 4; i++ ) {
		gfxGl.currentRgba[i] = GfxGL_ColorByte( gfxGl.currentColor[i] );
	}
}

/*
===============
GfxGL_Matrix

The current matrix, for a call that may change it
===============
*/
static gfxGlMatrixStack_t *GfxGL_Matrix( void )
{
	gfxGl.mvpValid = qfalse;
	return gfxGl.matrixMode == GL_PROJECTION ? &gfxGl.projection : &gfxGl.modelview;
}

/*
===============
GfxGL_MatrixProduct

out = a * b, all column-major (glMultMatrix's order); out may not be a or b
===============
*/
static void GfxGL_MatrixProduct( const float *a, const float *b, float *out )
{
	int col, row, k;

	for ( col = 0; col < 4; col++ ) {
		for ( row = 0; row < 4; row++ ) {
			out[col * 4 + row] = 0.0f;
			for ( k = 0; k < 4; k++ ) {
				out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
			}
		}
	}
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
GfxGL_ClipDepthZeroToOne

Whether a device's clip space has z in [0, w] (D3D, Vulkan) rather than GL's [-w, w]. Its y needs
nothing: gfx turns D3D's viewport over and flips Vulkan's, so +y is up as on GL.
===============
*/
static qboolean GfxGL_ClipDepthZeroToOne( enum gfx_device_backend backend )
{
	return backend == GFX_DEVICE_D3D9 || backend == GFX_DEVICE_D3D11 || backend == GFX_DEVICE_D3D12
		|| backend == GFX_DEVICE_VK;
}

/*
===============
GfxGL_Init

GLimp_Init's half of the GL state: the gfx window the back end draws to, the shaders and buffers
it draws with (qfalse: error says what could not be loaded or created), and GL's defaults
===============
*/
qboolean GfxGL_Init( struct gfx_window *window, enum gfx_device_backend backend, char *error, int errorSize )
{
	GfxGL_Reset();

	if ( !GfxDraw_Init( gfx_get_device( window ), backend, gfx_window_get_window_backend( window ), error, errorSize ) ) {
		return qfalse;
	}
	gfxGl.window = window;
	gfxGl.device = gfx_get_device( window );
	gfxGl.deviceBackend = backend;
	gfxGl.depthZeroToOne = GfxGL_ClipDepthZeroToOne( backend );
	gfxGl.maxTextureSize = GfxGL_MaxTextureSize( backend );
	gfxGl.viewport[2] = (GLint)gfx_window_get_width( window );
	gfxGl.viewport[3] = (GLint)gfx_window_get_height( window );
	Com_Memcpy( gfxGl.scissor, gfxGl.viewport, sizeof( gfxGl.scissor ) );

	GfxGL_BeginFrame();
	return qtrue;
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
	GfxDraw_Shutdown();
	if ( gfxGl.staging.buffer ) {
		ri.Free( gfxGl.staging.buffer );
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
	GfxDraw_BeginFrame();
}

/*
==============================================================================

Texture staging and the draw state

==============================================================================
*/

/*
===============
GfxGL_LevelSize

The size of a mip level of a width x height texture
===============
*/
static void GfxGL_LevelSize( GLsizei width, GLsizei height, int level, int *levelWidth, int *levelHeight )
{
	*levelWidth = MAX( width >> level, 1 );
	*levelHeight = MAX( height >> level, 1 );
}

/*
===============
GfxGL_ReduceToFormat

What sampling a texture of GL internal format internalFormat gives for RGBA8 texels: luminance
formats repeat red (renderergl1 picks them only for grey images, so this keeps the texel), the
formats without alpha read alpha as 1
===============
*/
static void GfxGL_ReduceToFormat( GLenum internalFormat, byte *rgba, int numTexels )
{
	int i;

	switch ( internalFormat ) {
	case GL_LUMINANCE:
	case GL_LUMINANCE8:
		for ( i = 0; i < numTexels; i++, rgba += 4 ) {
			rgba[1] = rgba[2] = rgba[0];
			rgba[3] = 255;
		}
		break;
	case GL_LUMINANCE_ALPHA:
	case GL_LUMINANCE8_ALPHA8:
		for ( i = 0; i < numTexels; i++, rgba += 4 ) {
			rgba[1] = rgba[2] = rgba[0];
		}
		break;
	case GL_RGB:
	case GL_RGB8:
	case GL_RGB5:
	case GL_RGB4_S3TC:
		for ( i = 0; i < numTexels; i++, rgba += 4 ) {
			rgba[3] = 255;
		}
		break;
	default:
		break;
	}
}

static void GfxGL_CurrentSampling( const gfxGlTexture_t *texture, gfxGlSampling_t *sampling )
{
	Com_Memset( sampling, 0, sizeof( *sampling ) );
	sampling->wrapS = texture->wrapS;
	sampling->wrapT = texture->wrapT;
	sampling->minFilter = texture->minFilter;
	sampling->magFilter = texture->magFilter;
	sampling->anisotropy = texture->anisotropy > 1.0f ? (uint32_t)texture->anisotropy : 0;
}

// GL's wrap mode as gfx's addressing (GL_CLAMP is edge clamping here, as the GL renderers ask
// for GL_CLAMP_TO_EDGE whenever the edge-clamp extension is there, and renderer_gfx has it)
static enum gfx_texture_addressing GfxGL_Addressing( GLenum wrap )
{
	return wrap == GL_REPEAT ? GFX_TEXTURE_ADDRESSING_REPEAT : GFX_TEXTURE_ADDRESSING_CLAMP;
}

// GL's minification filter as gfx's min and mip filterings
static void GfxGL_MinFiltering( GLenum filter, enum gfx_filtering *min, enum gfx_filtering *mip )
{
	switch ( filter ) {
	case GL_NEAREST:
		*min = GFX_FILTERING_NEAREST;
		*mip = GFX_FILTERING_NONE;
		break;
	case GL_LINEAR:
		*min = GFX_FILTERING_LINEAR;
		*mip = GFX_FILTERING_NONE;
		break;
	case GL_NEAREST_MIPMAP_NEAREST:
		*min = GFX_FILTERING_NEAREST;
		*mip = GFX_FILTERING_NEAREST;
		break;
	case GL_LINEAR_MIPMAP_NEAREST:
		*min = GFX_FILTERING_LINEAR;
		*mip = GFX_FILTERING_NEAREST;
		break;
	case GL_NEAREST_MIPMAP_LINEAR:
		*min = GFX_FILTERING_NEAREST;
		*mip = GFX_FILTERING_LINEAR;
		break;
	default:
		*min = GFX_FILTERING_LINEAR;
		*mip = GFX_FILTERING_LINEAR;
		break;
	}
}

/*
===============
GfxGL_FlushStaging

Makes the gfx texture of the staged levels, with the parameters set since, and empties the staging
===============
*/
static void GfxGL_FlushStaging( void )
{
	gfxGlStaging_t *staging = &gfxGl.staging;
	gfxGlTexture_t *texture;
	gfxGlSampling_t sampling;
	struct gfx_texture_create_info info;
	struct gfx_texture *made;
	enum gfx_filtering minFiltering, mipFiltering;
	int level, width, height;

	if ( !staging->name ) {
		return;
	}
	texture = &gfxGl.textures[staging->name];
	staging->name = 0;
	if ( !gfxGl.device || !texture->live || !staging->levels ) {
		return;
	}

	GfxGL_CurrentSampling( texture, &sampling );
	GfxGL_MinFiltering( sampling.minFilter, &minFiltering, &mipFiltering );
	if ( staging->levels == 1 ) {
		// GL would call a mipmapped texture without its levels incomplete; level 0 it is
		mipFiltering = GFX_FILTERING_NONE;
	}

	Com_Memset( &info, 0, sizeof( info ) );
	info.type = GFX_TEXTURE_2D;
	info.usage = GFX_TEXTURE_SAMPLED;
	info.format = GFX_FORMAT_R8G8B8A8_UNORM;
	info.levels = (uint8_t)staging->levels;
	info.width = (uint32_t)texture->width;
	info.height = (uint32_t)texture->height;
	info.depth = 1;
	info.addressing_s = GfxGL_Addressing( sampling.wrapS );
	info.addressing_t = GfxGL_Addressing( sampling.wrapT );
	info.addressing_r = GfxGL_Addressing( sampling.wrapT );
	info.min_filtering = minFiltering;
	info.mag_filtering = sampling.magFilter == GL_NEAREST ? GFX_FILTERING_NEAREST : GFX_FILTERING_LINEAR;
	info.mip_filtering = mipFiltering;
	info.anisotropy = sampling.anisotropy;
	if ( !gfx_create_texture( gfxGl.device, &info, &made ) ) {
		ri.Printf( PRINT_WARNING, "gfx: cannot create a %dx%d texture\n", texture->width, texture->height );
		return;
	}
	for ( level = 0; level < staging->levels; level++ ) {
		GfxGL_LevelSize( texture->width, texture->height, level, &width, &height );
		gfx_set_texture_data( gfxGl.device, made, (uint8_t)level, 0, (uint32_t)width, (uint32_t)height, 1,
			(uint32_t)( width * height * 4 ), staging->buffer + staging->levelOffsets[level] );
	}
	texture->texture = made;
	texture->made = sampling;
}

static enum gfx_blend_function GfxGL_BlendFunction( GLenum factor )
{
	switch ( factor ) {
	case GL_ZERO:					return GFX_BLEND_ZERO;
	case GL_ONE:					return GFX_BLEND_ONE;
	case GL_SRC_COLOR:				return GFX_BLEND_SRC_COLOR;
	case GL_ONE_MINUS_SRC_COLOR:	return GFX_BLEND_ONE_MINUS_SRC_COLOR;
	case GL_DST_COLOR:				return GFX_BLEND_DST_COLOR;
	case GL_ONE_MINUS_DST_COLOR:	return GFX_BLEND_ONE_MINUS_DST_COLOR;
	case GL_DST_ALPHA:				return GFX_BLEND_DST_ALPHA;
	case GL_ONE_MINUS_DST_ALPHA:	return GFX_BLEND_ONE_MINUS_DST_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA:	return GFX_BLEND_ONE_MINUS_SRC_ALPHA;
	// GL_SRC_ALPHA, and GL_SRC_ALPHA_SATURATE, which gfx has no factor for (GFX_STATUS.md, gfx
	// gaps): min(As, 1 - Ad) is As wherever the destination alpha is 0
	default:						return GFX_BLEND_SRC_ALPHA;
	}
}

static enum gfx_compare_function GfxGL_CompareFunction( GLenum func )
{
	switch ( func ) {
	case GL_NEVER:		return GFX_CMP_NEVER;
	case GL_LESS:		return GFX_CMP_LOWER;
	case GL_LEQUAL:		return GFX_CMP_LEQUAL;
	case GL_EQUAL:		return GFX_CMP_EQUAL;
	case GL_GEQUAL:		return GFX_CMP_GEQUAL;
	case GL_GREATER:	return GFX_CMP_GREATER;
	case GL_NOTEQUAL:	return GFX_CMP_NOTEQUAL;
	default:			return GFX_CMP_ALWAYS;
	}
}

static enum gfx_stencil_operation GfxGL_StencilOperation( GLenum op )
{
	switch ( op ) {
	case GL_ZERO:		return GFX_STENCIL_ZERO;
	case GL_REPLACE:	return GFX_STENCIL_REPLACE;
	case GL_INCR:		return GFX_STENCIL_INC;
	case GL_DECR:		return GFX_STENCIL_DEC;
	case GL_INVERT:		return GFX_STENCIL_INV;
	default:			return GFX_STENCIL_KEEP;
	}
}

/*
===============
GfxGL_PipelineKey

The pipeline GL's current state maps to
===============
*/
static void GfxGL_PipelineKey( gfxPrimitiveKey_t primitive, gfxPipelineKey_t *key )
{
	unsigned caps = gfxGl.caps;
	byte colorMask = GFX_COLOR_MASK_NONE;

	Com_Memset( key, 0, sizeof( *key ) );
	key->primitive = (byte)primitive;

	if ( gfxGl.colorMask[0] ) colorMask |= GFX_COLOR_MASK_R;
	if ( gfxGl.colorMask[1] ) colorMask |= GFX_COLOR_MASK_G;
	if ( gfxGl.colorMask[2] ) colorMask |= GFX_COLOR_MASK_B;
	if ( gfxGl.colorMask[3] ) colorMask |= GFX_COLOR_MASK_A;
	key->blend.colorMask = colorMask;
	if ( caps & GFX_GL_CAP_BLEND ) {
		key->blend.enabled = 1;
		key->blend.src = (byte)GfxGL_BlendFunction( gfxGl.blendSrc );
		key->blend.dst = (byte)GfxGL_BlendFunction( gfxGl.blendDst );
	}

	key->depthStencil.depthTest = ( caps & GFX_GL_CAP_DEPTH_TEST ) != 0;
	key->depthStencil.depthWrite = gfxGl.depthMask != qfalse;
	key->depthStencil.depthFunc = (byte)GfxGL_CompareFunction( gfxGl.depthFunc );
	if ( caps & GFX_GL_CAP_STENCIL_TEST ) {
		key->depthStencil.stencilEnabled = 1;
		key->depthStencil.stencilFunc = (byte)GfxGL_CompareFunction( gfxGl.stencilFunc );
		key->depthStencil.stencilRef = (byte)gfxGl.stencilRef;
		key->depthStencil.stencilFuncMask = (byte)gfxGl.stencilFuncMask;
		key->depthStencil.stencilWriteMask = (byte)gfxGl.stencilWriteMask;
		key->depthStencil.stencilFail = (byte)GfxGL_StencilOperation( gfxGl.stencilOp[0] );
		key->depthStencil.stencilZFail = (byte)GfxGL_StencilOperation( gfxGl.stencilOp[1] );
		key->depthStencil.stencilPass = (byte)GfxGL_StencilOperation( gfxGl.stencilOp[2] );
	}

	if ( !( caps & GFX_GL_CAP_CULL_FACE ) ) {
		key->rasterizer.cull = GFX_CULL_NONE;
	} else {
		key->rasterizer.cull = gfxGl.cullFace == GL_FRONT ? GFX_CULL_FRONT : GFX_CULL_BACK;
	}
	switch ( gfxGl.polygonMode ) {
	case GL_LINE:
		key->rasterizer.fill = GFX_FILL_LINE;
		break;
	case GL_POINT:
		key->rasterizer.fill = GFX_FILL_POINT;
		break;
	default:
		key->rasterizer.fill = GFX_FILL_SOLID;
		break;
	}
	key->rasterizer.scissor = ( caps & GFX_GL_CAP_SCISSOR_TEST ) != 0;
}

static float GfxGL_TexEnv( int unit )
{
	// unit 1 only exists with GL_ARB_multitexture (r_ext_multitexture)
	if ( !gfxGl.texture2D[unit] || ( unit > 0 && !qglActiveTextureARB ) ) {
		return GFX_TEX_ENV_OFF;
	}
	switch ( gfxGl.texEnv[unit] ) {
	case GL_REPLACE:	return GFX_TEX_ENV_REPLACE;
	case GL_DECAL:		return GFX_TEX_ENV_DECAL;
	case GL_ADD:		return GFX_TEX_ENV_ADD;
	default:			return GFX_TEX_ENV_MODULATE;
	}
}

/*
===============
GfxGL_Uniforms

The shaders' uniforms for GL's current state: the MVP, the alpha test and texture environments,
the clip plane in the vertices' coordinates, polygon offset as a depth bias
===============
*/
static void GfxGL_Uniforms( gfxUniforms_t *uniforms )
{
	const float *m = gfxGl.modelview.top;
	const float *p = gfxGl.clipPlane0;
	float alphaTest = GFX_ALPHA_TEST_NONE;
	int col;

	if ( !gfxGl.mvpValid ) {
		GfxGL_MatrixProduct( gfxGl.projection.top, gfxGl.modelview.top, gfxGl.mvp );
		gfxGl.mvpValid = qtrue;
	}
	Com_Memcpy( uniforms->mvp, gfxGl.mvp, sizeof( uniforms->mvp ) );

	if ( gfxGl.caps & GFX_GL_CAP_ALPHA_TEST ) {
		switch ( gfxGl.alphaFunc ) {
		case GL_GREATER:	alphaTest = GFX_ALPHA_TEST_GREATER; break;
		case GL_LESS:		alphaTest = GFX_ALPHA_TEST_LESS; break;
		case GL_GEQUAL:		alphaTest = GFX_ALPHA_TEST_GEQUAL; break;
		default:			break;
		}
	}
	uniforms->params[0] = alphaTest;
	uniforms->params[1] = gfxGl.alphaRef;
	uniforms->params[2] = GfxGL_TexEnv( 0 );
	uniforms->params[3] = GfxGL_TexEnv( 1 );

	// the eye-space plane in the vertices' coordinates: dot( plane, modelview * v ) is
	// dot( plane * modelview, v )
	if ( gfxGl.caps & GFX_GL_CAP_CLIP_PLANE0 ) {
		for ( col = 0; col < 4; col++ ) {
			uniforms->clipPlane[col] = p[0] * m[col * 4 + 0] + p[1] * m[col * 4 + 1]
				+ p[2] * m[col * 4 + 2] + p[3] * m[col * 4 + 3];
		}
	} else {
		uniforms->clipPlane[0] = uniforms->clipPlane[1] = uniforms->clipPlane[2] = 0.0f;
		uniforms->clipPlane[3] = 1.0f;
	}

	uniforms->depth[0] = 0.0f;
	if ( ( gfxGl.caps & GFX_GL_CAP_POLYGON_OFFSET_FILL ) && gfxGl.polygonMode == GL_FILL ) {
		uniforms->depth[0] = ( gfxGl.polygonOffset[0] * GFX_GL_POLYGON_OFFSET_SLOPE + gfxGl.polygonOffset[1] )
			* GFX_GL_NDC_DEPTH_UNIT;
	}
	uniforms->depth[1] = gfxGl.depthZeroToOne ? 1.0f : 0.0f;
	uniforms->depth[2] = 0.0f;
	uniforms->depth[3] = 0.0f;
}

/*
===============
GfxGL_Submit

Draws vertices with GL's current state: indexed by indices (relative to the first vertex) when
numIndices is not 0, else in order
===============
*/
static void GfxGL_Submit( gfxPrimitiveKey_t primitive, const gfxVertex_t *vertices, int numVertices, uint32_t *indices, int numIndices )
{
	gfxDrawState_t state;
	gfxGlTexture_t *texture;
	int unit;

	GfxGL_FlushStaging();
	GfxGL_PipelineKey( primitive, &state.key );
	GfxGL_Uniforms( &state.uniforms );
	for ( unit = 0; unit < GFX_GL_TEXTURE_UNITS; unit++ ) {
		texture = GfxGL_BoundTexture( unit );
		state.textures[unit] = texture ? texture->texture : NULL;
	}
	state.lineWidth = gfxGl.lineWidth;
	GfxDraw_Draw( &state, vertices, numVertices, numIndices ? indices : NULL, numIndices );
}

/*
===============
GfxGL_GatherArrays

Writes array elements first .. first + count - 1 of the enabled client arrays as vertices (a
disabled color or texture coordinate array gives the current glColor / glTexCoord, as in GL).
renderergl1's arrays are GL_FLOAT positions and texture coordinates and GL_UNSIGNED_BYTE colors;
an array of another type counts as disabled. qfalse without a vertex array.
===============
*/
static qboolean GfxGL_GatherArrays( int first, int count, gfxVertex_t *out )
{
	const gfxGlClientArray_t *xyz = &gfxGl.vertexArray;
	const gfxGlClientArray_t *color = &gfxGl.colorArray;
	const gfxGlClientArray_t *st;
	const byte *ptr;
	float *dst;
	int xyzStride, colorStride, stStride[GFX_GL_TEXTURE_UNITS];
	int xyzSize, colorSize;
	int i, element, unit, c;
	qboolean haveColor, haveSt[GFX_GL_TEXTURE_UNITS];

	if ( !xyz->enabled || !xyz->ptr || xyz->type != GL_FLOAT ) {
		return qfalse;
	}
	xyzSize = MIN( xyz->size, 3 );
	xyzStride = xyz->stride > 0 ? xyz->stride : xyz->size * (int)sizeof( float );
	haveColor = color->enabled && color->ptr && color->type == GL_UNSIGNED_BYTE;
	colorSize = MIN( color->size, 4 );
	colorStride = color->stride > 0 ? color->stride : color->size;
	for ( unit = 0; unit < GFX_GL_TEXTURE_UNITS; unit++ ) {
		st = &gfxGl.texCoordArrays[unit];
		haveSt[unit] = st->enabled && st->ptr && st->type == GL_FLOAT;
		stStride[unit] = st->stride > 0 ? st->stride : st->size * (int)sizeof( float );
	}

	for ( i = 0; i < count; i++, out++ ) {
		element = first + i;

		ptr = (const byte *)xyz->ptr + element * xyzStride;
		out->xyz[0] = out->xyz[1] = out->xyz[2] = 0.0f;
		for ( c = 0; c < xyzSize; c++ ) {
			out->xyz[c] = ( (const float *)ptr )[c];
		}

		if ( haveColor ) {
			ptr = (const byte *)color->ptr + element * colorStride;
			out->rgba[0] = out->rgba[1] = out->rgba[2] = 0;
			out->rgba[3] = 255;
			for ( c = 0; c < colorSize; c++ ) {
				out->rgba[c] = ptr[c];
			}
		} else {
			Com_Memcpy( out->rgba, gfxGl.currentRgba, sizeof( out->rgba ) );
		}

		for ( unit = 0; unit < GFX_GL_TEXTURE_UNITS; unit++ ) {
			dst = unit ? out->st1 : out->st0;
			if ( haveSt[unit] ) {
				st = &gfxGl.texCoordArrays[unit];
				ptr = (const byte *)st->ptr + element * stStride[unit];
				dst[0] = ( (const float *)ptr )[0];
				dst[1] = st->size > 1 ? ( (const float *)ptr )[1] : 0.0f;
			} else {
				dst[0] = gfxGl.currentSt[unit][0];
				dst[1] = gfxGl.currentSt[unit][1];
			}
		}
	}
	return qtrue;
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

Fixed-function state: recorded, read by the draws (GfxGL_PipelineKey, GfxGL_Uniforms)

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
GfxGL_InvertMatrix

The inverse of a column-major matrix (cofactors over the determinant); qfalse when singular
===============
*/
static qboolean GfxGL_InvertMatrix( const float *m, float *out )
{
	float inv[16], det;
	int i;

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if ( det == 0.0f ) {
		return qfalse;
	}
	for ( i = 0; i < 16; i++ ) {
		out[i] = inv[i] / det;
	}
	return qtrue;
}

/*
===============
qglClipPlane

GL keeps the plane in eye coordinates: times the inverse of the modelview it is set under
(s_flipMatrix in RB_BeginDrawingView); the draws turn it into their vertices' coordinates
(GfxGL_Uniforms)
===============
*/
void APIENTRY qglClipPlane( GLenum plane, const GLdouble *equation )
{
	float inverse[16];
	int col;

	if ( plane != GL_CLIP_PLANE0 ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	if ( !GfxGL_InvertMatrix( gfxGl.modelview.top, inverse ) ) {
		GfxGL_SetError( GL_INVALID_OPERATION );
		return;
	}
	for ( col = 0; col < 4; col++ ) {
		gfxGl.clipPlane0[col] = (float)( equation[0] * inverse[col * 4 + 0] + equation[1] * inverse[col * 4 + 1]
			+ equation[2] * inverse[col * 4 + 2] + equation[3] * inverse[col * 4 + 3] );
	}
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
As in GL the clear obeys the color and depth write masks and the scissor test, which gfx takes
from the bound pipeline: one with GL's current masks is bound first (its depth test always
passes, as gfx only applies the depth mask with the test on).
===============
*/
void APIENTRY qglClear( GLbitfield mask )
{
	struct gfx_clear_color color;
	gfxPipelineKey_t key;

	if ( !gfxGl.device ) {
		return;
	}
	// GL's color mask, depth mask and scissor test; nothing else of the pipeline acts on a clear
	GfxGL_PipelineKey( GFX_PRIMITIVE_KEY_TRIANGLES, &key );
	key.blend.enabled = 0;
	key.blend.src = 0;
	key.blend.dst = 0;
	Com_Memset( &key.depthStencil, 0, sizeof( key.depthStencil ) );
	key.depthStencil.depthTest = 1;
	key.depthStencil.depthWrite = gfxGl.depthMask != qfalse;
	key.depthStencil.depthFunc = GFX_CMP_ALWAYS;
	key.rasterizer.cull = GFX_CULL_NONE;
	key.rasterizer.fill = GFX_FILL_SOLID;
	GfxDraw_BindState( &key );

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

	GfxGL_MatrixProduct( a, b, out );
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

Textures: glTexImage2D's levels are staged, then made into one gfx texture (GfxGL_FlushStaging)

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
		if ( gfxGl.staging.name == name ) {
			gfxGl.staging.name = 0;
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
	// the staged texture's parameters are all set once another texture is bound
	if ( gfxGl.staging.name && gfxGl.staging.name != texture ) {
		GfxGL_FlushStaging();
	}
	if ( texture >= (GLuint)gfxGl.numTextures || !gfxGl.textures[texture].live ) {
		GfxGL_SetError( GL_INVALID_VALUE );
		return;
	}
	gfxGl.boundTextures[gfxGl.activeTexture] = texture;
}

void APIENTRY qglTexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	gfxGlTexture_t *texture = GfxGL_BoundTexture( gfxGl.activeTexture );
	gfxGlSampling_t sampling;

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
		return;
	}

	// a gfx texture keeps the sampling it was made with (GFX_STATUS.md, gfx gaps): a change after
	// that (r_textureMode at run time) reaches the images loaded after the next vid_restart
	if ( texture->texture && !gfxGl.warnedResample ) {
		GfxGL_CurrentSampling( texture, &sampling );
		if ( memcmp( &sampling, &texture->made, sizeof( sampling ) ) ) {
			gfxGl.warnedResample = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: texture filtering changes apply after a vid_restart\n" );
		}
	}
}

void APIENTRY qglTexParameteri( GLenum target, GLenum pname, GLint param )
{
	qglTexParameterf( target, pname, (GLfloat)param );
}

/*
===============
qglTexImage2D

renderergl1 always uploads RGBA bytes (Upload32, RE_UploadCinematic). The level goes into the
staging buffer, reduced to the internal format; a new level 0 starts the texture over, and a
texture that has its gfx texture already gets a new one (GL re-specifies it).
===============
*/
void APIENTRY qglTexImage2D( GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	gfxGlStaging_t *staging = &gfxGl.staging;
	GLuint name = gfxGl.boundTextures[gfxGl.activeTexture];
	gfxGlTexture_t *texture = GfxGL_BoundTexture( gfxGl.activeTexture );
	byte *grown;
	int size, offset;

	if ( format != GL_RGBA || type != GL_UNSIGNED_BYTE || level < 0 || width <= 0 || height <= 0 ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	if ( level >= GFX_GL_MAX_LEVELS ) {
		GfxGL_SetError( GL_INVALID_VALUE );
		return;
	}
	if ( name == 0 || !texture ) {
		return;
	}
	if ( staging->name && staging->name != name ) {
		GfxGL_FlushStaging();
	}
	if ( texture->texture && gfxGl.device ) {
		gfx_delete_texture( gfxGl.device, texture->texture );
	}
	texture->texture = NULL;

	if ( staging->name != name ) {
		staging->name = name;
		staging->levels = 0;
		staging->used = 0;
	}
	if ( level == 0 ) {
		texture->width = width;
		texture->height = height;
		texture->internalFormat = (GLenum)internalFormat;
		staging->levels = 0;
		staging->used = 0;
	}
	if ( level > staging->levels ) {
		// a level past the next one: GL keeps it, renderergl1 never gives one
		GfxGL_SetError( GL_INVALID_OPERATION );
		return;
	}

	// level n replaces the levels from n on
	size = width * height * 4;
	offset = level < staging->levels ? staging->levelOffsets[level] : staging->used;
	if ( offset + size > staging->size ) {
		grown = ri.Malloc( offset + size );
		if ( staging->buffer ) {
			Com_Memcpy( grown, staging->buffer, offset );
			ri.Free( staging->buffer );
		}
		staging->buffer = grown;
		staging->size = offset + size;
	}
	if ( pixels ) {
		Com_Memcpy( staging->buffer + offset, pixels, size );
		GfxGL_ReduceToFormat( texture->internalFormat, staging->buffer + offset, width * height );
	} else {
		Com_Memset( staging->buffer + offset, 0, size );
	}
	staging->levelOffsets[level] = offset;
	staging->levels = level + 1;
	staging->used = offset + size;
}

/*
===============
qglTexSubImage2D

Into the staging while the texture is staged; else whole rows from yoffset into the gfx texture,
which is what RE_UploadCinematic sends (gfx uploads whole rows: its offset is the first row)
===============
*/
void APIENTRY qglTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
	gfxGlStaging_t *staging = &gfxGl.staging;
	gfxGlTexture_t *texture = GfxGL_BoundTexture( gfxGl.activeTexture );
	int levelWidth, levelHeight, row, size;
	byte *dst;

	if ( format != GL_RGBA || type != GL_UNSIGNED_BYTE || level < 0 || width <= 0 || height <= 0 ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	if ( !texture ) {
		return;
	}
	GfxGL_LevelSize( texture->width, texture->height, level, &levelWidth, &levelHeight );
	if ( xoffset < 0 || yoffset < 0 || xoffset + width > levelWidth || yoffset + height > levelHeight ) {
		GfxGL_SetError( GL_INVALID_VALUE );
		return;
	}

	if ( staging->name == gfxGl.boundTextures[gfxGl.activeTexture] && level < staging->levels ) {
		// not made yet: into the staged level, row by row
		for ( row = 0; row < height; row++ ) {
			dst = staging->buffer + staging->levelOffsets[level] + ( ( yoffset + row ) * levelWidth + xoffset ) * 4;
			Com_Memcpy( dst, (const byte *)pixels + row * width * 4, width * 4 );
			GfxGL_ReduceToFormat( texture->internalFormat, dst, width );
		}
		return;
	}
	if ( !texture->texture || !gfxGl.device ) {
		return;
	}
	if ( xoffset != 0 || width != levelWidth ) {
		if ( !gfxGl.warnedSubImage ) {
			gfxGl.warnedSubImage = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: glTexSubImage2D of part of a row is not supported\n" );
		}
		return;
	}

	// the texels reduced to the format in the staging buffer (nothing is staged now)
	size = width * height * 4;
	if ( texture->internalFormat != GL_RGBA && texture->internalFormat != GL_RGBA8 ) {
		if ( staging->name ) {
			GfxGL_FlushStaging();
		}
		if ( size > staging->size ) {
			if ( staging->buffer ) {
				ri.Free( staging->buffer );
			}
			staging->buffer = ri.Malloc( size );
			staging->size = size;
		}
		Com_Memcpy( staging->buffer, pixels, size );
		GfxGL_ReduceToFormat( texture->internalFormat, staging->buffer, width * height );
		pixels = staging->buffer;
	}
	gfx_set_texture_data( gfxGl.device, texture->texture, (uint8_t)level, (uint32_t)yoffset,
		(uint32_t)width, (uint32_t)height, 1, (uint32_t)size, pixels );
}

/*
==============================================================================

Draws

==============================================================================
*/

/*
===============
qglDrawElements

renderergl1 draws GL_TRIANGLES lists of GL_INDEX_TYPE indices from the client arrays
(r_primitives 2, the default with compiled vertex arrays). Only the vertices the indices use go to
gfx - all of the locked range when the arrays are locked.
===============
*/
void APIENTRY qglDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	uint32_t *list = gfxGlDrawIndices;
	uint32_t minIndex, maxIndex;
	int i, first, numVertices;

	if ( mode != GL_TRIANGLES ) {
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}
	if ( count <= 0 || !indices ) {
		return;
	}
	if ( count > GFX_GL_DRAW_INDICES ) {
		if ( !gfxGl.warnedDrawRange ) {
			gfxGl.warnedDrawRange = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: a draw of more than %d indices or %d vertices dropped\n",
				GFX_GL_DRAW_INDICES, GFX_GL_DRAW_VERTICES );
		}
		return;
	}

	switch ( type ) {
	case GL_UNSIGNED_INT:
		Com_Memcpy( list, indices, count * sizeof( uint32_t ) );
		break;
	case GL_UNSIGNED_SHORT:
		for ( i = 0; i < count; i++ ) {
			list[i] = ( (const unsigned short *)indices )[i];
		}
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}

	minIndex = maxIndex = list[0];
	for ( i = 1; i < count; i++ ) {
		if ( list[i] < minIndex ) {
			minIndex = list[i];
		} else if ( list[i] > maxIndex ) {
			maxIndex = list[i];
		}
	}
	if ( gfxGl.locked && gfxGl.lockedFirst >= 0 && (uint32_t)gfxGl.lockedFirst <= minIndex
		&& maxIndex < (uint32_t)( gfxGl.lockedFirst + gfxGl.lockedCount ) ) {
		first = gfxGl.lockedFirst;
		numVertices = gfxGl.lockedCount;
	} else {
		first = (int)minIndex;
		numVertices = (int)( maxIndex - minIndex ) + 1;
	}
	if ( numVertices > GFX_GL_DRAW_VERTICES ) {
		if ( !gfxGl.warnedDrawRange ) {
			gfxGl.warnedDrawRange = qtrue;
			ri.Printf( PRINT_WARNING, "gfx: a draw of more than %d indices or %d vertices dropped\n",
				GFX_GL_DRAW_INDICES, GFX_GL_DRAW_VERTICES );
		}
		return;
	}
	for ( i = 0; i < count; i++ ) {
		list[i] -= (uint32_t)first;
	}

	if ( !GfxGL_GatherArrays( first, numVertices, gfxGlDrawVertices ) ) {
		return;
	}
	GfxGL_Submit( GFX_PRIMITIVE_KEY_TRIANGLES, gfxGlDrawVertices, numVertices, list, count );
}

static qboolean GfxGL_ImmediateFull( void )
{
	if ( gfxGl.numImmediate < GFX_GL_IMMEDIATE_VERTICES ) {
		return qfalse;
	}
	if ( !gfxGl.warnedImmediate ) {
		gfxGl.warnedImmediate = qtrue;
		ri.Printf( PRINT_WARNING, "gfx: more than %d vertices between glBegin and glEnd, the rest dropped\n",
			GFX_GL_IMMEDIATE_VERTICES );
	}
	return qtrue;
}

/*
===============
qglArrayElement

glArrayElement inside glBegin / glEnd (r_primitives 1 and 3): element i of the client arrays as
an immediate vertex
===============
*/
void APIENTRY qglArrayElement( GLint i )
{
	if ( i < 0 || !gfxGl.inBegin || GfxGL_ImmediateFull() ) {
		return;
	}
	if ( GfxGL_GatherArrays( i, 1, &gfxGl.immediate[gfxGl.numImmediate] ) ) {
		gfxGl.numImmediate++;
	}
}

/*
==============================================================================

Immediate mode: the vertices are collected, glEnd draws them

==============================================================================
*/

void APIENTRY qglBegin( GLenum mode )
{
	gfxGl.inBegin = qtrue;
	gfxGl.immediateMode = mode;
	gfxGl.numImmediate = 0;
}

/*
===============
qglEnd

The glBegin block's vertices as one draw - triangle strips, fans, polygons and quads as indexed
triangle lists, line strips as line lists
===============
*/
void APIENTRY qglEnd( void )
{
	uint32_t *list = gfxGlDrawIndices;
	gfxPrimitiveKey_t primitive;
	int n = gfxGl.numImmediate;
	int i, numIndices = 0;

	if ( !gfxGl.inBegin ) {
		return;
	}
	gfxGl.inBegin = qfalse;
	gfxGl.numImmediate = 0;

	switch ( gfxGl.immediateMode ) {
	case GL_TRIANGLES:
		primitive = GFX_PRIMITIVE_KEY_TRIANGLES;
		break;
	case GL_LINES:
		primitive = GFX_PRIMITIVE_KEY_LINES;
		break;
	case GL_POINTS:
		primitive = GFX_PRIMITIVE_KEY_POINTS;
		break;
	case GL_TRIANGLE_STRIP:
		// GL's strip winding: odd triangles swap their first two vertices
		for ( i = 2; i < n; i++ ) {
			list[numIndices++] = ( i & 1 ) ? i - 1 : i - 2;
			list[numIndices++] = ( i & 1 ) ? i - 2 : i - 1;
			list[numIndices++] = i;
		}
		primitive = GFX_PRIMITIVE_KEY_TRIANGLES;
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		for ( i = 2; i < n; i++ ) {
			list[numIndices++] = 0;
			list[numIndices++] = i - 1;
			list[numIndices++] = i;
		}
		primitive = GFX_PRIMITIVE_KEY_TRIANGLES;
		break;
	case GL_QUADS:
		for ( i = 0; i + 3 < n; i += 4 ) {
			list[numIndices++] = i;
			list[numIndices++] = i + 1;
			list[numIndices++] = i + 2;
			list[numIndices++] = i;
			list[numIndices++] = i + 2;
			list[numIndices++] = i + 3;
		}
		primitive = GFX_PRIMITIVE_KEY_TRIANGLES;
		break;
	case GL_LINE_STRIP:
		for ( i = 1; i < n; i++ ) {
			list[numIndices++] = i - 1;
			list[numIndices++] = i;
		}
		primitive = GFX_PRIMITIVE_KEY_LINES;
		break;
	default:
		GfxGL_SetError( GL_INVALID_ENUM );
		return;
	}

	if ( numIndices ) {
		GfxGL_Submit( primitive, gfxGl.immediate, n, list, numIndices );
	} else if ( gfxGl.immediateMode == GL_TRIANGLES || gfxGl.immediateMode == GL_LINES || gfxGl.immediateMode == GL_POINTS ) {
		GfxGL_Submit( primitive, gfxGl.immediate, n, NULL, 0 );
	}
}

static void GfxGL_ImmediateVertex( float x, float y, float z )
{
	gfxVertex_t *vertex;

	if ( GfxGL_ImmediateFull() ) {
		return;
	}
	vertex = &gfxGl.immediate[gfxGl.numImmediate++];
	VectorSet( vertex->xyz, x, y, z );
	Com_Memcpy( vertex->rgba, gfxGl.currentRgba, sizeof( vertex->rgba ) );
	vertex->st0[0] = gfxGl.currentSt[0][0];
	vertex->st0[1] = gfxGl.currentSt[0][1];
	vertex->st1[0] = gfxGl.currentSt[1][0];
	vertex->st1[1] = gfxGl.currentSt[1][1];
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
