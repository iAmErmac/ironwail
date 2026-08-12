#ifndef GL_GLES_UBO_H
#define GL_GLES_UBO_H

#if defined(ANDROID_GLES3)
void GLESUBO_Create (void);
void GLESUBO_Invalidate (void);
void GLESUBO_Delete (void);
void GLESUBO_Acquire (void);
GLuint GLESUBO_Upload (const void *data, size_t numbytes, GLbyte **outofs, const char *owner);
void GLESUBO_BindRange (GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, const char *owner);
#endif

#endif
