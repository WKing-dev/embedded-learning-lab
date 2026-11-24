#include "Function.h"
Function_Struct Function;

void Function_Init(void)
{
    Function.state = FUNC_IDLE;
    Function.count = 0;
    Function.data = 0;

}



void Function_Task(void)
{
  if(Function.count>0)
  {
    return;
  }

    switch (Function.state)
    {
    case FUNC_IDLE:
    Function.count=100;//100ms执行一次
        /* code */
        break;
    case FUNC_BUSY:
     Function.count=100;//100ms执行一次
        /* code */
        break;
    case FUNC_ERROR:
     Function.count=100;//100ms执行一次
        /* code */
        break;
    default:
        break;
    }
}


//放入1ms定时器中断中
void Function_Timing_Handle(void)
{
   if ( Function.count>0)
   {
       Function.count--;
   }

   
}