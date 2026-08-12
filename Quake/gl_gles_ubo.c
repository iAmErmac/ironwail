#include "quakedef.h"
#include "glquake.h"
#include "gl_gles_ubo.h"

#if defined(ANDROID_GLES3)
extern cvar_t r_gles_ubo_validate;
#define GLES_UBO_SLOTS 3
#define GLES_UBO_CAPACITY (128 * 1024)

typedef struct { GLuint buffer; GLuint *overflow; size_t offset; } gles_ubo_slot_t;
static gles_ubo_slot_t slots[GLES_UBO_SLOTS];
static int current_slot;
static int ubo_alignment;
typedef struct { GLuint buffer; GLintptr offset; GLsizeiptr size; } ubo_range_t;
static ubo_range_t ubo_ranges[8];

static size_t Align (size_t value) { return (value + (size_t)ubo_alignment) & ~(size_t)ubo_alignment; }
static void DeleteOverflow (gles_ubo_slot_t *slot) { size_t i; for (i = 0; i < VEC_SIZE(slot->overflow); i++) GL_DeleteBuffer(slot->overflow[i]); VEC_CLEAR(slot->overflow); }
void GLESUBO_Create (void)
{
    int i; GLint align = 16;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    if (align < 1) align = 1;
    ubo_alignment = align - 1;
    for (i = 0; i < GLES_UBO_SLOTS; i++) { if (!slots[i].buffer) GL_GenBuffersFunc(1, &slots[i].buffer); GL_BindBuffer(GL_UNIFORM_BUFFER, slots[i].buffer); GL_BufferDataFunc(GL_UNIFORM_BUFFER, GLES_UBO_CAPACITY, NULL, GL_STREAM_DRAW); slots[i].offset = 0; }
    current_slot = 0;
}
void GLESUBO_Invalidate (void) { int i; for (i=0;i<GLES_UBO_SLOTS;i++){ slots[i].buffer=0; slots[i].offset=0; VEC_CLEAR(slots[i].overflow); } memset(ubo_ranges,0,sizeof(ubo_ranges)); current_slot=0; }
void GLESUBO_Delete (void) { int i; for(i=0;i<GLES_UBO_SLOTS;i++){ DeleteOverflow(&slots[i]); GL_DeleteBuffer(slots[i].buffer); slots[i].buffer=0; } }
void GLESUBO_Acquire (void) { gles_ubo_slot_t *s; current_slot=(current_slot+1)%GLES_UBO_SLOTS; s=&slots[current_slot]; DeleteOverflow(s); s->offset=0; GL_BindBuffer(GL_UNIFORM_BUFFER,s->buffer); GL_BufferDataFunc(GL_UNIFORM_BUFFER,GLES_UBO_CAPACITY,NULL,GL_STREAM_DRAW); memset(ubo_ranges,0,sizeof(ubo_ranges)); }
GLuint GLESUBO_Upload (const void *data, size_t numbytes, GLbyte **outofs, const char *owner)
{
    gles_ubo_slot_t *s=&slots[current_slot]; size_t offset=Align(s->offset); GLuint buffer;
    if (numbytes > GLES_UBO_CAPACITY || offset + numbytes > GLES_UBO_CAPACITY) { GLuint overflow; GL_GenBuffersFunc(1,&overflow); GL_BindBuffer(GL_UNIFORM_BUFFER,overflow); GL_BufferDataFunc(GL_UNIFORM_BUFFER,numbytes,NULL,GL_STREAM_DRAW); GL_BufferSubDataFunc(GL_UNIFORM_BUFFER,0,numbytes,data); VEC_PUSH(s->overflow,overflow); *outofs=0; glperf_stats.ubo_overflows++; GL_PerfCountUpload(GL_UNIFORM_BUFFER,numbytes); Con_Warning("GLES UBO stream overflow owner=%s bytes=%u\n",owner?owner:"unknown",(unsigned)numbytes); return overflow; }
    buffer=s->buffer; GL_BindBuffer(GL_UNIFORM_BUFFER,buffer); GL_BufferSubDataFunc(GL_UNIFORM_BUFFER,(GLintptr)offset,numbytes,data); *outofs=(GLbyte*)offset; s->offset=offset+numbytes; if(s->offset>glperf_stats.ubo_peak) glperf_stats.ubo_peak=s->offset; glperf_stats.ubo_bytes+=numbytes; glperf_stats.ubo_uploads++; GL_PerfCountUpload(GL_UNIFORM_BUFFER,numbytes); return buffer;
}
void GLESUBO_BindRange (GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, const char *owner)
{
    if (index < countof(ubo_ranges) && ubo_ranges[index].buffer == buffer && ubo_ranges[index].offset == offset && ubo_ranges[index].size == size) { glperf_stats.ubo_range_skips++; return; }
    if (index < countof(ubo_ranges)) { ubo_ranges[index].buffer=buffer; ubo_ranges[index].offset=offset; ubo_ranges[index].size=size; }
    GL_BindBufferRangeFunc(GL_UNIFORM_BUFFER,index,buffer,offset,size); GL_PerfCountBufferRangeBind(); glperf_stats.ubo_range_binds++;
    if (r_gles_ubo_validate.value && (offset & ubo_alignment)) Con_Warning("GLES UBO alignment mismatch owner=%s offset=%d align=%d\n",owner?owner:"unknown",(int)offset,ubo_alignment+1);
}
#endif
