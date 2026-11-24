#ifndef  _FUNCTION_H_
#define  _FUNCTION_H_
#include "RM_typedef.h"
typedef enum __Function_State_Enum
{
    FUNC_IDLE,
    FUNC_BUSY,
    FUNC_ERROR

} Function_State_Enum;



typedef struct __Function_Struct
{
    uint8_t state;//状态
    uint16_t count;//计时
    uint8_t data;
} Function_Struct;
#endif 

