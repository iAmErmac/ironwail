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
// r_world.c: world model rendering

#include "quakedef.h"
#if defined(ANDROID_GLES3)
#include "gl_gles_vao.h"
#endif

extern cvar_t gl_fullbrights, r_oldskyleaf, r_showtris; //johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix
extern cvar_t r_oit;

extern gltexture_t *lightmap_texture;

extern GLuint gl_bmodel_vbo;
#if defined(ANDROID_GLES3)
extern GLuint gl_bmodel_vao;
extern cvar_t r_gles_static_vao;
extern cvar_t r_gles_world_batch;
extern cvar_t r_gles_world_cull_dist;
extern cvar_t r_gles_liquid_cull_dist;
extern cvar_t r_gles_alpha_cull_dist;
#endif
extern size_t gl_bmodel_vbo_size;
extern GLuint gl_bmodel_ibo;
extern size_t gl_bmodel_ibo_size;
extern GLuint gl_bmodel_indirect_buffer;
extern size_t gl_bmodel_indirect_buffer_size;
extern GLuint gl_bmodel_surf_buffer;
extern GLuint gl_bmodel_marksurf_buffer;
extern GLuint gl_bmodel_marksurf_buffer_size;

typedef struct gpumark_frame_s {
	vec4_t		frustum[4];
	vec3_t		vieworg;
	GLuint		oldskyleaf;
	GLuint		framecount;
	GLuint		padding[3];
} gpumark_frame_t;

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);

#if defined(ANDROID_GLES3)
static int *gles_visible_heads;
static int *gles_visible_tails;
static int *gles_visible_next;
static int *gles_visible_sort_indices;
static msurface_t *gles_visible_sort_base;
static msurface_t *gles_visible_surface_base;
static int gles_visible_surface_count;
static int gles_visible_surface_used;
static int gles_visible_command_count;

static int GLES_CullDistance (float value)
{
    int distance = (int)value;
    if (value == (float)distance && distance >= 1024 && distance <= 8192 && !(distance & 63))
        return distance;
    return 0;
}

static qboolean GLES_SurfaceDistanceCull (const msurface_t *surf)
{
    int limit;
    vec3_t delta;
    float distance2 = 0.f;
    int i;

    if (surf->flags & SURF_DRAWSKY)
        return false;
    if (surf->flags & SURF_DRAWTURB)
        limit = GLES_CullDistance (r_gles_liquid_cull_dist.value);
    else if (surf->flags & SURF_DRAWFENCE)
        limit = GLES_CullDistance (r_gles_alpha_cull_dist.value);
    else
        limit = GLES_CullDistance (r_gles_world_cull_dist.value);
    if (!limit)
        return false;

    for (i = 0; i < 3; i++)
    {
        if (r_refdef.vieworg[i] < surf->mins[i])
            delta[i] = surf->mins[i] - r_refdef.vieworg[i];
        else if (r_refdef.vieworg[i] > surf->maxs[i])
            delta[i] = r_refdef.vieworg[i] - surf->maxs[i];
        else
            delta[i] = 0.f;
        distance2 += delta[i] * delta[i];
    }
    return distance2 > (float)limit * (float)limit;
}

void R_ResetGLESWorldVisibility (void)
{
    gles_visible_heads = NULL;
    gles_visible_tails = NULL;
    gles_visible_next = NULL;
    gles_visible_sort_indices = NULL;
    gles_visible_sort_base = NULL;
    gles_visible_surface_base = NULL;
    gles_visible_surface_count = 0;
    gles_visible_surface_used = 0;
    gles_visible_command_count = 0;
}

static void R_InitGLESWorldVisibility (void)
{
    qmodel_t *world = cl.worldmodel;
    int command_count = world->texofs[TEXTYPE_COUNT];

    if (gles_visible_surface_base == world->surfaces &&
        gles_visible_surface_count == world->numsurfaces &&
        gles_visible_command_count == command_count)
        return;

    gles_visible_surface_base = world->surfaces;
    gles_visible_sort_base = world->surfaces;
    gles_visible_surface_count = world->numsurfaces;
    gles_visible_command_count = command_count;
    gles_visible_heads = (int *) Hunk_AllocNameNoFill (sizeof(*gles_visible_heads) * command_count, "gles visible heads");
    gles_visible_tails = (int *) Hunk_AllocNameNoFill (sizeof(*gles_visible_tails) * command_count, "gles visible tails");
    gles_visible_next = (int *) Hunk_AllocNameNoFill (sizeof(*gles_visible_next) * world->numsurfaces, "gles visible next");
    gles_visible_sort_indices = (int *) Hunk_AllocNameNoFill (sizeof(*gles_visible_sort_indices) * world->numsurfaces, "gles visible sort");
}
static int GLES_CompareVisibleSurface (const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (gles_visible_sort_base[ia].vbo_firstindex < gles_visible_sort_base[ib].vbo_firstindex)
        return -1;
    if (gles_visible_sort_base[ia].vbo_firstindex > gles_visible_sort_base[ib].vbo_firstindex)
        return 1;
    return ia - ib;
}
#endif
/*
===============
R_MarkVisSurfaces
===============
*/
static void R_MarkVisSurfaces (byte* vis)
{
#if defined(ANDROID_GLES3)
	int i, j;

	R_InitGLESWorldVisibility ();
	for (i = 0; i < gles_visible_command_count; i++)
		gles_visible_heads[i] = gles_visible_tails[i] = -1;
	gles_visible_surface_used = 0;
	for (i = 0; i < cl.worldmodel->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->leafs[i + 1];
		if (!(vis[i >> 3] & (1 << (i & 7))))
			continue;
		for (j = 0; j < leaf->nummarksurfaces; j++)
		{
			int surface_index = leaf->firstmarksurface[j];
			msurface_t *surf = &cl.worldmodel->surfaces[surface_index];
			int command;

			if (r_gles_perf_capture.value)
				glperf_stats.world_marked_surface_refs++;
			if (surf->visframe == r_visframecount)
				continue;
			surf->visframe = r_visframecount;
			if (r_gles_perf_capture.value)
				glperf_stats.world_visible_surfaces++;
			if (R_CullBox (surf->mins, surf->maxs))
			{
				if (r_gles_perf_capture.value)
					glperf_stats.world_frustum_culled++;
				continue;
			}
			if (GLES_SurfaceDistanceCull (surf))
			{
				if (r_gles_perf_capture.value)
				{
					if (surf->flags & SURF_DRAWTURB)
						glperf_stats.liquid_distance_culled++;
					else if (surf->flags & SURF_DRAWFENCE)
						glperf_stats.alpha_distance_culled++;
					else
						glperf_stats.world_distance_culled++;
				}
				continue;
			}
			command = surf->vbo_cmd - cl.worldmodel->firstcmd;
			if (command < 0 || command >= gles_visible_command_count)
			{
				if (r_gles_perf_capture.value)
					glperf_stats.world_visible_invalid_commands++;
				continue;
			}
			if (gles_visible_surface_used >= gles_visible_surface_count)
			{
				if (r_gles_perf_capture.value)
					glperf_stats.world_visible_capacity_overflows++;
				continue;
			}
			if (surf->flags & SURF_DRAWSKY)
				Fog_SetSkyVisible (true);
            gles_visible_next[surface_index] = -1;
            if (gles_visible_tails[command] < 0)
                gles_visible_heads[command] = surface_index;
            else
                gles_visible_next[gles_visible_tails[command]] = surface_index;
            gles_visible_tails[command] = surface_index;
			gles_visible_surface_used++;
		}

	}

    for (i = 0; i < gles_visible_command_count; i++)
    {
        int count = 0;
        int a;
        int current = gles_visible_heads[i];
        while (current >= 0)
        {
            gles_visible_sort_indices[count++] = current;
            current = gles_visible_next[current];
        }
        if (count > 1)
        {
            qsort (gles_visible_sort_indices, count, sizeof(*gles_visible_sort_indices), GLES_CompareVisibleSurface);
            gles_visible_heads[i] = gles_visible_sort_indices[0];
            gles_visible_tails[i] = gles_visible_sort_indices[count - 1];
            for (a = 0; a + 1 < count; a++)
                gles_visible_next[gles_visible_sort_indices[a]] = gles_visible_sort_indices[a + 1];
            gles_visible_next[gles_visible_tails[i]] = -1;
        }
    }
#else
	int			i;
	GLuint		buf;
	GLbyte*		ofs;
	size_t		vissize = (cl.worldmodel->numleafs + 7) >> 3;
	size_t		nummark = gl_bmodel_marksurf_buffer_size / sizeof (bmodel_gpu_marksurf_t);
	gpumark_frame_t frame;

	GL_BeginGroup ("Mark surfaces");

	for (i = 0; i < 4; i++)
	{
		frame.frustum[i][0] = frustum[i].normal[0];
		frame.frustum[i][1] = frustum[i].normal[1];
		frame.frustum[i][2] = frustum[i].normal[2];
		frame.frustum[i][3] = frustum[i].dist;
	}
	frame.vieworg[0] = r_refdef.vieworg[0];
	frame.vieworg[1] = r_refdef.vieworg[1];
	frame.vieworg[2] = r_refdef.vieworg[2];
	frame.oldskyleaf = r_oldskyleaf.value != 0.f;
	frame.framecount = r_framecount;

	COMPILE_TIME_ASSERT (vis_alignment_must_be_power_of_2, (VIS_ALIGN & (VIS_ALIGN - 1)) == 0);
	COMPILE_TIME_ASSERT (vis_alignment_must_be_multiple_of_uint, (VIS_ALIGN & 3) == 0);
	vissize = (vissize + VIS_ALIGN_MASK) & ~VIS_ALIGN_MASK; // round up

	GL_UseProgram (glprogs.clear_indirect);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 1, gl_bmodel_indirect_buffer, 0, cl.worldmodel->texofs[TEXTYPE_COUNT] * sizeof(bmodel_draw_indirect_t));
	GL_DispatchComputeFunc ((cl.worldmodel->texofs[TEXTYPE_COUNT] + 63) / 64, 1, 1);
	GL_MemoryBarrierFunc (GL_SHADER_STORAGE_BARRIER_BIT);

	GL_UseProgram (glprogs.cull_mark);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 2, gl_bmodel_ibo, 0, gl_bmodel_ibo_size);
	GL_Upload (GL_SHADER_STORAGE_BUFFER, vis, vissize, &buf, &ofs);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 3, buf, (GLintptr)ofs, vissize);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 4, gl_bmodel_marksurf_buffer, 0, gl_bmodel_marksurf_buffer_size);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 5, gl_bmodel_surf_buffer, 0, cl.worldmodel->numsurfaces * sizeof(bmodel_gpu_surf_t));
	GL_Upload (GL_UNIFORM_BUFFER, &frame, sizeof(frame), &buf, &ofs);
	GL_BindBufferRange (GL_UNIFORM_BUFFER, 1, buf, (GLintptr)ofs, sizeof(frame));

	GL_DispatchComputeFunc ((nummark + 63) / 64, 1, 1);
	GL_MemoryBarrierFunc (GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT);

	GL_EndGroup ();
#endif
}

/*
===============
R_MarkSurfaces
===============
*/
void R_MarkSurfaces (void)
{
	byte		*vis;
	int			i;
	qboolean	nearwaterportal;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = false;
	Fog_SetSkyVisible (false);
	for (i=0; i < r_viewleaf->nummarksurfaces; i++)
		if (cl.worldmodel->surfaces[r_viewleaf->firstmarksurface[i]].flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
		vis = Mod_NoVisPVS (cl.worldmodel);
	else if (nearwaterportal)
		vis = SV_FatPVS (r_origin, cl.worldmodel);
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	r_visframecount++;

	R_MarkVisSurfaces (vis);
	R_AddStaticModels (vis, !nearwaterportal);
}

/*
================
GL_WaterAlphaForEntityTextureType
 
Returns the water alpha to use for the entity and texture type combination.
================
*/
float GL_WaterAlphaForEntityTextureType (entity_t *ent, textype_t type)
{
	float entalpha;
	if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForTextureType(type);
	else
		entalpha = ENTALPHA_DECODE(ent->alpha);
	return entalpha;
}

typedef struct bmodel_gpu_instance_s {
	float		world[12];	// world matrix (transposed mat4x3)
	float		alpha;
	float		padding[3];
} bmodel_gpu_instance_t;

typedef struct bmodel_bindless_gpu_call_s {
	GLuint		flags;
	GLfloat		alpha;
	GLuint64	texture;
	GLuint64	fullbright;
} bmodel_bindless_gpu_call_t;

typedef struct bmodel_bound_gpu_call_s {
	GLuint		flags;
	GLfloat		alpha;
	GLint		baseinstance;
	GLint		padding;
} bmodel_bound_gpu_call_t;

typedef struct bmodel_gpu_call_remap_s {
	GLuint		src;
	GLuint		inst;
} bmodel_gpu_call_remap_t;

static bmodel_gpu_instance_t		bmodel_instances[MAX_VISEDICTS + 1]; // +1 for worldspawn
static union {
	struct {
		bmodel_bindless_gpu_call_t	params[MAX_BMODEL_DRAWS];
	} bindless;
	struct {
		bmodel_bound_gpu_call_t		params[MAX_BMODEL_DRAWS];
		gltexture_t					*textures[MAX_BMODEL_DRAWS][2];
	} bound;
} bmodel_calls;
static bmodel_gpu_call_remap_t		bmodel_call_remap[MAX_BMODEL_DRAWS];
static int							num_bmodel_calls;
static GLuint						bmodel_batch_program;

/*
=============
R_InitBModelInstance
=============
*/
static void R_InitBModelInstance (bmodel_gpu_instance_t *inst, entity_t *ent)
{
	vec3_t angles;
	float mat[16];

	angles[0] = -ent->angles[0];
	angles[1] =  ent->angles[1];
	angles[2] =  ent->angles[2];
	R_EntityMatrix (mat, ent->origin, angles, ent == &cl_entities[0] ? ENTSCALE_DEFAULT : ent->scale);

	MatrixTranspose4x3 (mat, inst->world);

	inst->alpha = ent->alpha == ENTALPHA_DEFAULT ? -1.f : ENTALPHA_DECODE (ent->alpha);
	memset (&inst->padding, 0, sizeof(inst->padding));
}

/*
=============
R_ResetBModelCalls
=============
*/
static void R_ResetBModelCalls (GLuint program)
{
	bmodel_batch_program = program;
	num_bmodel_calls = 0;
}

/*
=============
R_FlushBModelCalls
=============
*/
static void R_FlushBModelCalls (void)
{
	GLuint	cmdbuf, buf;
	GLbyte	*ofs;
	size_t	dstcmdofs;
#if defined(ANDROID_GLES3)
	static bmodel_draw_indirect_t commands[MAX_BMODEL_DRAWS];
	static const GLfloat identity_matrix[12] = {
		1.f, 0.f, 0.f, 0.f,
		0.f, 1.f, 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f
	};
#endif

	if (!num_bmodel_calls)
		return;

#if defined(ANDROID_GLES3)
	{
		GLuint buf;
		GLbyte *ofs;
		int i, oit, dither, mode;
		qboolean gles_world_program = false;
		qboolean gles_water_program = false;
		qboolean gles_skycubemap_program = false;

		for (i = 0; i < num_bmodel_calls; i++)
		{
			bmodel_draw_indirect_t command = gl_bmodel_indirect_cmds[bmodel_call_remap[i].src];
			command.instanceCount = (bmodel_call_remap[i].inst % MAX_BMODEL_INSTANCES) + 1;
			command.baseInstance = bmodel_call_remap[i].inst / MAX_BMODEL_INSTANCES;
			commands[i] = command;
		}

		GL_UseProgram (bmodel_batch_program);
		for (oit = 0; oit < countof (glprogs.world); oit++)
			for (dither = 0; dither < countof (glprogs.world[oit]); dither++)
				for (mode = 0; mode < countof (glprogs.world[oit][dither]); mode++)
					if (bmodel_batch_program == glprogs.world[oit][dither][mode])
						gles_world_program = true;
		for (oit = 0; oit < countof (glprogs.water); oit++)
			for (dither = 0; dither < countof (glprogs.water[oit]); dither++)
				if (bmodel_batch_program == glprogs.water[oit][dither])
					gles_water_program = true;
		for (oit = 0; oit < countof (glprogs.skycubemap); oit++)
			for (dither = 0; dither < countof (glprogs.skycubemap[oit]); dither++)
				if (bmodel_batch_program == glprogs.skycubemap[oit][dither])
					gles_skycubemap_program = true;
		if (gles_world_program)
		{
			GL_Uniform1iFunc (3, r_framedata.numlights);
			if (r_framedata.numlights > 0)
				GL_Uniform4fvFunc (4, r_framedata.numlights * 2, (const GLfloat *)r_lightbuffer.lights);
		}
		#if defined(ANDROID_GLES3)
		if (r_gles_static_vao.value)
			GLESVAO_BindStatic (gl_bmodel_vao, GLES_LAYOUT_WORLD, 4);
		else
		{
			GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ibo);
			GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
			GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, pos));
			GL_VertexAttribPointerFunc (1, 4, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, st));
			GL_VertexAttribPointerFunc (2, 1, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, lmofs));
			GL_VertexAttribIPointerFunc (3, 1, GL_UNSIGNED_INT, sizeof (glvert_t), (void *) offsetof (glvert_t, styles));
		}
		GLESVAO_UseLayout (GLES_LAYOUT_WORLD, "world", gl_bmodel_vbo, gl_bmodel_ibo, GL_UNSIGNED_INT);
#else
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ibo);
		GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
		GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, pos));
		GL_VertexAttribPointerFunc (1, 4, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, st));
		GL_VertexAttribPointerFunc (2, 1, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, lmofs));
		GL_VertexAttribIPointerFunc (3, 1, GL_UNSIGNED_INT, sizeof (glvert_t), (void *) offsetof (glvert_t, styles));
#endif
		GL_Upload (GL_SHADER_STORAGE_BUFFER, &bmodel_calls.bound.params, sizeof (bmodel_calls.bound.params[0]) * num_bmodel_calls, &buf, &ofs);
		GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 1, buf, (GLintptr)ofs, sizeof (bmodel_calls.bound.params[0]) * num_bmodel_calls);
		if (!gles_skycubemap_program)
			GL_Bind (GL_TEXTURE2, r_fullbright_cheatsafe ? greytexture : lightmap_texture);
		while (glGetError () != GL_NO_ERROR)
			;
		for (i = 0; i < num_bmodel_calls; i++)
		{
			int src = bmodel_call_remap[i].src;
			qboolean worldcmd = cl.worldmodel && src >= cl.worldmodel->firstcmd && src < cl.worldmodel->firstcmd + cl.worldmodel->texofs[TEXTYPE_COUNT];
			GL_BindTextures (0, 2, bmodel_calls.bound.textures[i]);
			if (worldcmd)
			{
				int surfindex;
				GL_Uniform4fvFunc (0, 3, identity_matrix);
				if (gles_world_program || gles_water_program)
					GL_Uniform1fFunc (132, bmodel_calls.bound.params[i].alpha);
                {
                    int command = src - cl.worldmodel->firstcmd;
                    int next_surface;

                    if (command < 0 || command >= gles_visible_command_count)
                        continue;
                    for (surfindex = gles_visible_heads[command]; surfindex != -1; surfindex = next_surface)
                    {
                        msurface_t *surf = &cl.worldmodel->surfaces[surfindex];
                        int draw_indices = (q_max (surf->numedges, 2) - 2) * 3;
                        int first_index = surf->vbo_firstindex;
                        int draw_surfaces = 1;

                        next_surface = gles_visible_next[surfindex];
#if defined(ANDROID_GLES3)
                        if (r_gles_world_batch.value)
                        {
                            while (draw_surfaces < 256 && next_surface != -1)
                            {
                                msurface_t *adjacent = &cl.worldmodel->surfaces[next_surface];
                                int adjacent_indices;
                                if (adjacent->vbo_cmd != src ||
                                    adjacent->vbo_firstindex != first_index + draw_indices)
                                    break;
                                adjacent_indices = (q_max (adjacent->numedges, 2) - 2) * 3;
                                if (draw_indices + adjacent_indices > 65535)
                                    break;
                                draw_indices += adjacent_indices;
                                draw_surfaces++;
                                next_surface = gles_visible_next[next_surface];
                            }
                            if (r_gles_perf_capture.value)
                            {
                                glperf_stats.world_batch_source_draws += draw_surfaces;
                                glperf_stats.world_batch_indices += draw_indices;
                                if (draw_surfaces > 1)
                                    glperf_stats.world_batch_batches++;
                                else
                                    glperf_stats.world_batch_fallbacks++;
                            }
                        }
#endif
                        GL_PerfCountDraws (1);
#if defined(ANDROID_GLES3)
                        if (r_gles_perf_capture.value)
                        {
                            glperf_stats.world_batch_emitted_draws++;
                            glperf_stats.world_submitted_surfaces += draw_surfaces;
                        }
#endif
                        glDrawElements (GL_TRIANGLES, draw_indices, GL_UNSIGNED_INT,
                            (const void *)((size_t)first_index * sizeof (GLuint)));
                    }
                }
			}
			else
			{
				int instance_index;
				for (instance_index = 0; instance_index < (int)commands[i].instanceCount; instance_index++)
				{
                GL_Uniform4fvFunc (0, 3,
                    bmodel_instances[commands[i].baseInstance + instance_index].world);
                if (gles_world_program)
                {
                    float alpha = bmodel_instances[commands[i].baseInstance + instance_index].alpha;
                    GL_Uniform1fFunc (132, alpha < 0.f ? bmodel_calls.bound.params[i].alpha : alpha);
                }
                GL_PerfCountDraws (1);
                glDrawElements (GL_TRIANGLES, (GLsizei)commands[i].count, GL_UNSIGNED_INT,
                    (const void *)((size_t)commands[i].firstIndex * sizeof (GLuint)));
				}
			}
		}
		num_bmodel_calls = 0;
		return;
	}
#endif

	GL_ReserveDeviceMemory (GL_DRAW_INDIRECT_BUFFER, sizeof (bmodel_draw_indirect_t) * num_bmodel_calls, &cmdbuf, &dstcmdofs);

	GL_UseProgram (glprogs.gather_indirect);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 5, gl_bmodel_indirect_buffer, 0, gl_bmodel_indirect_buffer_size);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 6, cmdbuf, dstcmdofs, sizeof (bmodel_draw_indirect_t) * num_bmodel_calls);
	GL_Upload (GL_SHADER_STORAGE_BUFFER, bmodel_call_remap, sizeof (bmodel_call_remap[0]) * num_bmodel_calls, &buf, &ofs);
	GL_BindBufferRange  (GL_SHADER_STORAGE_BUFFER, 7, buf, (GLintptr)ofs, sizeof (bmodel_call_remap[0]) * num_bmodel_calls);
	GL_DispatchComputeFunc ((num_bmodel_calls + 63) / 64, 1, 1);
	GL_MemoryBarrierFunc (GL_COMMAND_BARRIER_BIT);

	GL_UseProgram (bmodel_batch_program);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ibo);
	GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer (GL_DRAW_INDIRECT_BUFFER, cmdbuf);
	GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, pos));
	GL_VertexAttribPointerFunc (1, 4, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, st));
	GL_VertexAttribPointerFunc (2, 1, GL_FLOAT, GL_FALSE, sizeof (glvert_t), (void *) offsetof (glvert_t, lmofs));
	GL_VertexAttribIPointerFunc (3, 1, GL_UNSIGNED_INT, sizeof (glvert_t), (void *) offsetof (glvert_t, styles));

	if (gl_bindless_able)
	{
		GL_Upload (GL_SHADER_STORAGE_BUFFER, bmodel_calls.bindless.params, sizeof (bmodel_calls.bindless.params[0]) * num_bmodel_calls, &buf, &ofs);
		GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 1, buf, (GLintptr)ofs, sizeof (bmodel_calls.bindless.params[0]) * num_bmodel_calls);
		GL_PerfCountDraws (num_bmodel_calls);
		GL_MultiDrawElementsIndirectFunc (GL_TRIANGLES, GL_UNSIGNED_INT, (const void *)dstcmdofs, num_bmodel_calls, sizeof (bmodel_draw_indirect_t));
	}
	else
	{
		int i;

		GL_Upload (GL_SHADER_STORAGE_BUFFER, &bmodel_calls.bound.params, sizeof (bmodel_calls.bound.params[0]) * num_bmodel_calls, &buf, &ofs);
		GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 1, buf, (GLintptr)ofs, sizeof (bmodel_calls.bound.params[0]) * num_bmodel_calls);

		for (i = 0; i < num_bmodel_calls; i++)
		{
			GL_Uniform1iFunc (0, i);
			GL_BindTextures (0, 2, bmodel_calls.bound.textures[i]);
			GL_PerfCountDraws (1);
			GL_DrawElementsIndirectFunc (GL_TRIANGLES, GL_UNSIGNED_INT, (const byte *)(dstcmdofs + i * sizeof (bmodel_draw_indirect_t)));
		}
	}

	num_bmodel_calls = 0;
}

/*
=============
R_AddBModelCall
=============
*/
static void R_AddBModelCall (int index, int first_instance, int num_instances, texture_t *t, qboolean zfix)
{
	GLuint		flags;
	float		alpha;
	gltexture_t	*tx, *fb;

	if (num_bmodel_calls == MAX_BMODEL_DRAWS)
		R_FlushBModelCalls ();

	if (t)
	{
		tx = t->gltexture;
		fb = t->fullbright;
		if (r_lightmap_cheatsafe)
			tx = fb = NULL;
		if (!gl_fullbrights.value && t->type != TEXTYPE_SKY)
			fb = NULL;
	}
	else
	{
		tx = fb = whitetexture;
	}

	if (!gl_zfix.value || map_checks.value)
		zfix = 0;

	flags = zfix | ((fb != NULL) << 1) | ((r_fullbright_cheatsafe != false) << 2);
	alpha = t ? GL_WaterAlphaForTextureType (t->type) : 1.f;

	if (gl_bindless_able)
	{
		bmodel_bindless_gpu_call_t *call = &bmodel_calls.bindless.params[num_bmodel_calls];
		call->flags = flags;
		call->alpha = alpha;
		call->texture = tx ? tx->bindless_handle : greytexture->bindless_handle;
		call->fullbright = fb ? fb->bindless_handle : blacktexture->bindless_handle;
	}
	else
	{
		bmodel_bound_gpu_call_t *call = &bmodel_calls.bound.params[num_bmodel_calls];
		gltexture_t **textures = bmodel_calls.bound.textures[num_bmodel_calls];
		call->flags = flags;
		call->alpha = alpha;
		call->baseinstance = first_instance;
		call->padding = 0;
		textures[0] = tx ? tx : greytexture;
		textures[1] = fb ? fb : blacktexture;
	}

	SDL_assert (num_instances > 0);
	SDL_assert (num_instances <= MAX_BMODEL_INSTANCES);
	bmodel_call_remap[num_bmodel_calls].src = index;
	bmodel_call_remap[num_bmodel_calls].inst = first_instance * MAX_BMODEL_INSTANCES + (num_instances - 1);

	++num_bmodel_calls;
}

/*
=============
R_ChooseBModelProgram
=============
*/
static GLuint R_ChooseBModelProgram (qboolean oit, qboolean alphatest)
{
	extern cvar_t r_softemu_lightmap_banding;

	switch (softemu)
	{
	case SOFTEMU_BANDED:
		if (r_softemu_lightmap_banding.value != 0.f)
			return glprogs.world[oit][2][alphatest];
		else
			return glprogs.world[oit][1][alphatest];

	case SOFTEMU_COARSE:
		if (r_softemu_lightmap_banding.value > 0.f)
			return glprogs.world[oit][2][alphatest];
		else
			return glprogs.world[oit][1][alphatest];

	default:
		if (r_softemu_lightmap_banding.value > 0.f)
			return glprogs.world[oit][2][alphatest];
		else
			return glprogs.world[oit][0][alphatest];
	}
}

typedef enum {
	BP_SOLID,
	BP_ALPHATEST,
	BP_SKYLAYERS,
	BP_SKYCUBEMAP,
	BP_SKYSTENCIL,
	BP_SHOWTRIS,
} brushpass_t;

/*
=============
R_DrawBrushModels_Real
=============
*/
static void R_DrawBrushModels_Real (entity_t **ents, int count, brushpass_t pass, qboolean translucent)
{
	int i, j;
	int totalinst, baseinst;
	unsigned state;
	GLuint program;
	GLuint buf;
	GLbyte *ofs;
	textype_t texbegin, texend;
	qboolean oit;

	if (!count)
		return;

	if (count > countof(bmodel_instances))
	{
		Con_DWarning ("bmodel instance overflow: %d > %d\n", count, (int)countof(bmodel_instances));
		count = countof(bmodel_instances);
	}

	oit = translucent && R_GetEffectiveAlphaMode () == ALPHAMODE_OIT;
	switch (pass)
	{
	default:
	case BP_SOLID:
		texbegin = 0;
		texend = TEXTYPE_CUTOUT;
		program = R_ChooseBModelProgram (oit, false);
		break;
	case BP_ALPHATEST:
		texbegin = TEXTYPE_CUTOUT;
		texend = TEXTYPE_CUTOUT + 1;
		program = R_ChooseBModelProgram (oit, true);
		break;
	case BP_SKYLAYERS:
		texbegin = TEXTYPE_SKY;
		texend = TEXTYPE_SKY + 1;
		program = glprogs.skylayers[softemu == SOFTEMU_COARSE];
		break;
	case BP_SKYCUBEMAP:
		texbegin = TEXTYPE_SKY;
		texend = TEXTYPE_SKY + 1;
		program = glprogs.skycubemap[Sky_IsAnimated ()][softemu == SOFTEMU_COARSE];
		break;
	case BP_SKYSTENCIL:
		texbegin = TEXTYPE_SKY;
		texend = TEXTYPE_SKY + 1;
		program = glprogs.skystencil;
		break;
	case BP_SHOWTRIS:
		texbegin = 0;
		texend = TEXTYPE_COUNT;
		program = glprogs.world[0][0][0];
		break;
	}

	// fill instance data
	for (i = 0, totalinst = 0; i < count; i++)
		if (ents[i]->model->texofs[texend] - ents[i]->model->texofs[texbegin] > 0)
			R_InitBModelInstance (&bmodel_instances[totalinst++], ents[i]);

	if (!totalinst)
		return;

	// setup state
	state = GLS_CULL_BACK | GLS_ATTRIBS(4);
	if (!translucent)
		state |= GLS_BLEND_OPAQUE;
	else
		state |= GLS_BLEND_ALPHA_OIT | GLS_NO_ZWRITE;
#if defined(ANDROID_GLES3)
	if (pass >= BP_SKYLAYERS && pass <= BP_SKYSTENCIL)
		state |= GLS_NO_ZWRITE; // sky is drawn after opaque world and must leave cleared depth for the GLES background mask
#endif

	
	R_ResetBModelCalls (program);
	GL_SetState (state);
	if (pass <= BP_ALPHATEST)
		GL_Bind (GL_TEXTURE2, r_fullbright_cheatsafe ? greytexture : lightmap_texture);
	else if (pass == BP_SKYCUBEMAP)
		GL_Bind (GL_TEXTURE2, skybox->cubemap);

	GL_Upload (GL_SHADER_STORAGE_BUFFER, bmodel_instances, sizeof(bmodel_instances[0]) * count, &buf, &ofs);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 2, buf, (GLintptr)ofs, sizeof(bmodel_instances[0]) * count);

	// generate drawcalls
	for (i = 0, baseinst = 0; i < count; /**/)
	{
		int numinst;
		entity_t *e = ents[i++];
		qmodel_t *model = e->model;
		qboolean isworld = (e == &cl_entities[0]);
		qboolean isstatic = PTR_IN_RANGE (e, cl_static_entities, cl_static_entities + MAX_STATIC_ENTITIES);
		qboolean isdecal = isstatic && pass == BP_ALPHATEST;
		qboolean zfix = !isworld && !isdecal;
		int frame = isworld ? 0 : e->frame;
		int numtex = model->texofs[texend] - model->texofs[texbegin];

		if (!numtex)
			continue;

		for (numinst = 1; i < count && ents[i]->model == model && numinst < MAX_BMODEL_INSTANCES; i++)
			numinst += (ents[i]->model->texofs[texend] - ents[i]->model->texofs[texbegin]) > 0;

		for (j = model->texofs[texbegin]; j < model->texofs[texend]; j++)
		{
			texture_t *t = model->textures[model->usedtextures[j]];
			R_AddBModelCall (model->firstcmd + j, baseinst, numinst, pass != BP_SHOWTRIS ? R_TextureAnimation (t, frame) : 0, zfix);
		}

		baseinst += numinst;
	}

	R_FlushBModelCalls ();
}

/*
=============
R_EntHasWater
=============
*/
static qboolean R_EntHasWater (entity_t *ent, qboolean translucent)
{
	int i;
	for (i = TEXTYPE_FIRSTLIQUID; i < TEXTYPE_LASTLIQUID+1; i++)
	{
		int numtex = ent->model->texofs[i+1] - ent->model->texofs[i];
		if (numtex && (GL_WaterAlphaForEntityTextureType (ent, (textype_t)i) < 1.f) == translucent)
			return true;
	}
	return false;
}

/*
=============
R_DrawBrushModels_Water
=============
*/
void R_DrawBrushModels_Water (entity_t **ents, int count, qboolean translucent)
{
	int i, j;
	int totalinst, baseinst;
	unsigned state;
	GLuint buf, program;
	GLbyte *ofs;
	qboolean oit;

	if (count > countof(bmodel_instances))
	{
		Con_DWarning ("bmodel instance overflow: %d > %d\n", count, (int)countof(bmodel_instances));
		count = countof(bmodel_instances);
	}

	// fill instance data
	for (i = 0, totalinst = 0; i < count; i++)
		if (R_EntHasWater (ents[i], translucent))
			R_InitBModelInstance (&bmodel_instances[totalinst++], ents[i]);

	if (!totalinst)
		return;

	GL_BeginGroup (translucent ? "Water (translucent)" : "Water (opaque)");

	// setup state
	state = GLS_CULL_BACK | GLS_ATTRIBS(4);
	if (translucent)
		state |= GLS_BLEND_ALPHA_OIT | GLS_NO_ZWRITE;
	else
		state |= GLS_BLEND_OPAQUE;

	oit = translucent && R_GetEffectiveAlphaMode () == ALPHAMODE_OIT;
#if defined(ANDROID_GLES3)
	program = glprogs.water[oit][softemu == SOFTEMU_COARSE];
#else
	if (cl.worldmodel->haslitwater && r_litwater.value)
		program = glprogs.world[oit][q_max(0, (int)softemu - 1)][WORLDSHADER_WATER];
	else
		program = glprogs.water[oit][softemu == SOFTEMU_COARSE];
#endif

	R_ResetBModelCalls (program);
	GL_SetState (state);
	GL_Bind (GL_TEXTURE2, r_fullbright_cheatsafe ? greytexture : lightmap_texture);

	GL_Upload (GL_SHADER_STORAGE_BUFFER, bmodel_instances, sizeof(bmodel_instances[0]) * totalinst, &buf, &ofs);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 2, buf, (GLintptr)ofs, sizeof(bmodel_instances[0]) * count);

	// generate drawcalls
	for (i = 0, baseinst = 0; i < count; /**/)
	{
		int numinst;
		entity_t *e = ents[i++];
		qmodel_t *model = e->model;
		qboolean isworld = (e == &cl_entities[0]);
		int frame = isworld ? 0 : e->frame;

		if (!R_EntHasWater (e, translucent))
			continue;

		for (numinst = 1; i < count && ents[i]->model == model && numinst < MAX_BMODEL_INSTANCES; i++)
			numinst += R_EntHasWater (ents[i], translucent);

		for (j = model->texofs[TEXTYPE_FIRSTLIQUID]; j < model->texofs[TEXTYPE_LASTLIQUID+1]; j++)
		{
			texture_t *t = model->textures[model->usedtextures[j]];
			if ((GL_WaterAlphaForEntityTextureType (e, t->type) < 1.f) != translucent)
				continue;
			R_AddBModelCall (model->firstcmd + j, baseinst, numinst, R_TextureAnimation (t, frame), !isworld);
		}

		baseinst += numinst;
	}

	R_FlushBModelCalls ();

	GL_EndGroup ();
}

/*
=============
R_GetBModelAlphaPasses
=============
*/
static uint32_t R_GetBModelAlphaPasses (const entity_t *ent)
{
	const qmodel_t *mod = ent->model;
	uint32_t mask = 0;

	if (mod->texofs[TEXTYPE_CUTOUT] != mod->texofs[TEXTYPE_DEFAULT])
		mask |= (1 << BP_SOLID);
	if (mod->texofs[TEXTYPE_SKY] != mod->texofs[TEXTYPE_CUTOUT])
		mask |= (1 << BP_ALPHATEST);

	return mask;
}

/*
=============
R_CanMergeBModelAlphaPasses
=============
*/
static qboolean R_CanMergeBModelAlphaPasses (uint32_t mask_a, uint32_t mask_b)
{
	COMPILE_TIME_ASSERT (check_bit_0, BP_SOLID == 0);
	COMPILE_TIME_ASSERT (check_bit_1, BP_ALPHATEST == 1);

	enum
	{
		#define ALLOW_MERGE(a, b) (1 << ((a)|((b)<<2)))

		MERGE_LUT =
			ALLOW_MERGE (0, 0) |
			ALLOW_MERGE (0, 1) |
			ALLOW_MERGE (0, 2) |
			ALLOW_MERGE (0, 3) |

			ALLOW_MERGE (1, 0) |
			ALLOW_MERGE (1, 1) |
			ALLOW_MERGE (1, 2) |
			ALLOW_MERGE (1, 3) |

			ALLOW_MERGE (2, 0) |
			ALLOW_MERGE (2, 2) |

			ALLOW_MERGE (3, 0) |
			ALLOW_MERGE (3, 2)
		,

		#undef ALLOW_MERGE
	};

	return (MERGE_LUT & (1 << (mask_a | (mask_b << 2)))) != 0;
}

/*
=============
R_DrawBrushModels
=============
*/
void R_DrawBrushModels (entity_t **ents, int count)
{
	qboolean translucent;
	if (!count)
		return;
	translucent = (ents[0] != &cl_entities[0]) && !ENTALPHA_OPAQUE (ents[0]->alpha);
	if (!translucent || R_GetEffectiveAlphaMode () == ALPHAMODE_OIT)
	{
		R_DrawBrushModels_Real (ents, count, BP_SOLID, translucent);
		R_DrawBrushModels_Real (ents, count, BP_ALPHATEST, translucent);
	}
	else
	{
		int i, j;
		for (i = 0; i < count; /**/)
		{
			uint32_t mask = R_GetBModelAlphaPasses (ents[i]);
			if (!mask)
			{
				i++;
				continue;
			}
			for (j = i + 1; j < count; j++)
			{
				uint32_t nextmask = R_GetBModelAlphaPasses (ents[j]);
				if (!R_CanMergeBModelAlphaPasses (mask, nextmask))
					break;
				mask |= nextmask;
			}
			if (mask & (1 << BP_SOLID))
				R_DrawBrushModels_Real (ents + i, j - i, BP_SOLID, true);
			if (mask & (1 << BP_ALPHATEST))
				R_DrawBrushModels_Real (ents + i, j - i, BP_ALPHATEST, true);
			i = j;
		}
	}
}

/*
=============
R_DrawBrushModels_SkyLayers
=============
*/
void R_DrawBrushModels_SkyLayers (entity_t **ents, int count)
{
	R_DrawBrushModels_Real (ents, count, BP_SKYLAYERS, false);
}

/*
=============
R_DrawBrushModels_SkyCubemap
=============
*/
void R_DrawBrushModels_SkyCubemap (entity_t **ents, int count)
{
	R_DrawBrushModels_Real (ents, count, BP_SKYCUBEMAP, false);
}

/*
=============
R_DrawBrushModels_SkyStencil
=============
*/
void R_DrawBrushModels_SkyStencil (entity_t **ents, int count)
{
	R_DrawBrushModels_Real (ents, count, BP_SKYSTENCIL, false);
}

/*
=============
R_DrawBrushModels_ShowTris
=============
*/
void R_DrawBrushModels_ShowTris (entity_t **ents, int count)
{
	R_DrawBrushModels_Real (ents, count, BP_SHOWTRIS, false);
}
