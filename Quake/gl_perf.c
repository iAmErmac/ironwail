#include "quakedef.h"
#include "glquake.h"
#include "gl_texmgr.h"
#if defined(ANDROID_GLES3)
#include "android_gles.h"
#endif

#if defined(ANDROID_GLES3)
#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_QUERY_RESULT_AVAILABLE_EXT 0x8867
#define GL_QUERY_RESULT_EXT 0x8866
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

glperf_stats_t glperf_stats;
static double glperf_frame_start;
#if defined(ANDROID_GLES3)
static GLuint glperf_queries[2];
static int glperf_query_index;
static qboolean glperf_query_initialized;
static qboolean glperf_query_active;
#endif

const char *GL_PerfRendererTier (void)
{
#if defined(ANDROID_GLES3)
	return IW_GLES_FeatureTier ();
#else
	return "desktop";
#endif
}

void GL_PerfCountDraws (int count)
{
	if (count > 0)
		glperf_stats.draws += count;
}

void GL_PerfCountAliasDraw (qboolean viewmodel)
{
	glperf_stats.alias_draws++;
	if (viewmodel)
		glperf_stats.viewmodel_draws++;
}

void GL_PerfSetTextureMemory (size_t bytes)
{
	glperf_stats.texture_memory = bytes;
}

void GL_PerfCountProgramBind (void) { glperf_stats.program_binds++; }
void GL_PerfCountBufferBind (void) { glperf_stats.buffer_binds++; }
void GL_PerfCountBufferRangeBind (void) { glperf_stats.buffer_range_binds++; }
void GL_PerfCountTextureBind (void) { glperf_stats.texture_binds++; }
void GL_PerfCountTextureUnitChange (void) { glperf_stats.texture_unit_changes++; }

void GL_PerfCountUpload (GLenum target, size_t bytes)
{
	switch (target)
	{
		case GL_ARRAY_BUFFER: glperf_stats.upload_array += bytes; break;
		case GL_ELEMENT_ARRAY_BUFFER: glperf_stats.upload_element += bytes; break;
		case GL_UNIFORM_BUFFER: glperf_stats.upload_uniform += bytes; break;
		case GL_SHADER_STORAGE_BUFFER: glperf_stats.upload_storage += bytes; break;
		case GL_DRAW_INDIRECT_BUFFER: glperf_stats.upload_indirect += bytes; break;
		default: break;
	}
}

void GL_PerfCountStreamRealloc (void) { glperf_stats.stream_reallocs++; }

#if defined(ANDROID_GLES3)
static void GL_PerfPollGpuQuery (GLuint query)
{
	GLint available = 0;
	GLint disjoint = 0;
	GLuint64 elapsed = 0;

	if (!query)
		return;
	GL_GetQueryObjectivEXTFunc (query, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
	if (!available)
		return;
	glGetIntegerv (GL_GPU_DISJOINT_EXT, &disjoint);
	GL_GetQueryObjectui64vEXTFunc (query, GL_QUERY_RESULT_EXT, &elapsed);
	if (!disjoint)
	{
		glperf_stats.gpu_frame_ms = (double) elapsed * 1e-6;
		glperf_stats.gpu_frame_valid = 1;
	}
}
#endif

void GL_PerfBeginFrame (void)
{
	glperf_frame_start = Sys_DoubleTime ();
	glperf_stats.draws = 0;
	glperf_stats.gpu_stalls = dev_stats.gpu_stalls;
	glperf_stats.gpu_frame_ms = 0.0;
	glperf_stats.gpu_frame_valid = 0;
	glperf_stats.program_binds = 0;
	glperf_stats.buffer_binds = 0;
	glperf_stats.buffer_range_binds = 0;
	glperf_stats.texture_binds = 0;
	glperf_stats.texture_unit_changes = 0;
	glperf_stats.vao_binds = 0;
	glperf_stats.layout_changes = 0;
	glperf_stats.stream_reallocs = 0;
    glperf_stats.ubo_bytes = 0;
    glperf_stats.ubo_peak = 0;
    glperf_stats.ubo_uploads = 0;
    glperf_stats.ubo_range_binds = 0;
    glperf_stats.ubo_range_skips = 0;
    glperf_stats.ubo_overflows = 0;
	glperf_stats.stream_vbo_bytes = 0;
	glperf_stats.stream_vbo_peak = 0;
	glperf_stats.stream_ebo_bytes = 0;
	glperf_stats.stream_ebo_peak = 0;
	glperf_stats.stream_vbo_uploads = 0;
	glperf_stats.stream_ebo_uploads = 0;
	glperf_stats.stream_overflows = 0;
	glperf_stats.alias_draws = 0;
	glperf_stats.viewmodel_draws = 0;
	glperf_stats.world_batch_source_draws = 0;
	glperf_stats.world_batch_emitted_draws = 0;
	glperf_stats.world_batch_batches = 0;
	glperf_stats.world_batch_fallbacks = 0;
	glperf_stats.world_batch_indices = 0;
	glperf_stats.static_entity_scans = 0;
	glperf_stats.static_entity_cache_hits = 0;
	glperf_stats.world_marked_surface_refs = 0;
	glperf_stats.world_visible_surfaces = 0;
	glperf_stats.world_submitted_surfaces = 0;
	glperf_stats.world_visible_invalid_commands = 0;
	glperf_stats.world_visible_capacity_overflows = 0;
	glperf_stats.world_frustum_culled = 0;
	glperf_stats.visible_entities = 0;
	glperf_stats.upload_array = 0;
	glperf_stats.upload_element = 0;
	glperf_stats.upload_uniform = 0;
	glperf_stats.upload_storage = 0;
	glperf_stats.upload_indirect = 0;
	GL_PerfSetTextureMemory (TexMgr_TotalUsage ());
#if defined(ANDROID_GLES3)
	if (gl_timer_query_able)
	{
		if (!glperf_query_initialized)
		{
			GL_GenQueriesEXTFunc (2, glperf_queries);
			glperf_query_initialized = true;
		}
		GL_PerfPollGpuQuery (glperf_queries[glperf_query_index ^ 1]);
		GL_BeginQueryEXTFunc (GL_TIME_ELAPSED_EXT, glperf_queries[glperf_query_index]);
		glperf_query_active = true;
	}
#endif
}

void GL_PerfEndFrame (void)
{
#if defined(ANDROID_GLES3)
	if (glperf_query_active)
	{
		GL_EndQueryEXTFunc (GL_TIME_ELAPSED_EXT);
		glperf_query_active = false;
		glperf_query_index ^= 1;
	}
#endif
	glperf_stats.frame_ms = (Sys_DoubleTime () - glperf_frame_start) * 1000.0;
	glperf_stats.gpu_stalls = dev_stats.gpu_stalls;

#if defined(ANDROID_GLES3)
	{
		static int report_frames;
		if (++report_frames >= 120 && r_speeds.value == 3)
		{
			size_t uploads = glperf_stats.upload_array + glperf_stats.upload_element + glperf_stats.upload_uniform + glperf_stats.upload_storage + glperf_stats.upload_indirect;
			Con_Printf ("GLES perf: cpu=%.3f gpu=%.3f valid=%d draws=%d stalls=%d binds=%d/%d/%d vao=%d layouts=%d tex=%d units=%d uploads=%zu realloc=%d streams=%zu/%zu peak=%zu/%zu(%d/%d) overflow=%d ubo=%zu peak=%zu uploads=%d ranges=%d/%d overflow=%d models=%d/%d batch=%d/%d/%d/%d idx=%zu vis=%d/%d/%d invalid=%d capacity=%d culled=%d ents=%d static=%d/%d tier=%s\n", glperf_stats.frame_ms, glperf_stats.gpu_frame_ms, glperf_stats.gpu_frame_valid, glperf_stats.draws, glperf_stats.gpu_stalls, glperf_stats.program_binds, glperf_stats.buffer_binds, glperf_stats.buffer_range_binds, glperf_stats.vao_binds, glperf_stats.layout_changes, glperf_stats.texture_binds, glperf_stats.texture_unit_changes, uploads, glperf_stats.stream_reallocs, glperf_stats.stream_vbo_bytes, glperf_stats.stream_ebo_bytes, glperf_stats.stream_vbo_peak, glperf_stats.stream_ebo_peak, glperf_stats.stream_vbo_uploads, glperf_stats.stream_ebo_uploads, glperf_stats.stream_overflows, glperf_stats.ubo_bytes, glperf_stats.ubo_peak, glperf_stats.ubo_uploads, glperf_stats.ubo_range_binds, glperf_stats.ubo_range_skips, glperf_stats.ubo_overflows, glperf_stats.alias_draws, glperf_stats.viewmodel_draws, glperf_stats.world_batch_source_draws, glperf_stats.world_batch_emitted_draws, glperf_stats.world_batch_batches, glperf_stats.world_batch_fallbacks, glperf_stats.world_batch_indices, glperf_stats.world_marked_surface_refs, glperf_stats.world_visible_surfaces, glperf_stats.world_submitted_surfaces, glperf_stats.world_visible_invalid_commands, glperf_stats.world_visible_capacity_overflows, glperf_stats.world_frustum_culled, glperf_stats.visible_entities, glperf_stats.static_entity_scans, glperf_stats.static_entity_cache_hits, GL_PerfRendererTier ());
			report_frames = 0;
		}
	}
#endif
}
