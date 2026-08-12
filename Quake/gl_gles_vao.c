#include "quakedef.h"
#include "glquake.h"
#include "gl_gles_vao.h"

#if defined(ANDROID_GLES3)

typedef struct {
    const char *name;
    const char *class_name;
    int attributes;
    GLenum index_type;
    qboolean persistent;
} gles_layout_desc_t;

static const gles_layout_desc_t gles_layouts[GLES_LAYOUT_COUNT] = {
    [GLES_LAYOUT_NONE]       = { "none",       "direct-bound", 0, GL_NONE, false },
    [GLES_LAYOUT_WORLD]      = { "world",      "static-vao",   4, GL_UNSIGNED_INT, true },
    [GLES_LAYOUT_ALIAS_IQM]  = { "alias-iqm",  "static-vao",   5, GL_UNSIGNED_SHORT, true },
    [GLES_LAYOUT_ALIAS_MESH] = { "alias-mesh", "static-vao",   1, GL_UNSIGNED_SHORT, true },
    [GLES_LAYOUT_GUI]        = { "gui",        "dynamic-vao",  3, GL_UNSIGNED_SHORT, false },
    [GLES_LAYOUT_SPRITE]     = { "sprite",     "dynamic-vao",  2, GL_UNSIGNED_SHORT, false },
    [GLES_LAYOUT_PARTICLE]   = { "particle",   "dynamic-vao",  2, GL_NONE, false },
    [GLES_LAYOUT_SKY]        = { "sky",        "dynamic-vao",  2, GL_NONE, false },
    [GLES_LAYOUT_DEBUG]      = { "debug",      "dynamic-vao",  2, GL_UNSIGNED_SHORT, false },
    [GLES_LAYOUT_FULLSCREEN] = { "fullscreen", "direct-bound", 0, GL_NONE, false }
};

static GLuint gles_global_vao;
static GLuint gles_bound_vao;
static gles_layout_id_t gles_current_layout;
static GLuint gles_current_array;
static GLuint gles_current_element;
static GLenum gles_current_index;
static const char *gles_current_owner;

extern cvar_t r_gles_vao_validate;
extern cvar_t r_gles_static_vao;

void GLESVAO_Init (void)
{
    gles_global_vao = 0;
    gles_bound_vao = 0;
    gles_current_layout = GLES_LAYOUT_NONE;
    gles_current_array = 0;
    gles_current_element = 0;
    gles_current_index = GL_NONE;
    gles_current_owner = "none";

    for (int i = 1; i < GLES_LAYOUT_COUNT; i++)
        Con_SafePrintf ("GLES VAO layout: %s class=%s attrs=%d index=0x%04X lifetime=%s\n",
            gles_layouts[i].name, gles_layouts[i].class_name, gles_layouts[i].attributes,
            gles_layouts[i].index_type, gles_layouts[i].persistent ? "static" : "stream");
}

void GLESVAO_Invalidate (void)
{
    gles_global_vao = 0;
    gles_bound_vao = 0;
    gles_current_layout = GLES_LAYOUT_NONE;
    gles_current_array = 0;
    gles_current_element = 0;
    gles_current_index = GL_NONE;
    gles_current_owner = "context-loss";
}

void GLESVAO_BindGlobal (GLuint vao)
{
    gles_global_vao = vao;
    if (gles_bound_vao != vao)
    {
        gles_bound_vao = vao;
        GLESVAO_RecordBind ();
    }
    GL_BindVertexArrayFunc (vao);
    GL_InvalidateBufferBinding (GL_ELEMENT_ARRAY_BUFFER);
}

void GLESVAO_BindDynamic (void)
{
    GLESVAO_BindGlobal (gles_global_vao);
}

GLuint GLESVAO_CreateStatic (GLuint array_buffer, GLuint element_buffer, const gles_vao_attribute_t *attributes, int attribute_count)
{
    GLuint vao;
    int i;

    GL_GenVertexArraysFunc (1, &vao);
    GL_BindVertexArrayFunc (vao);
    GL_BindBufferFunc (GL_ARRAY_BUFFER, array_buffer);
    GL_BindBufferFunc (GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    for (i = 0; i < attribute_count; i++)
    {
        const gles_vao_attribute_t *a = &attributes[i];
        GL_EnableVertexAttribArrayFunc (a->index);
        if (a->integer)
            GL_VertexAttribIPointerFunc (a->index, a->size, a->type, a->stride, (const void *)a->offset);
        else
            GL_VertexAttribPointerFunc (a->index, a->size, a->type, a->normalized, a->stride, (const void *)a->offset);
    }
    for (i = attribute_count; i < GLS_ATTRIBS_MAXCOUNT; i++)
        GL_DisableVertexAttribArrayFunc (i);

    GLESVAO_BindGlobal (gles_global_vao);
    return vao;
}

void GLESVAO_DeleteStatic (GLuint *vao)
{
    if (vao && *vao)
    {
        GL_DeleteVertexArraysFunc (1, vao);
        *vao = 0;
    }
}

void GLESVAO_BindStatic (GLuint vao, gles_layout_id_t layout, int attribute_count)
{
    if (!r_gles_static_vao.value || !vao)
    {
        GLESVAO_BindGlobal (gles_global_vao);
        return;
    }
    if (gles_bound_vao != vao)
    {
        gles_bound_vao = vao;
        GL_BindVertexArrayFunc (vao);
        GL_InvalidateBufferBinding (GL_ELEMENT_ARRAY_BUFFER);
        GLESVAO_RecordBind ();
    }
    if (gles_current_layout != layout)
        GLESVAO_RecordLayoutChange ();
    gles_current_layout = layout;
    GL_ForceVertexAttribCount (attribute_count);
}

void GLESVAO_RecordLayoutChange (void)
{
    glperf_stats.layout_changes++;
}

void GLESVAO_RecordBind (void)
{
    glperf_stats.vao_binds++;
}


void GLESVAO_UseLayout (gles_layout_id_t layout, const char *owner, GLuint array_buffer, GLuint element_buffer, GLenum index_type)
{
    GLint vao = 0, array = 0, element = 0;
    const gles_layout_desc_t *desc;

    if (layout <= GLES_LAYOUT_NONE || layout >= GLES_LAYOUT_COUNT)
        layout = GLES_LAYOUT_NONE;
    desc = &gles_layouts[layout];
    if (gles_current_layout != layout || gles_current_array != array_buffer ||
        gles_current_element != element_buffer || gles_current_index != index_type)
        GLESVAO_RecordLayoutChange ();
    gles_current_layout = layout;
    gles_current_array = array_buffer;
    gles_current_element = element_buffer;
    gles_current_index = index_type;
    gles_current_owner = owner ? owner : desc->name;

    if (!r_gles_vao_validate.value)
        return;

    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &array);
    glGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
    if ((GLuint)vao != gles_bound_vao || (!desc->persistent && (GLuint)array != array_buffer) ||
        (desc->persistent && element_buffer != (GLuint)-1 && (GLuint)element != element_buffer))
        Con_Warning ("GLES VAO layout mismatch owner=%s layout=%s expected vao=%u array=%u element=%u got vao=%d array=%d element=%d\n",
            gles_current_owner, desc->name, gles_bound_vao, array_buffer, element_buffer, vao, array, element);
    if (desc->index_type != GL_NONE && index_type != desc->index_type)
        Con_Warning ("GLES VAO index mismatch owner=%s layout=%s expected=0x%04X got=0x%04X\n",
            gles_current_owner, desc->name, desc->index_type, index_type);
}

#endif