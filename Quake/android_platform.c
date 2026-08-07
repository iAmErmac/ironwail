#include "quakedef.h"
#include "platform.h"

#if defined(ANDROID_GLES3)
#include <android/log.h>

void PL_SetWindowIcon (void)
{
}

void PL_VID_Shutdown (void)
{
}

char *PL_GetClipboardData (void)
{
return NULL;
}

void PL_ErrorDialog (const char *errorMsg)
{
__android_log_print (ANDROID_LOG_ERROR, "Ironwail", "%s", errorMsg ? errorMsg : "unknown error");
}
#endif
