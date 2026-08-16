#ifndef IRONWAIL_XR_INPUT_H
#define IRONWAIL_XR_INPUT_H

#include "q_stdinc.h"


void XR_Input_Init(void);
void XR_Input_Shutdown(void);
void XR_Input_Update(void);
qboolean XR_Input_OwnsInput(void);
qboolean XR_Input_Move(usercmd_t *cmd);

#endif