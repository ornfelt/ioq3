/*
gfx_shader.c - loads renderer_gfx's compiled gfx shaders (.gfx files, made from
shaders/*.gfxs by shaders/generate_shaders.ps1 / .sh) and links them into gfx shader states. New
file, no renderergl1 counterpart: the port of ioq3-rs's code/gfx/gfx_shader.rs, itself modelled
on gfx_dll/rs/gfx_demo's shader loader.

A .gfx file is a packed header (magic, the input / output / struct / sampler / constant counts,
then a code length and offset per device backend), the declaration tables, and one code blob per
backend it was compiled for (GLSL text for gl3 / gl4 / gles3, HLSL text for d3d11, SPIR-V for
vk). d3d12 runs d3d11's HLSL when the file has no d3d12 blob, as gfx_demo does.

The files are read from shaders/<dialect>/ next to the client (the post-build copy of
cmake/renderer_gfx.cmake), then under fs_basepath. Load time only: this file allocates.
*/

#include "tr_local.h"
#include "gfx_shader.h"

#include <stdio.h>

// sys_main.c's folder of the running executable (renderer_gfx is linked into the client, so the
// engine's function is called directly; sys_local.h is not the renderer's to include)
char *Sys_BinaryPath( void );

// GFX_MAGIC('G', 'F', 'X', 'V') / 'F': the first u32 of a vertex / fragment .gfx file
#define GFX_SHADER_MAGIC_VERTEX		0x47465856
#define GFX_SHADER_MAGIC_FRAGMENT	0x47465846

// the record sizes of gfx's shader definitions (all packed)
#define GFX_SHADER_NAME_SIZE		64
#define GFX_SHADER_MAX_MEMBERS		64
#define GFX_SHADER_CODE_SLOTS		8
#define GFX_SHADER_HEADER_SIZE		( 4 + 5 + 3 + 4 * GFX_SHADER_CODE_SLOTS * 2 )
#define GFX_SHADER_INPUT_SIZE		( GFX_SHADER_NAME_SIZE + 2 )
#define GFX_SHADER_OUTPUT_SIZE		( GFX_SHADER_NAME_SIZE + 2 )
#define GFX_SHADER_STRUCT_SIZE		( GFX_SHADER_NAME_SIZE + GFX_SHADER_MAX_MEMBERS * ( GFX_SHADER_NAME_SIZE + 4 ) + 4 )
#define GFX_SHADER_SAMPLER_SIZE		( GFX_SHADER_NAME_SIZE + 4 )
#define GFX_SHADER_CONSTANT_SIZE	( GFX_SHADER_NAME_SIZE + GFX_SHADER_MAX_MEMBERS * ( GFX_SHADER_NAME_SIZE + 4 ) + 4 + 4 )

// the attribute / constant / sampler bindings one program links with (generic has 4 / 1 / 2)
#define GFX_SHADER_MAX_BINDINGS		32
// a sampler array element's name: the array's name and "[i]"
#define GFX_SHADER_BINDING_NAME		( GFX_SHADER_NAME_SIZE + 8 )

typedef struct {
	char		name[GFX_SHADER_BINDING_NAME];
	uint32_t	bind;
} gfxShaderBinding_t;

// what renderer_gfx needs of one parsed .gfx file
typedef struct {
	byte				*data;
	int					size;
	gfxShaderBinding_t	inputs[GFX_SHADER_MAX_BINDINGS];
	int					numInputs;
	gfxShaderBinding_t	samplers[GFX_SHADER_MAX_BINDINGS];	// arrays expanded to name[i]
	int					numSamplers;
	gfxShaderBinding_t	constants[GFX_SHADER_MAX_BINDINGS];
	int					numConstants;
	uint32_t			codeLengths[GFX_SHADER_CODE_SLOTS];
	uint32_t			codeOffsets[GFX_SHADER_CODE_SLOTS];
} gfxShaderFile_t;

static uint32_t GfxShader_ReadU32( const byte *data )
{
	return (uint32_t)data[0] | ( (uint32_t)data[1] << 8 ) | ( (uint32_t)data[2] << 16 ) | ( (uint32_t)data[3] << 24 );
}

static void GfxShader_ReadName( const byte *data, char *name )
{
	int len;

	for ( len = 0; len < GFX_SHADER_NAME_SIZE && data[len]; len++ ) {
		name[len] = (char)data[len];
	}
	name[len] = '\0';
}

/*
===============
GfxShader_Parse

Reads the header and the declaration tables of a .gfx file already in file->data
===============
*/
static qboolean GfxShader_Parse( gfxShaderFile_t *file, uint32_t magic, char *error, int errorSize )
{
	const byte *data = file->data;
	int inputs, outputs, structs, samplers, constants;
	int inputsAt, outputsAt, structsAt, samplersAt, constantsAt;
	int i, j, at, arraySize;
	gfxShaderBinding_t *binding;
	char name[GFX_SHADER_NAME_SIZE + 1];

	if ( file->size < GFX_SHADER_HEADER_SIZE ) {
		Com_sprintf( error, errorSize, "too small for the header" );
		return qfalse;
	}
	if ( GfxShader_ReadU32( data ) != magic ) {
		Com_sprintf( error, errorSize, "bad magic 0x%08x", GfxShader_ReadU32( data ) );
		return qfalse;
	}
	inputs = data[4];
	outputs = data[5];
	structs = data[6];
	samplers = data[7];
	constants = data[8];
	for ( i = 0; i < GFX_SHADER_CODE_SLOTS; i++ ) {
		file->codeLengths[i] = GfxShader_ReadU32( data + 12 + i * 4 );
		file->codeOffsets[i] = GfxShader_ReadU32( data + 12 + ( GFX_SHADER_CODE_SLOTS + i ) * 4 );
	}

	inputsAt = GFX_SHADER_HEADER_SIZE;
	outputsAt = inputsAt + inputs * GFX_SHADER_INPUT_SIZE;
	structsAt = outputsAt + outputs * GFX_SHADER_OUTPUT_SIZE;
	samplersAt = structsAt + structs * GFX_SHADER_STRUCT_SIZE;
	constantsAt = samplersAt + samplers * GFX_SHADER_SAMPLER_SIZE;
	if ( file->size < constantsAt + constants * GFX_SHADER_CONSTANT_SIZE ) {
		Com_sprintf( error, errorSize, "too small for its declarations" );
		return qfalse;
	}
	if ( inputs > GFX_SHADER_MAX_BINDINGS || constants > GFX_SHADER_MAX_BINDINGS ) {
		Com_sprintf( error, errorSize, "more than %d inputs or constants", GFX_SHADER_MAX_BINDINGS );
		return qfalse;
	}

	for ( i = 0; i < inputs; i++ ) {
		at = inputsAt + i * GFX_SHADER_INPUT_SIZE;
		binding = &file->inputs[file->numInputs++];
		GfxShader_ReadName( data + at, binding->name );
		binding->bind = data[at + GFX_SHADER_NAME_SIZE + 1];
	}

	for ( i = 0; i < samplers; i++ ) {
		at = samplersAt + i * GFX_SHADER_SAMPLER_SIZE;
		GfxShader_ReadName( data + at, name );
		arraySize = data[at + GFX_SHADER_NAME_SIZE + 2] | ( data[at + GFX_SHADER_NAME_SIZE + 3] << 8 );
		for ( j = 0; j < MAX( arraySize, 1 ); j++ ) {
			if ( file->numSamplers == GFX_SHADER_MAX_BINDINGS ) {
				Com_sprintf( error, errorSize, "more than %d samplers", GFX_SHADER_MAX_BINDINGS );
				return qfalse;
			}
			binding = &file->samplers[file->numSamplers++];
			if ( arraySize ) {
				// GL names an array's elements name[i]
				Com_sprintf( binding->name, sizeof( binding->name ), "%s[%d]", name, j );
			} else {
				Q_strncpyz( binding->name, name, sizeof( binding->name ) );
			}
			binding->bind = data[at + GFX_SHADER_NAME_SIZE + 1] + j;
		}
	}

	for ( i = 0; i < constants; i++ ) {
		at = constantsAt + i * GFX_SHADER_CONSTANT_SIZE;
		binding = &file->constants[file->numConstants++];
		GfxShader_ReadName( data + at, binding->name );
		// after the name, the members and their count
		binding->bind = data[at + GFX_SHADER_CONSTANT_SIZE - 4];
	}
	return qtrue;
}

/*
===============
GfxShader_Code

The code blob for a device (d3d12 falling back to d3d11's)
===============
*/
static const byte *GfxShader_Code( const gfxShaderFile_t *file, enum gfx_device_backend backend, uint32_t *length, char *error, int errorSize )
{
	int slot = (int)backend;

	if ( slot < 0 || slot >= GFX_SHADER_CODE_SLOTS ) {
		Com_sprintf( error, errorSize, "no code slot for %s", gfx_device_backend_name( backend ) );
		return NULL;
	}
	if ( backend == GFX_DEVICE_D3D12 && !file->codeLengths[slot] ) {
		slot = GFX_DEVICE_D3D11;
	}
	*length = file->codeLengths[slot];
	if ( !*length ) {
		Com_sprintf( error, errorSize, "no code for %s", gfx_device_backend_name( backend ) );
		return NULL;
	}
	if ( file->codeOffsets[slot] > (uint32_t)file->size || *length > (uint32_t)file->size - file->codeOffsets[slot] ) {
		Com_sprintf( error, errorSize, "code range outside the file" );
		return NULL;
	}
	return file->data + file->codeOffsets[slot];
}

/*
===============
GfxShader_Subdir

The compiled-shader folder for a device (gfx_demo's get_shader_subdir): shaders_vk, shaders_d3 for
the D3D devices, shaders_gles3_dark for gles3 off Linux except on GLFW's window, and shaders (the
gl3 / gl4 / gles3 code) otherwise
===============
*/
static const char *GfxShader_Subdir( enum gfx_device_backend device, enum gfx_window_backend window )
{
	switch ( device ) {
	case GFX_DEVICE_VK:
		return "shaders_vk";
	case GFX_DEVICE_GLES3:
#ifndef __linux__
		if ( window != GFX_WINDOW_GLFW ) {
			return "shaders_gles3_dark";
		}
#endif
		return "shaders";
	case GFX_DEVICE_D3D9:
	case GFX_DEVICE_D3D11:
	case GFX_DEVICE_D3D12:
		return "shaders_d3";
	default:
		return "shaders";
	}
}

/*
===============
GfxShader_Read

Reads <dir>/shaders/<subdir>/<name> from the first of the client's folder and fs_basepath that
holds it, and parses it. file->data is ri.Malloc'ed on success.
===============
*/
static qboolean GfxShader_Read( const char *subdir, const char *name, uint32_t magic, gfxShaderFile_t *file, char *error, int errorSize )
{
	const char *dirs[2];
	char path[MAX_OSPATH];
	char tried[MAX_OSPATH * 2];
	FILE *f = NULL;
	long size;
	int i;

	Com_Memset( file, 0, sizeof( *file ) );
	dirs[0] = Sys_BinaryPath();
	// the filesystem registered it at startup; Cvar_Get returns it unchanged
	dirs[1] = ri.Cvar_Get( "fs_basepath", "", 0 )->string;
	tried[0] = '\0';

	for ( i = 0; i < ARRAY_LEN( dirs ) && !f; i++ ) {
		if ( !dirs[i][0] || ( i > 0 && !Q_stricmp( dirs[i], dirs[0] ) ) ) {
			continue;
		}
		Com_sprintf( path, sizeof( path ), "%s%cshaders%c%s%c%s", dirs[i], PATH_SEP, PATH_SEP, subdir, PATH_SEP, name );
		f = fopen( path, "rb" );
		if ( !f ) {
			Q_strcat( tried, sizeof( tried ), tried[0] ? ", " : "" );
			Q_strcat( tried, sizeof( tried ), path );
		}
	}
	if ( !f ) {
		Com_sprintf( error, errorSize, "%s not found (%s)", name, tried );
		return qfalse;
	}

	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( size <= 0 ) {
		fclose( f );
		Com_sprintf( error, errorSize, "%s: empty", path );
		return qfalse;
	}
	file->data = ri.Malloc( (int)size );
	file->size = (int)size;
	if ( fread( file->data, 1, (size_t)size, f ) != (size_t)size ) {
		fclose( f );
		ri.Free( file->data );
		file->data = NULL;
		Com_sprintf( error, errorSize, "%s: read error", path );
		return qfalse;
	}
	fclose( f );

	if ( !GfxShader_Parse( file, magic, error, errorSize ) ) {
		char reason[MAX_STRING_CHARS];

		Q_strncpyz( reason, error, sizeof( reason ) );
		Com_sprintf( error, errorSize, "%s: %s", path, reason );
		ri.Free( file->data );
		file->data = NULL;
		return qfalse;
	}
	return qtrue;
}

static void GfxShader_Free( gfxShaderFile_t *file )
{
	if ( file->data ) {
		ri.Free( file->data );
		file->data = NULL;
	}
}

/*
===============
GfxShader_AddBinding

Appends a binding to a NULL-terminated gfx list (room for GFX_SHADER_MAX_BINDINGS + 1); a name the
list has already is skipped (a uniform block both stages declare is bound once)
===============
*/
static void GfxShader_AddBinding( struct gfx_shader_constant *list, int *count, const gfxShaderBinding_t *binding )
{
	int i;

	for ( i = 0; i < *count; i++ ) {
		if ( !strcmp( list[i].name, binding->name ) ) {
			return;
		}
	}
	if ( *count == GFX_SHADER_MAX_BINDINGS ) {
		return;
	}
	list[*count].name = binding->name;
	list[*count].bind = binding->bind;
	( *count )++;
	list[*count].name = NULL;
	list[*count].bind = 0;
}

/*
===============
GfxShader_LoadProgram

Loads <vertex>.vs.gfx and <fragment>.fs.gfx for a device and window, and links them with the
attribute, uniform block and sampler bindings their tables declare. qfalse: error says why.
===============
*/
qboolean GfxShader_LoadProgram( struct gfx_device *device, enum gfx_device_backend deviceBackend,
	enum gfx_window_backend windowBackend, const char *vertex, const char *fragment,
	gfxShaderProgram_t *program, char *error, int errorSize )
{
	const char *subdir = GfxShader_Subdir( deviceBackend, windowBackend );
	gfxShaderFile_t *vs, *fs;
	struct gfx_shader_create_info shaderInfo;
	struct gfx_shader_state_create_info stateInfo;
	// gfx's three binding structs have the same layout (name, bind): one list type serves all
	struct gfx_shader_constant attributes[GFX_SHADER_MAX_BINDINGS + 1];
	struct gfx_shader_constant constants[GFX_SHADER_MAX_BINDINGS + 1];
	struct gfx_shader_constant samplers[GFX_SHADER_MAX_BINDINGS + 1];
	int numAttributes = 0, numConstants = 0, numSamplers = 0;
	const byte *code;
	uint32_t length;
	char name[MAX_QPATH];
	qboolean ok = qfalse;
	int i;

	Com_Memset( program, 0, sizeof( *program ) );
	// two parsed files hold a few KB of bindings each: off the stack
	vs = ri.Malloc( sizeof( *vs ) );
	fs = ri.Malloc( sizeof( *fs ) );
	Com_Memset( vs, 0, sizeof( *vs ) );
	Com_Memset( fs, 0, sizeof( *fs ) );

	Com_sprintf( name, sizeof( name ), "%s.vs.gfx", vertex );
	if ( !GfxShader_Read( subdir, name, GFX_SHADER_MAGIC_VERTEX, vs, error, errorSize ) ) {
		goto done;
	}
	Com_sprintf( name, sizeof( name ), "%s.fs.gfx", fragment );
	if ( !GfxShader_Read( subdir, name, GFX_SHADER_MAGIC_FRAGMENT, fs, error, errorSize ) ) {
		goto done;
	}

	code = GfxShader_Code( vs, deviceBackend, &length, error, errorSize );
	if ( !code ) {
		goto done;
	}
	shaderInfo.type = GFX_SHADER_VERTEX;
	shaderInfo.data = code;
	shaderInfo.size = length;
	if ( !gfx_create_shader( device, &shaderInfo, &program->vertex ) ) {
		Com_sprintf( error, errorSize, "%s.vs: gfx could not create the shader", vertex );
		program->vertex = NULL;
		goto done;
	}

	code = GfxShader_Code( fs, deviceBackend, &length, error, errorSize );
	if ( !code ) {
		goto done;
	}
	shaderInfo.type = GFX_SHADER_FRAGMENT;
	shaderInfo.data = code;
	shaderInfo.size = length;
	if ( !gfx_create_shader( device, &shaderInfo, &program->fragment ) ) {
		Com_sprintf( error, errorSize, "%s.fs: gfx could not create the shader", fragment );
		program->fragment = NULL;
		goto done;
	}

	attributes[0].name = constants[0].name = samplers[0].name = NULL;
	for ( i = 0; i < vs->numInputs; i++ ) {
		GfxShader_AddBinding( attributes, &numAttributes, &vs->inputs[i] );
	}
	for ( i = 0; i < vs->numConstants; i++ ) {
		GfxShader_AddBinding( constants, &numConstants, &vs->constants[i] );
	}
	for ( i = 0; i < fs->numConstants; i++ ) {
		GfxShader_AddBinding( constants, &numConstants, &fs->constants[i] );
	}
	for ( i = 0; i < vs->numSamplers; i++ ) {
		GfxShader_AddBinding( samplers, &numSamplers, &vs->samplers[i] );
	}
	for ( i = 0; i < fs->numSamplers; i++ ) {
		GfxShader_AddBinding( samplers, &numSamplers, &fs->samplers[i] );
	}

	stateInfo.vertex_shader = program->vertex;
	stateInfo.fragment_shader = program->fragment;
	stateInfo.geometry_shader = NULL;
	stateInfo.attributes = (const struct gfx_shader_attribute *)attributes;
	stateInfo.constants = constants;
	stateInfo.samplers = (const struct gfx_shader_sampler *)samplers;
	if ( !gfx_create_shader_state( device, &stateInfo, &program->state ) ) {
		Com_sprintf( error, errorSize, "%s + %s: gfx could not link the shaders", vertex, fragment );
		program->state = NULL;
		goto done;
	}
	ok = qtrue;

done:
	if ( !ok ) {
		GfxShader_DeleteProgram( device, program );
	}
	GfxShader_Free( vs );
	GfxShader_Free( fs );
	ri.Free( vs );
	ri.Free( fs );
	return ok;
}

/*
===============
GfxShader_DeleteProgram

Releases the state and its shaders; the device must be the one that created them
===============
*/
void GfxShader_DeleteProgram( struct gfx_device *device, gfxShaderProgram_t *program )
{
	if ( program->state ) {
		gfx_delete_shader_state( device, program->state );
	}
	if ( program->vertex ) {
		gfx_delete_shader( device, program->vertex );
	}
	if ( program->fragment ) {
		gfx_delete_shader( device, program->fragment );
	}
	Com_Memset( program, 0, sizeof( *program ) );
}
