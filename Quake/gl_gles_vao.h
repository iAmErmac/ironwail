#ifndef GL_GLES_VAO_H
#define GL_GLES_VAO_H

#if defined(ANDROID_GLES3)

typedef struct {
    GLuint index;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLboolean integer;
    GLsizei stride;
    size_t offset;
} gles_vao_attribute_t;

typedef enum {
    GLES_LAYOUT_NONE = 0,
    GLES_LAYOUT_WORLD,
    GLES_LAYOUT_ALIAS_IQM,
    GLES_LAYOUT_ALIAS_MESH,
    GLES_LAYOUT_GUI,
    GLES_LAYOUT_SPRITE,
    GLES_LAYOUT_PARTICLE,
    GLES_LAYOUT_SKY,
    GLES_LAYOUT_DEBUG,
    GLES_LAYOUT_FULLSCREEN,
    GLES_LAYOUT_COUNT
} gles_layout_id_t;

void GLESVAO_Init (void);
void GLESVAO_Invalidate (void);
void GLESVAO_BindGlobal (GLuint vao);
void GLESVAO_BindDynamic (void);
GLuint GLESVAO_CreateStatic (GLuint array_buffer, GLuint element_buffer, const gles_vao_attribute_t *attributes, int attribute_count);
void GLESVAO_DeleteStatic (GLuint *vao);
void GLESVAO_BindStatic (GLuint vao, gles_layout_id_t layout, int attribute_count);
void GLESVAO_UseLayout (gles_layout_id_t layout, const char *owner, GLuint array_buffer, GLuint element_buffer, GLenum index_type);
void GLESVAO_RecordLayoutChange (void);
void GLESVAO_RecordBind (void);

#endif
#endif