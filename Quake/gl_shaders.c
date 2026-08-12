/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "gl_shaders.h"
#include "q_ctype.h"

glprogs_t glprogs;
static GLuint gl_programs[128];
static GLuint gl_current_program;
static int gl_num_programs;

#if defined(ANDROID_GLES3)
static cvar_t gl_gles_shader_dump = {"gl_gles_shader_dump", "0", CVAR_NONE};
static qboolean gl_gles_shader_dump_registered;
static unsigned gl_shader_dump_sequence;
static int gl_shader_programs_linked;

typedef struct gl_shader_source_s {
	GLuint shader;
	GLenum type;
	char name[256];
	char *source;
} gl_shader_source_t;

static gl_shader_source_t gl_shader_sources[8];
#endif

/*
=============
GL_InitError
=============
*/
static void GL_InitError (const char *message, ...)
{
	const char *fmt;
	char buf[4096];
	size_t len;
	va_list argptr;

	va_start (argptr, message);
	q_vsnprintf (buf, sizeof (buf), message, argptr);
	va_end (argptr);

	len = strlen (buf);
	while (len && q_isspace (buf[len - 1]))
		buf[--len] = '\0';

	fmt = 
		"Your system appears to meet the minimum requirements,\n"
		"however an error was encountered during OpenGL initialization.\n"
		"This could be caused by a driver or an engine bug.\n"
		"Please report this issue, including the following details:\n"
		"\n"
		"%s\n"
		"\n"
		"Engine:	Ironwail " IRONWAIL_VER_STRING " (%d-bit)\n"
		"OpenGL:	%s\n"
		"GPU:   	%s\n"
		"Vendor:	%s\n"
#if defined(_WIN32)
		"\n"
		"(Note: you can press Ctrl+C to copy this text to clipboard)"
#endif
	;
#if defined(ANDROID_GLES3)
    Con_Printf("GLES shader initialization error: %s\\n", buf);
    return;
#else
    Sys_Error (fmt, buf, (int) sizeof (void *) * 8, gl_version, gl_renderer, gl_vendor);
#endif
}

#if defined(ANDROID_GLES3)
static gl_shader_source_t *GL_FindShaderSource(GLuint shader)
{
	int i;

	for (i = 0; i < countof(gl_shader_sources); ++i)
		if (gl_shader_sources[i].shader == shader)
			return &gl_shader_sources[i];
	return NULL;
}

static void GL_ForgetShaderSource(GLuint shader)
{
	gl_shader_source_t *entry = GL_FindShaderSource(shader);

	if (!entry)
		return;
	free(entry->source);
	memset(entry, 0, sizeof(*entry));
}

static void GL_DumpShaderSource(GLenum type, const char *name, const char *source)
{
	char safe_name[192];
	char dump_dir[MAX_OSPATH];
	char dump_path[MAX_OSPATH];
	const char *stage;
	int i, j;

	if (!gl_gles_shader_dump.value || !source || !host_parms || !host_parms->basedir)
		return;

	for (i = 0, j = 0; name[i] && j < (int)sizeof(safe_name) - 1; ++i)
	{
		char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-')
			safe_name[j++] = c;
		else
			safe_name[j++] = '_';
	}
	safe_name[j] = 0;
	stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
	q_snprintf(dump_dir, sizeof(dump_dir), "%s/shader_dumps", host_parms->basedir);
	Sys_mkdir(dump_dir);
	q_snprintf(dump_path, sizeof(dump_path), "%s/%04u_%s_%s.glsl",
		dump_dir, ++gl_shader_dump_sequence, safe_name, stage);
	if (COM_WriteFile_OSPath(dump_path, source, strlen(source)))
		Con_Printf("GLES shader source dump: %s\n", dump_path);
	else
		Con_Printf("GLES shader source dump failed: %s\n", dump_path);
}

static void GL_RegisterShaderSource(GLuint shader, GLenum type, const char *name, char *source)
{
	int i;

	for (i = 0; i < countof(gl_shader_sources); ++i)
	{
		if (gl_shader_sources[i].shader)
			continue;
		gl_shader_sources[i].shader = shader;
		gl_shader_sources[i].type = type;
		q_strlcpy(gl_shader_sources[i].name, name, sizeof(gl_shader_sources[i].name));
		gl_shader_sources[i].source = source;
		return;
	}
	free(source);
	Con_Printf("GLES shader source registry full for %s\n", name);
}
#endif

/*
=============
AppendString
=============
*/
static qboolean AppendString (char **dst, const char *dstend, const char *str, int len)
{
	int avail = dstend - *dst;
	if (len < 0)
		len = Q_strlen (str);
	if (len + 1 > avail)
		return false;
	memcpy (*dst, str, len);
	(*dst)[len] = 0;
	*dst += len;
	return true;
}

/*
=============
GL_CreateShader
=============
*/
static GLuint GL_CreateShader (GLenum type, const char *source, const char *extradefs, const char *name)
{
	const char *strings[16];
	const char *typestr = NULL;
	char header[512];
	int numstrings = 0;
	GLint status;
	GLuint shader;
#if defined(ANDROID_GLES3)
	const char *line_directive = "#line 1\n";
	char *final_source;
	size_t final_length;
#endif

	switch (type)
	{
		case GL_VERTEX_SHADER:
			typestr = "vertex";
			break;
		case GL_FRAGMENT_SHADER:
			typestr = "fragment";
			break;
		case GL_COMPUTE_SHADER:
			typestr = "compute";
			break;
		default:
			Sys_Error ("GL_CreateShader: unknown type 0x%X for %s", type, name);
			break;
	}
#if defined(ANDROID_GLES3)
	q_snprintf (header, sizeof (header),
		"#version 310 es\n"
		"precision highp float;\n"
		"precision highp int;\n"

		"precision highp sampler2D;\n"
		"precision highp samplerCube;\n"
		"precision highp usampler3D;\n"
		"\n"
		"#define IW_GL_BACKEND_GLES 1\n"
		"#define IW_NOPERSPECTIVE smooth\n"
		"#define BINDLESS 0\n"
		"#define USE_BINDLESS 0\n"
		"#define USE_DRAW_ID 0\n"
		"#define USE_OIT 0\n"
        "#define IW_MIX(a,b,t) ((a) + ((b) - (a)) * (t))\n"
		"#define USE_MULTISAMPLE 0\n"
		"#define REVERSED_Z 0\n"
	);
#else
	q_snprintf (header, sizeof (header),
		"#version 430\n"
		"\n"
		"#define IW_GL_BACKEND_GLES 0\n"
		"#define IW_NOPERSPECTIVE noperspective\n"
		"#define BINDLESS %d\n"
		"#define USE_BINDLESS %d\n"
		"#define USE_DRAW_ID 0\n"
		"#define USE_OIT %d\n"
		"#define USE_MULTISAMPLE 0\n"
		"#define REVERSED_Z %d\n",
		gl_bindless_able,
		gl_bindless_able,
		gl_bindless_able ? 1 : 0,
		gl_clipcontrol_able
	);
#endif
	strings[numstrings++] = header;
	if (extradefs && *extradefs)
		strings[numstrings++] = extradefs;
#if defined(ANDROID_GLES3)
	strings[numstrings++] = line_directive;
#endif
	strings[numstrings++] = source;

#if defined(ANDROID_GLES3)
	final_length = strlen(header) +
		(extradefs && *extradefs ? strlen(extradefs) : 0) +
		strlen(line_directive) + strlen(source);
	final_source = (char *) malloc(final_length + 1);
	if (!final_source)
		Sys_Error ("GL_CreateShader: out of memory for %s", name);
	final_source[0] = 0;
	strcat(final_source, header);
	if (extradefs && *extradefs)
		strcat(final_source, extradefs);
	strcat(final_source, line_directive);
	strcat(final_source, source);
#endif

	    shader = GL_CreateShaderFunc (type);
	if (GL_ObjectLabelFunc)
		GL_ObjectLabelFunc (GL_SHADER, shader, -1, name);
	GL_ShaderSourceFunc (shader, numstrings, strings, NULL);
	GL_CompileShaderFunc (shader);
	GL_GetShaderivFunc (shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[4096];
		memset(infolog, 0, sizeof(infolog));
#if defined(ANDROID_GLES3)
		GL_DumpShaderSource(type, name, final_source);
#endif
		GL_GetShaderInfoLogFunc (shader, sizeof(infolog), NULL, infolog);
		GL_InitError ("Error compiling %s %s shader:\n\n%s", name, typestr, infolog);
	}

#if defined(ANDROID_GLES3)
	GL_RegisterShaderSource(shader, type, name, final_source);
#endif
	return shader;
}

/*
=============
GL_CreateProgramFromShaders
=============
*/
static GLuint GL_CreateProgramFromShaders (const GLuint *shaders, int numshaders, const char *name)
{
	GLuint program;
	GLint status;
	int i;

	program = GL_CreateProgramFunc ();
	if (GL_ObjectLabelFunc)
		GL_ObjectLabelFunc (GL_PROGRAM, program, -1, name);

	for (i = 0; i < numshaders; ++i)
		GL_AttachShaderFunc (program, shaders[i]);

	GL_LinkProgramFunc (program);
	GL_GetProgramivFunc (program, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[4096];
		memset(infolog, 0, sizeof(infolog));
#if defined(ANDROID_GLES3)
		for (i = 0; i < numshaders; ++i)
		{
			gl_shader_source_t *entry = GL_FindShaderSource(shaders[i]);
			if (entry)
				GL_DumpShaderSource(entry->type, entry->name, entry->source);
		}
#endif
		GL_GetProgramInfoLogFunc (program, sizeof(infolog), NULL, infolog);
		GL_InitError ("Error linking %s program:\n\n%s", name, infolog);
	}

	for (i = 0; i < numshaders; ++i)
	{
		GL_DeleteShaderFunc (shaders[i]);
#if defined(ANDROID_GLES3)
		GL_ForgetShaderSource(shaders[i]);
#endif
	}

	if (gl_num_programs == countof(gl_programs))
		Sys_Error ("gl_programs overflow");
	gl_programs[gl_num_programs] = program;
	gl_num_programs++;
#if defined(ANDROID_GLES3)
	gl_shader_programs_linked++;
#endif
	return program;
}

/*
====================
GL_CreateProgramFromSources
====================
*/
static GLuint GL_CreateProgramFromSources (int count, const GLchar **sources, const GLenum *types, const char *name, va_list argptr)
{
	char macros[1024];
	char eval[256];
	char *pipe;
	int i, realcount;
	GLuint shaders[2];

	if (count <= 0 || count > 2)
		Sys_Error ("GL_CreateProgramFromSources: invalid source count (%d)", count);

	q_vsnprintf (eval, sizeof (eval), name, argptr);
	macros[0] = 0;

	pipe = strchr (name, '|');
	if (pipe) // parse symbol list and generate #defines
	{
		char *dst = macros;
		char *dstend = macros + sizeof (macros);
		char *src = eval + 1 + (pipe - name);

		while (*src == ' ')
			src++;

		while (*src)
		{
			char *srcend = src + 1;
			while (*srcend && *srcend != ';')
				srcend++;

			if (!AppendString (&dst, dstend, "#define ", 8) ||
				!AppendString (&dst, dstend, src, srcend - src) ||
				!AppendString (&dst, dstend, "\n", 1))
				Sys_Error ("GL_CreateProgram: symbol overflow for %s", eval);

			src = srcend;
			while (*src == ';' || *src == ' ')
				src++;
		}

		AppendString (&dst, dstend, "\n", 1);
	}

	name = eval;

	realcount = 0;
	for (i = 0; i < count; i++)
		if (sources[i])
			shaders[realcount++] = GL_CreateShader (types[i], sources[i], macros, name);

	{
		GLuint program = GL_CreateProgramFromShaders (shaders, realcount, name);
#if defined(ANDROID_GLES3)
		Con_Printf ("GLES shader link ok: %s stages=%d macros=%s\n",
			name, realcount, macros[0] ? macros : "(none)");
#endif
		return program;
	}
}

/*
====================
GL_CreateProgram

Compiles and returns GLSL program.
====================
*/
static FUNC_PRINTF(3,4) GLuint GL_CreateProgram (const GLchar *vertSource, const GLchar *fragSource, const char *name, ...)
{
	const GLchar *sources[2] = {vertSource, fragSource};
	GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
	va_list argptr;
	GLuint program;

	va_start (argptr, name);
	program = GL_CreateProgramFromSources (2, sources, types, name, argptr);
	va_end (argptr);

	return program;
}

/*
====================
GL_CreateComputeProgram

Compiles and returns GLSL program.
====================
*/
static FUNC_PRINTF(2,3) GLuint GL_CreateComputeProgram (const GLchar *source, const char *name, ...)
{
	GLenum type = GL_COMPUTE_SHADER;
	va_list argptr;
	GLuint program;

	va_start (argptr, name);
	program = GL_CreateProgramFromSources (1, &source, &type, name, argptr);
	va_end (argptr);

	return program;
}

/*
====================
GL_UseProgram
====================
*/
void GL_UseProgram (GLuint program)
{
	if (program == gl_current_program)
		return;
	gl_current_program = program;
	GL_PerfCountProgramBind ();
	GL_UseProgramFunc (program);
}

/*
====================
GL_ClearCachedProgram

This must be called if you do anything that could make the cached program
invalid (e.g. manually binding, destroying the context).
====================
*/
void GL_ClearCachedProgram (void)
{
	gl_current_program = 0;
	GL_UseProgramFunc (0);
}

/*
=============
GL_CreateShaders
=============
*/
void GL_CreateShaders (void)
{
	int palettize, dither, mode, alphatest, warp, oit, poseverttype;
#if defined(ANDROID_GLES3)
	if (!gl_gles_shader_dump_registered)
	{
		Cvar_RegisterVariable (&gl_gles_shader_dump);
		gl_gles_shader_dump_registered = true;
	}
	gl_shader_programs_linked = 0;
	Con_Printf ("GLES shader validation: base direct permutations begin\n");
	const int poseverttype_count = 3;
	const int oit_count = 1;
	const int dither_count = 3;
#else
	const int poseverttype_count = 3;
	const int oit_count = 2;
	const int dither_count = 3;
#endif

	glprogs.gui = GL_CreateProgram (gui_vertex_shader, gui_fragment_shader, "gui");
	glprogs.viewblend = GL_CreateProgram (viewblend_vertex_shader, viewblend_fragment_shader, "viewblend");
	for (warp = 0; warp < 2; warp++)
		glprogs.warpscale[warp] = GL_CreateProgram (warpscale_vertex_shader, warpscale_fragment_shader, "view warp/scale|WARP %d", warp);
	for (palettize = 0; palettize < 3; palettize++)
		glprogs.postprocess[palettize] = GL_CreateProgram (postprocess_vertex_shader, postprocess_fragment_shader, "postprocess|PALETTIZE %d", palettize);

#if !defined(ANDROID_GLES3)
	for (mode = 0; mode < 2; mode++)
		glprogs.oit_resolve[mode] = GL_CreateProgram (oit_resove_vertex_shader, oit_resove_fragment_shader, "oit resolve|MSAA %d", mode);
#endif

	for (oit = 0; oit < oit_count; oit++)
		for (dither = 0; dither < dither_count; dither++)
			for (mode = 0; mode < 3; mode++)
				#if defined(ANDROID_GLES3)
				glprogs.world[oit][dither][mode] = GL_CreateProgram (world_vertex_shader_gles, world_fragment_shader_gles, "world|OIT %d; DITHER %d; MODE %d", oit, dither, mode);
#else
				glprogs.world[oit][dither][mode] = GL_CreateProgram (world_vertex_shader, world_fragment_shader, "world|OIT %d; DITHER %d; MODE %d", oit, dither, mode);
#endif

	for (dither = 0; dither < 2; dither++)
	{
		for (oit = 0; oit < oit_count; oit++)
		{
			#if defined(ANDROID_GLES3)
			glprogs.water[oit][dither] = GL_CreateProgram (water_vertex_shader_gles, water_fragment_shader_gles, "water|OIT %d; DITHER %d", oit, dither);
#else
			glprogs.water[oit][dither] = GL_CreateProgram (water_vertex_shader, water_fragment_shader, "water|OIT %d; DITHER %d", oit, dither);
#endif
			glprogs.particles[oit][dither] = GL_CreateProgram (particles_vertex_shader, particles_fragment_shader, "particles|OIT %d; DITHER %d", oit, dither);
		}
		for (mode = 0; mode < 2; mode++)
			#if defined(ANDROID_GLES3)
			glprogs.skycubemap[mode][dither] = GL_CreateProgram (sky_cubemap_vertex_shader_gles, sky_cubemap_fragment_shader, "sky cubemap|ANIM %d; DITHER %d", mode, dither);
#else
			glprogs.skycubemap[mode][dither] = GL_CreateProgram (sky_cubemap_vertex_shader, sky_cubemap_fragment_shader, "sky cubemap|ANIM %d; DITHER %d", mode, dither);
#endif
		#if defined(ANDROID_GLES3)
		glprogs.skylayers[dither] = GL_CreateProgram (sky_layers_vertex_shader_gles, sky_layers_fragment_shader, "sky layers|DITHER %d", dither);
#else
		glprogs.skylayers[dither] = GL_CreateProgram (sky_layers_vertex_shader, sky_layers_fragment_shader, "sky layers|DITHER %d", dither);
#endif
		glprogs.skyboxside[dither] = GL_CreateProgram (sky_boxside_vertex_shader, sky_boxside_fragment_shader, "skybox side|DITHER %d", dither);
		glprogs.sprites[dither] = GL_CreateProgram (sprites_vertex_shader, sprites_fragment_shader, "sprites|DITHER %d", dither);
	}
	#if defined(ANDROID_GLES3)
	glprogs.skystencil = GL_CreateProgram (skystencil_vertex_shader, skystencil_fragment_shader_gles, "sky stencil");
#else
	glprogs.skystencil = GL_CreateProgram (skystencil_vertex_shader, NULL, "sky stencil");
#endif

	for (oit = 0; oit < oit_count; oit++)
		for (mode = 0; mode < 3; mode++)
			for (alphatest = 0; alphatest < 2; alphatest++)
				for (poseverttype = 0; poseverttype < poseverttype_count; poseverttype++)
					glprogs.alias[oit][mode][alphatest][poseverttype] =
#if defined(ANDROID_GLES3)
					GL_CreateProgram (alias_vertex_shader_gles, alias_fragment_shader_gles, "alias|OIT %d; MODE %d; ALPHATEST %d; POSEVERTTYPE %d", oit, mode, alphatest, poseverttype);
#else
					GL_CreateProgram (alias_vertex_shader, alias_fragment_shader, "alias|OIT %d; MODE %d; ALPHATEST %d; POSEVERTTYPE %d", oit, mode, alphatest, poseverttype);
#endif
	glprogs.debug3d = GL_CreateProgram (debug3d_vertex_shader, debug3d_fragment_shader, "debug3d");

#if !defined(ANDROID_GLES3)
	glprogs.clear_indirect = GL_CreateComputeProgram (clear_indirect_compute_shader, "clear indirect draw params");
	glprogs.gather_indirect = GL_CreateComputeProgram (gather_indirect_compute_shader, "indirect draw gather");
	glprogs.cull_mark = GL_CreateComputeProgram (cull_mark_compute_shader, "cull/mark");
	glprogs.cluster_lights = GL_CreateComputeProgram (cluster_lights_compute_shader, "light cluster");
	for (mode = 0; mode < 3; mode++)
		glprogs.palette_init[mode] = GL_CreateComputeProgram (palette_init_compute_shader, "palette init|MODE %d", mode);
	glprogs.palette_postprocess = GL_CreateComputeProgram (palette_postprocess_compute_shader, "palette postprocess");
#endif
#if defined(ANDROID_GLES3)
	Con_Printf ("GLES shader validation: linked=%d expected=50; skipped=OIT-resolve, compute/indirect, clustered-light, palette-compute, bindless, multisample\n", gl_shader_programs_linked);
#endif
}
/*
=============
GL_DeleteShaders
=============
*/
void GL_InvalidateShaders (void)
{
#if defined(ANDROID_GLES3)
	int i;
	for (i = 0; i < countof (gl_shader_sources); i++)
	{
		free (gl_shader_sources[i].source);
		memset (&gl_shader_sources[i], 0, sizeof (gl_shader_sources[i]));
	}
#endif
	gl_num_programs = 0;
	gl_current_program = 0;
	memset (&glprogs, 0, sizeof (glprogs));
}

void GL_DeleteShaders (void)
{
	int i;
	for (i = 0; i < gl_num_programs; i++)
	{
		GL_DeleteProgramFunc (gl_programs[i]);
		gl_programs[i] = 0;
	}
	gl_num_programs = 0;

	GL_UseProgramFunc (0);
	gl_current_program = 0;

	memset (&glprogs, 0, sizeof(glprogs));
}
