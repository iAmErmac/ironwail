#include "quakedef.h"
#include "glquake.h"
#include "gl_gles_stream.h"

#if defined(ANDROID_GLES3)

#define GLES_STREAM_SLOTS 3
#define GLES_STREAM_VBO_CAPACITY (256 * 1024)
#define GLES_STREAM_EBO_CAPACITY (128 * 1024)

typedef struct
{
    GLuint vbo;
    GLuint ebo;
    GLuint *overflow;
    size_t voffset;
    size_t eoffset;
} gles_stream_slot_t;

static gles_stream_slot_t slots[GLES_STREAM_SLOTS];
static int current_slot;

static size_t GLESStream_Align (size_t offset, size_t alignment)
{
    return (offset + alignment - 1) & ~(alignment - 1);
}

static void GLESStream_DeleteOverflow (gles_stream_slot_t *slot)
{
    size_t i;
    for (i = 0; i < VEC_SIZE (slot->overflow); i++)
        GL_DeleteBuffer (slot->overflow[i]);
    VEC_CLEAR (slot->overflow);
}

void GLESStream_Create (void)
{
    int i;
    for (i = 0; i < GLES_STREAM_SLOTS; i++)
    {
        gles_stream_slot_t *slot = &slots[i];
        if (!slot->vbo)
            GL_GenBuffersFunc (1, &slot->vbo);
        if (!slot->ebo)
            GL_GenBuffersFunc (1, &slot->ebo);
        GL_BindBuffer (GL_ARRAY_BUFFER, slot->vbo);
        GL_BufferDataFunc (GL_ARRAY_BUFFER, GLES_STREAM_VBO_CAPACITY, NULL, GL_STREAM_DRAW);
        GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, slot->ebo);
        GL_BufferDataFunc (GL_ELEMENT_ARRAY_BUFFER, GLES_STREAM_EBO_CAPACITY, NULL, GL_STREAM_DRAW);
        slot->voffset = 0;
        slot->eoffset = 0;
    }
    current_slot = 0;
}

void GLESStream_Invalidate (void)
{
    int i;
    for (i = 0; i < GLES_STREAM_SLOTS; i++)
    {
        slots[i].vbo = 0;
        slots[i].ebo = 0;
        slots[i].voffset = 0;
        slots[i].eoffset = 0;
        VEC_CLEAR (slots[i].overflow);
    }
    current_slot = 0;
}

void GLESStream_Delete (void)
{
    int i;
    for (i = 0; i < GLES_STREAM_SLOTS; i++)
    {
        GLESStream_DeleteOverflow (&slots[i]);
        GL_DeleteBuffer (slots[i].vbo);
        GL_DeleteBuffer (slots[i].ebo);
        slots[i].vbo = slots[i].ebo = 0;
    }
}

void GLESStream_Acquire (void)
{
    gles_stream_slot_t *slot;

    current_slot = (current_slot + 1) % GLES_STREAM_SLOTS;
    slot = &slots[current_slot];
    GLESStream_DeleteOverflow (slot);
    slot->voffset = 0;
    slot->eoffset = 0;

    GL_BindBuffer (GL_ARRAY_BUFFER, slot->vbo);
    GL_BufferDataFunc (GL_ARRAY_BUFFER, GLES_STREAM_VBO_CAPACITY, NULL, GL_STREAM_DRAW);
    GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, slot->ebo);
    GL_BufferDataFunc (GL_ELEMENT_ARRAY_BUFFER, GLES_STREAM_EBO_CAPACITY, NULL, GL_STREAM_DRAW);
}


GLuint GLESStream_Upload (GLenum target, const void *data, size_t numbytes, GLbyte **outofs, const char *owner)
{
    gles_stream_slot_t *slot = &slots[current_slot];
    GLuint buffer;
    size_t *offset;
    size_t capacity;
    size_t alignment;
    const char *stream_name;

    if (target == GL_ARRAY_BUFFER)
    {
        buffer = slot->vbo;
        offset = &slot->voffset;
        capacity = GLES_STREAM_VBO_CAPACITY;
        alignment = 16;
        stream_name = "vbo";
    }
    else if (target == GL_ELEMENT_ARRAY_BUFFER)
    {
        buffer = slot->ebo;
        offset = &slot->eoffset;
        capacity = GLES_STREAM_EBO_CAPACITY;
        alignment = 2;
        stream_name = "ebo";
    }
    else
        Sys_Error ("GLESStream_Upload: unsupported target 0x%04X (%s)", target, owner ? owner : "unknown");

    *offset = GLESStream_Align (*offset, alignment);
    if (*offset + numbytes > capacity)
    {
        GLuint overflow;
        GL_GenBuffersFunc (1, &overflow);
        GL_BindBuffer (target, overflow);
        GL_BufferDataFunc (target, numbytes, NULL, GL_STREAM_DRAW);
        GL_BufferSubDataFunc (target, 0, numbytes, data);
        VEC_PUSH (slot->overflow, overflow);
        *outofs = (GLbyte *) 0;
        glperf_stats.stream_overflows++;
        Con_Warning ("GLES %s stream overflow owner=%s bytes=%u capacity=%u\n", stream_name,
            owner ? owner : "unknown", (unsigned)numbytes, (unsigned)capacity);
        GL_PerfCountUpload (target, numbytes);
        return overflow;
    }

    GL_BindBuffer (target, buffer);
    GL_BufferSubDataFunc (target, (GLintptr)*offset, numbytes, data);
    *outofs = (GLbyte *)*offset;
    *offset += numbytes;
    if (target == GL_ARRAY_BUFFER)
    {
        if (*offset > glperf_stats.stream_vbo_peak)
            glperf_stats.stream_vbo_peak = *offset;
    }
    else if (*offset > glperf_stats.stream_ebo_peak)
        glperf_stats.stream_ebo_peak = *offset;
    if (target == GL_ARRAY_BUFFER)
        glperf_stats.stream_vbo_bytes += numbytes, glperf_stats.stream_vbo_uploads++;
    else
        glperf_stats.stream_ebo_bytes += numbytes, glperf_stats.stream_ebo_uploads++;
    GL_PerfCountUpload (target, numbytes);
    return buffer;
}

#endif
