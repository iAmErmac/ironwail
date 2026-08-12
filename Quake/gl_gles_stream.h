#ifndef GL_GLES_STREAM_H
#define GL_GLES_STREAM_H

#if defined(ANDROID_GLES3)

void GLESStream_Create (void);
void GLESStream_Invalidate (void);
void GLESStream_Delete (void);
void GLESStream_Acquire (void);
GLuint GLESStream_Upload (GLenum target, const void *data, size_t numbytes, GLbyte **outofs, const char *owner);

#endif

#endif
