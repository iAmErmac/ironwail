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

void GL_PerfSetTextureMemory (size_t bytes)
{
	glperf_stats.texture_memory = bytes;
}

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
		glperf_stats.gpu_frame_ms = (double) elapsed * 1e-6;
}
#endif

void GL_PerfBeginFrame (void)
{
	glperf_frame_start = Sys_DoubleTime ();
	glperf_stats.draws = 0;
	glperf_stats.gpu_stalls = dev_stats.gpu_stalls;
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
}