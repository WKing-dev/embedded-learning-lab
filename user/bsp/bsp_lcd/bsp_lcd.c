/**
  ******************************************************************************
  * @file    bsp_lcd.c
  * @author  fire
  * @version V1.0
  * @date    2016-xx-xx
  * @brief   LCD应用函数接口，支持ARGB888/RGB888/RGB565/ARGB4444/ARGB1555 (不含中文显示)
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火  STM32 H743 开发板  
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :http://firestm32.taobao.com
  *
  ******************************************************************************
  */
  
/* ============================================================
 * 移植说明（相对野火原版）：
 *   1. 删除 LCD_GPIO_Config / LCD_ClockConfig / LCD_Init / LCD_LayerInit
 *      —— 引脚/时钟/LTDC 图层全部由 CubeMX 生成的 ltdc.c 负责
 *   2. 删除 lcd_param[] / cur_lcd / PIXEL_BPP[]（死代码）
 *   3. Ltdc_Handler 别名到 CubeMX 的 hltdc，画图函数自动拿到
 *      帧缓存地址(0xC0000000) 与行宽(ImageWidth=1024)
 *   4. 调用前必须自己设置：LCD_SetFont / LCD_SetTextColor / LCD_SetBackColor
 * ============================================================ */

/* Includes ------------------------------------------------------------------*/
#include "bsp_lcd.h"
#include "tim.h"        /* 提供 htim2（背光 PWM） */
#include "font24.c"
#include "font20.c"
#include "font16.c"
#include "font12.c"
#include "font8.c"


#define POLY_X(Z)              ((int32_t)((Points + Z)->X))
#define POLY_Y(Z)              ((int32_t)((Points + Z)->Y)) 

#define ABS(X)  ((X) > 0 ? (X) : -(X))

extern LTDC_HandleTypeDef  hltdc;      /* CubeMX 生成的 LTDC 句柄 */
#define  Ltdc_Handler      hltdc
static DMA2D_HandleTypeDef Dma2d_Handler;

/* Default LCD configuration with LCD Layer 1 */
uint32_t            ActiveLayer = 0;
LCD_DrawPropTypeDef DrawProp[MAX_LAYER_NUMBER];
/**
 * @brief  Initializes the LCD.
 * @param  None
 * @retval None
 */





static void DrawChar(uint16_t Xpos, uint16_t Ypos, const uint8_t *c);
static void FillTriangle(uint16_t x1, uint16_t x2, uint16_t x3, uint16_t y1, uint16_t y2, uint16_t y3);
static void LL_FillBuffer(uint32_t LayerIndex, void *pDst, uint32_t xSize, uint32_t ySize, uint32_t OffLine, uint32_t ColorIndex);
static void LL_ConvertLineToARGB8888(void * pSrc, void *pDst, uint32_t xSize, uint32_t ColorMode);

/**
  * @brief  获取LCD当前有效层X轴的大小
  * @retval X轴的大小
  */
uint32_t LCD_GetXSize(void)
{
  return Ltdc_Handler.LayerCfg[ActiveLayer].ImageWidth;
}

/**
  * @brief  获取LCD当前有效层Y轴的大小
  * @retval Y轴的大小
  */
uint32_t LCD_GetYSize(void)
{
  return Ltdc_Handler.LayerCfg[ActiveLayer].ImageHeight;
}

/**
  * @brief  设置LCD当前有效层Y轴的大小
  * @param  imageWidthPixels：图像宽度像素个数
  * @retval 无
  */
void LCD_SetXSize(uint32_t imageWidthPixels)
{
  Ltdc_Handler.LayerCfg[ActiveLayer].ImageWidth = imageWidthPixels;
}

/**
  * @brief  设置LCD当前有效层Y轴的大小
  * @param  imageHeightPixels：图像高度像素个数
  * @retval None
  */
void LCD_SetYSize(uint32_t imageHeightPixels)
{
  Ltdc_Handler.LayerCfg[ActiveLayer].ImageHeight = imageHeightPixels;
}

/**
  * @brief  选择LCD层
  * @param  LayerIndex: 前景层(层1)或者背景层(层0)
  * @retval 无
  */
void LCD_SelectLayer(uint32_t LayerIndex)
{
  ActiveLayer = LayerIndex;
} 

/**
  * @brief  设置LCD层的可视化
  * @param  LayerIndex: 前景层(层1)或者背景层(层0)
  * @param  State: 禁能或者使能
  * @retval 无
  */
void LCD_SetLayerVisible(uint32_t LayerIndex, FunctionalState State)
{
  if(State == ENABLE)
  {
    __HAL_LTDC_LAYER_ENABLE(&Ltdc_Handler, LayerIndex);
  }
  else
  {
    __HAL_LTDC_LAYER_DISABLE(&Ltdc_Handler, LayerIndex);
  }
  __HAL_LTDC_RELOAD_CONFIG(&Ltdc_Handler);
} 

/**
  * @brief  设置LCD的透明度常量
  * @param  LayerIndex: 前景层(层1)或者背景层(层0)
  * @param  Transparency: 透明度，范围（0—255）0为完全透明
  * @retval 无
  */
void LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency)
{    
  HAL_LTDC_SetAlpha(&Ltdc_Handler, Transparency, LayerIndex);
}

/**
  * @brief  设置LCD缓冲帧的首地址 
  * @param  LayerIndex: 前景层(层1)或者背景层(层0)
  * @param  Address: LCD缓冲帧的首地址     
  * @retval 无
  */
void LCD_SetLayerAddress(uint32_t LayerIndex, uint32_t Address)
{
  HAL_LTDC_SetAddress(&Ltdc_Handler, Address, LayerIndex);
}

/**
  * @brief  设置显示窗口
  * @param  LayerIndex: 前景层(层1)或者背景层(层0)
  * @param  Xpos: LCD 	X轴起始位置
  * @param  Ypos: LCD 	Y轴起始位置
  * @param  Width: LCD  窗口大小
  * @param  Height: LCD 窗口大小  
  * @retval None
  */
void LCD_SetLayerWindow(uint16_t LayerIndex, uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height)
{
  /* 重新设置窗口大小 */
  HAL_LTDC_SetWindowSize(&Ltdc_Handler, Width, Height, LayerIndex);
  
  /* 重新设置窗口的起始位置 */
  HAL_LTDC_SetWindowPosition(&Ltdc_Handler, Xpos, Ypos, LayerIndex); 
}


/**
  * @brief  设置LCD当前层文字颜色
  * @param  Color: 文字颜色
  * @retval 无
  */
void LCD_SetTextColor(uint32_t Color)
{
  DrawProp[ActiveLayer].TextColor = Color;
}

/**
  * @brief  获取LCD当前层文字颜色
  * @retval 文字颜色
  */
uint32_t LCD_GetTextColor(void)
{
  return DrawProp[ActiveLayer].TextColor;
}

/**
  * @brief  设置LCD当前层的文字背景颜色
  * @param  Color: 文字背景颜色
  * @retval 无
  */
void LCD_SetBackColor(uint32_t Color)
{
  DrawProp[ActiveLayer].BackColor = Color;
}

/**
  * @brief  获取LCD当前层的文字背景颜色
  * @retval 文字背景颜色
  */
uint32_t LCD_GetBackColor(void)
{
  return DrawProp[ActiveLayer].BackColor;
}

/**
 * @brief  设置LCD文字的颜色和背景的颜色
 * @param  TextColor: 指定文字颜色
 * @param  BackColor: 指定背景颜色
 * @retval 无
 */
void LCD_SetColors(uint32_t TextColor, uint32_t BackColor)
{
     LCD_SetTextColor (TextColor);
     LCD_SetBackColor (BackColor);
}
/**
  * @brief  设置LCD当前层显示的字体
  * @param  fonts: 字体类型
  * @retval None
  */
void LCD_SetFont(sFONT *fonts)
{
  DrawProp[ActiveLayer].pFont = fonts;
}

/**
  * @brief  获取LCD当前层显示的字体
  * @retval 字体类型
  */
sFONT *LCD_GetFont(void)
{
  return DrawProp[ActiveLayer].pFont;
}

/**
  * @brief  读LCD的像素值
  * @param  Xpos: X 轴坐标位置
  * @param  Ypos: Y 轴坐标位置
  * @retval RGB像素值
  */
uint32_t LCD_ReadPixel(uint16_t Xpos, uint16_t Ypos)
{
  uint32_t addr;
  uint32_t off;

  /* 边界保护：和 LCD_DrawPixel 保持一致 */
  if ((Xpos >= LCD_GetXSize()) || (Ypos >= LCD_GetYSize()))
  {
    return 0;
  }

  /* 线性像素序号；地址 = 基址 + 序号 × 每像素字节数 */
  off  = (uint32_t)Ypos * LCD_GetXSize() + (uint32_t)Xpos;
  addr = Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress;

  switch (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat)
  {
    /* ---------- 4 字节/像素 ---------- */
    case LTDC_PIXEL_FORMAT_ARGB8888:
      return *(__IO uint32_t *)(addr + off * 4U);          /* 0xAARRGGBB */

    /* ---------- 3 字节/像素 ---------- */
    case LTDC_PIXEL_FORMAT_RGB888:
    {
      uint32_t p = addr + off * 3U;
      uint32_t r = (uint32_t)(*(__IO uint8_t *)(p + 2U));  /* 内存顺序 B,G,R */
      uint32_t g = (uint32_t)(*(__IO uint8_t *)(p + 1U));
      uint32_t b = (uint32_t)(*(__IO uint8_t *)(p + 0U));
      return (r << 16) | (g << 8) | b;                     /* 0x00RRGGBB */
    }

    /* ---------- 2 字节/像素 ---------- */
    case LTDC_PIXEL_FORMAT_RGB565:
    case LTDC_PIXEL_FORMAT_ARGB1555:
    case LTDC_PIXEL_FORMAT_ARGB4444:
    case LTDC_PIXEL_FORMAT_AL88:
      return (uint32_t)(*(__IO uint16_t *)(addr + off * 2U));

    /* ---------- 1 字节/像素 ---------- */
    case LTDC_PIXEL_FORMAT_AL44:
    case LTDC_PIXEL_FORMAT_L8:
      return (uint32_t)(*(__IO uint8_t *)(addr + off));

    default:
      return 0;
  }
}

/**
  * @brief  回读一个像素，并统一换算成 0x00RRGGBB
  * @note   不管当前图层是 RGB565 / RGB888 / ARGB8888，返回值都是 24 位格式，
  *         便于跨格式比较（比如自动验证"填进去的颜色对不对"）。
  */
uint32_t LCD_ReadPixelRGB888(uint16_t Xpos, uint16_t Ypos)
{
  uint32_t v = LCD_ReadPixel(Xpos, Ypos);

  switch (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat)
  {
    case LTDC_PIXEL_FORMAT_RGB565:
    {
      uint16_t c = (uint16_t)v;
      uint32_t r = (c >> 11) & 0x1FU;
      uint32_t g = (c >>  5) & 0x3FU;
      uint32_t b =  c        & 0x1FU;

      r = (r << 3) | (r >> 2);
      g = (g << 2) | (g >> 4);
      b = (b << 3) | (b >> 2);
      return (r << 16) | (g << 8) | b;
    }

    case LTDC_PIXEL_FORMAT_ARGB8888:
      return v & 0x00FFFFFFU;        /* 丢掉 A */

    default:
      return v;                      /* RGB888 已经是 0x00RRGGBB */
  }
}

/**
  * @brief  LCD当前层清屏
  * @param  Color: 背景颜色
  * @retval None
  */
void LCD_Clear(uint32_t Color)
{ 
  /* 清屏 */ 
  LL_FillBuffer(ActiveLayer, (uint32_t *)(Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress), LCD_GetXSize(), LCD_GetYSize(), 0, Color);
}

/**
  * @brief  清除一行
  * @param  Line: 行
  * @retval None
  */
void LCD_ClearLine(uint32_t Line)
{
  uint32_t color_backup = DrawProp[ActiveLayer].TextColor;
  DrawProp[ActiveLayer].TextColor = DrawProp[ActiveLayer].BackColor;
  
  /* 画一个跟背景色一致的矩形完成清除 */
  LCD_FillRect(0, (Line * DrawProp[ActiveLayer].pFont->Height), LCD_GetXSize(), DrawProp[ActiveLayer].pFont->Height);
  
  DrawProp[ActiveLayer].TextColor = color_backup;
  LCD_SetTextColor(DrawProp[ActiveLayer].TextColor);  
}

/**
  * @brief  显示一个字符
  * @param  Xpos: X轴起始坐标
  * @param  Ypos: Y轴起始坐标
  * @param  Ascii: 字符 ascii 码,范围（ 0x20 —0x7E ）
  * @retval 无
  */
void LCD_DisplayChar(uint16_t Xpos, uint16_t Ypos, uint8_t Ascii)
{
    DrawChar(Xpos, Ypos, &DrawProp[ActiveLayer].pFont->table[(Ascii-' ') *\
    DrawProp[ActiveLayer].pFont->Height * ((DrawProp[ActiveLayer].pFont->Width + 7) / 8)]);
}

/**
  * @brief  显示字符串
  * @param  Xpos: X轴起始坐标
  * @param  Ypos: Y轴起始坐标 
  * @param  Text: 字符串指针
  * @param  Mode: 显示对齐方式，可以是CENTER_MODE、RIGHT_MODE、LEFT_MODE
  * @retval None
  */
void LCD_DisplayStringAt(uint16_t Xpos, uint16_t Ypos, uint8_t *Text, Text_AlignModeTypdef Mode)
{
  uint16_t ref_column = 1, i = 0;
  uint32_t size = 0, xsize = 0; 
  uint8_t  *ptr = Text;
  
  /* 获取字符串大小 */
  while (*ptr++) size ++ ;
  
  /* 每一行可以显示字符的数量 */
  xsize = (LCD_GetXSize()/DrawProp[ActiveLayer].pFont->Width);
  
  switch (Mode)
  {
  case CENTER_MODE:
    {
      ref_column = Xpos + ((xsize - size)* DrawProp[ActiveLayer].pFont->Width) / 2;
      break;
    }
  case LEFT_MODE:
    {
      ref_column = Xpos;
      break;
    }
  case RIGHT_MODE:
    {
      ref_column = - Xpos + ((xsize - size)*DrawProp[ActiveLayer].pFont->Width);
      break;
    }    
  default:
    {
      ref_column = Xpos;
      break;
    }
  }
  
  /*检查起始行是否在显示范围内 */
  if ((ref_column < 1) || (ref_column >= 0x8000))
  {
    ref_column = 1;
  }

  /* 使用字符显示函数显示每一个字符*/
  while ((*Text != 0) & (((LCD_GetXSize() - (i*DrawProp[ActiveLayer].pFont->Width)) & 0xFFFF)\
			>= DrawProp[ActiveLayer].pFont->Width))
  {
    /* 显示一个字符 */
    LCD_DisplayChar(ref_column, Ypos, *Text);
    /* 根据字体大小计算下一个偏移位置 */
    ref_column += DrawProp[ActiveLayer].pFont->Width;
    /* 指针指向下一个字符 */
    Text++;
    i++;
  }  
}

/**
  * @brief  在指定行显示字符串(最多60个)
  * @param  Line: 显示的行
  * @param  ptr: 字符串指针
  * @retval 无
  */
void LCD_DisplayStringLine(uint16_t Line, uint8_t *ptr)
{  
  LCD_DisplayStringAt(0, LINE(Line), ptr, LEFT_MODE);
}
/**
 * @brief  在显示器上显示一个中文字符
 * @param  usX ：在特定扫描方向下字符的起始X坐标
 * @param  usY ：在特定扫描方向下字符的起始Y坐标
 * @param  usChar ：要显示的中文字符（国标码）
 * @retval 无
 */ 
//static void LCD_DispChar_CH (uint16_t Xpos, uint16_t Ypos, uint16_t Text)
//{
//  uint32_t i = 0, j = 0;
//  uint16_t height, width;
//  uint8_t  offset;
//  uint8_t  *pchar;
//  uint8_t  Buffer[HEIGHT_CH_CHAR*3];
//  uint32_t line;
//	
//  GetGBKCode (Buffer, Text );
//  
//  height = 	HEIGHT_CH_CHAR;//取字模数据//获取正在使用字体高度
//  width  =  WIDTH_CH_CHAR; //获取正在使用字体宽度
//  
//  offset =  8 *((width + 7)/8) -  width ;//计算字符的每一行像素的偏移值，实际存储大小-字体宽度
//  
//  for(i = 0; i < height; i++)//遍历字体高度绘点
//  {
//    pchar = ((uint8_t *)Buffer + (width + 7)/8 * i);//计算字符的每一行像素的偏移地址
//    
//    switch(((width + 7)/8))//根据字体宽度来提取不同字体的实际像素值
//    {
//      
//    case 1:
//      line =  pchar[0];      //提取字体宽度小于8的字符的像素值
//      break;
//      
//    case 2:
//      line =  (pchar[0]<< 8) | pchar[1]; //提取字体宽度大于8小于16的字符的像素值     
//      break;
//      
//    case 3:
//    default:
//      line =  (pchar[0]<< 16) | (pchar[1]<< 8) | pchar[2]; //提取字体宽度大于16小于24的字符的像素值     
//      break;
//    } 
//    
//    for (j = 0; j < width; j++)//遍历字体宽度绘点
//    {
//      if(line & (1 << (width- j + offset- 1))) //根据每一行的像素值及偏移位置按照当前字体颜色进行绘点
//      {
//        LCD_DrawPixel((Xpos + j), Ypos, DrawProp[ActiveLayer].TextColor);
//      }
//      else//如果这一行没有字体像素则按照背景颜色绘点
//      {
//        LCD_DrawPixel((Xpos + j), Ypos, DrawProp[ActiveLayer].BackColor);
//      } 
//    }
//    Ypos++;
//  }
//}

/**
  * @brief  显示一行字符，若超出液晶宽度，不自动换行。
			中英混显时，请把英文字体设置为Font24格式
  * @param  Line: 要显示的行编号LINE(0) - LINE(N)
  * @param  *ptr: 要显示的字符串指针
  * @retval None
  */
void LCD_DisplayStringLine_EN_CH(uint16_t Line, uint8_t *ptr)
{  
  uint16_t refcolumn = 0;
  /* 判断显示位置不能超出液晶的边界 */
  while ((refcolumn < LCD_PIXEL_WIDTH) && ((*ptr != 0) & (((refcolumn + DrawProp[ActiveLayer].pFont->Width) & 0xFFFF) >= DrawProp[ActiveLayer].pFont->Width)))
  {
	/* 使用LCD显示一个字符 */
	if ( * ptr <= 126 )	           	//英文字符
	{
				
		LCD_DisplayChar(refcolumn, LINE(Line), *ptr);
		/* 根据字体偏移显示的位置 */
		refcolumn += DrawProp[ActiveLayer].pFont->Width;
		/* 指向字符串中的下一个字符 */
		ptr++;
	}
//	
//	else	                            //汉字字符
//	{	
//		uint16_t usCh;
//		
//		/*一个汉字两字节*/
//		usCh = * ( uint16_t * ) ptr;	
//		/*交换编码顺序*/
//		usCh = ( usCh << 8 ) + ( usCh >> 8 );		
//		
//		/*显示汉字*/
//		LCD_DispChar_CH ( refcolumn,LINE(Line) , usCh );
//		/*显示位置偏移*/
//		refcolumn += WIDTH_CH_CHAR;
//		/* 指向字符串中的下一个字符 */
//		ptr += 2; 		
//    }		
  }
}
/**
 * @brief  在显示器上显示中英文字符串,超出液晶宽度时会自动换行。
					 中英文混显示时，请把英文字体设置为Font16x24格式
 * @param  Line ：行(也可理解为y坐标)
 * @param  Column ：列（也可理解为x坐标）
 * @param  pStr ：要显示的字符串的首地址
 * @retval 无
 */
void LCD_DispString_EN_CH( uint16_t Line, uint16_t Column, const uint8_t * pStr )
{
      /* 判断显示位置不能超出液晶的边界 */
  while ((Column < LCD_PIXEL_WIDTH) && ((*pStr != 0) & (((Column + DrawProp[ActiveLayer].pFont->Width) & 0xFFFF) >= DrawProp[ActiveLayer].pFont->Width)))
  {
		/* 使用LCD显示一个字符 */
		if ( * pStr <= 126 )	           	//英文字符
		{
					
			LCD_DisplayChar(Column, Line, *pStr);
			/* 根据字体偏移显示的位置 */
			Column += DrawProp[ActiveLayer].pFont->Width;
			/* 指向字符串中的下一个字符 */
			pStr++;
		}
		
//		else	                            //汉字字符
//		{	
//			uint16_t usCh;
//			
//			/*一个汉字两字节*/
//			usCh = * ( uint16_t * ) pStr;	
//			/*交换编码顺序*/
//			usCh = ( usCh << 8 ) + ( usCh >> 8 );		
//			
//			/*显示汉字*/
//			LCD_DispChar_CH (Column,Line, usCh );
//			/*显示位置偏移*/
//			Column += WIDTH_CH_CHAR;
//			/* 指向字符串中的下一个字符 */
//			pStr += 2; 		
//	}
  }
}
/**
  * @brief  绘制水平线
  * @param  Xpos: X轴起始坐标
  * @param  Ypos: Y轴起始坐标
  * @param  Length: 线的长度
  * @retval 无
  */
void LCD_DrawHLine(uint16_t Xpos, uint16_t Ypos, uint16_t Length)
{
  uint32_t  Xaddress = 0;

  if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 4*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 3*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if((Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB4444) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_AL88))  
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);   
  }
  else
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);   
  }	
  /* 填充数据 */
  LL_FillBuffer(ActiveLayer, (uint32_t *)Xaddress, Length, 1, 0, DrawProp[ActiveLayer].TextColor);
}

/**
  * @brief  绘制垂直线
  * @param  Xpos: X轴起始坐标
  * @param  Ypos: Y轴起始坐标
  * @param  Length: 线的长度
  * @retval 无
  */
void LCD_DrawVLine(uint16_t Xpos, uint16_t Ypos, uint16_t Length)
{
  uint32_t  Xaddress = 0;
  
  if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 4*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 3*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if((Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB4444) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_AL88))  
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);   
  }
  else
  {
    Xaddress = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);   
  }	
  
  /* 填充数据 */
  LL_FillBuffer(ActiveLayer, (uint32_t *)Xaddress, 1, Length, (LCD_GetXSize() - 1), DrawProp[ActiveLayer].TextColor);
}

/**
  * @brief  指定两点画一条线
  * @param  x1: 第一点X轴坐标
  * @param  y1: 第一点Y轴坐标
  * @param  x2: 第二点X轴坐标
  * @param  y2: 第二点Y轴坐标
  * @retval 无
  */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0, 
  yinc1 = 0, yinc2 = 0, den = 0, num = 0, num_add = 0, num_pixels = 0, 
  curpixel = 0;
  
  deltax = ABS(x2 - x1);        /* 求x轴的绝对值 */
  deltay = ABS(y2 - y1);        /* 求y轴的绝对值 */
  x = x1;                       /* 第一个像素的x坐标起始值 */
  y = y1;                       /* 第一个像素的y坐标起始值 */
  
  if (x2 >= x1)                 /* x坐标值为递增 */
  {
    xinc1 = 1;
    xinc2 = 1;
  }
  else                          /* x坐标值为递减 */
  {
    xinc1 = -1;
    xinc2 = -1;
  }
  
  if (y2 >= y1)                 /* y坐标值为递增 */
  {
    yinc1 = 1;
    yinc2 = 1;
  }
  else                          /* y坐标值为递减 */
  {
    yinc1 = -1;
    yinc2 = -1;
  }
  
  if (deltax >= deltay)         /* 每个 y 坐标值至少有一个x坐标值*/
  {
    xinc1 = 0;                  /* 当分子大于或等于分母时不要改变 x */
    yinc2 = 0;                  /* 不要为每次迭代更改 y */
    den = deltax;
    num = deltax / 2;
    num_add = deltay;
    num_pixels = deltax;         /* x比y多的值 */
  }
  else                          /* 每个 x 坐标值至少有一个y坐标值 */
  {
    xinc2 = 0;                  /* 不要为每次迭代更改 x */
    yinc1 = 0;                  /* 当分子大于或等于分母时不要改变 y */
    den = deltay;
    num = deltay / 2;
    num_add = deltax;
    num_pixels = deltay;         /* y比x多的值 */
  }
  
  for (curpixel = 0; curpixel <= num_pixels; curpixel++)
  {
    LCD_DrawPixel(x, y, DrawProp[ActiveLayer].TextColor);   /* 绘制当前像素点 */
    num += num_add;                            /* 在分数的基础上增加分子 */
    if (num >= den)                           /* 检查分子大于或等于分母 */
    {
      num -= den;                             /* 计算新的分子值 */
      x += xinc1;                             /* x值递增 */
      y += yinc1;                             /* y值递增 */
    }
    x += xinc2;                               /* y值递增 */
    y += yinc2;                               /* y值递增 */
  }
}

/**
  * @brief  绘制一个矩形
  * @param  Xpos:   X轴坐标
  * @param  Ypos:   Y轴坐标
  * @param  Width:  矩形宽度  
  * @param  Height: 矩形高度
  * @retval 无
  */
void LCD_DrawRect(uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height)
{
  /* 绘制水平线 */
  LCD_DrawHLine(Xpos, Ypos, Width);
  LCD_DrawHLine(Xpos, (Ypos+ Height), Width);
  
  /* 绘制垂直线 */
  LCD_DrawVLine(Xpos, Ypos, Height);
  LCD_DrawVLine((Xpos + Width), Ypos, Height);
}

/**
  * @brief  绘制一个圆形
  * @param  Xpos:   X轴坐标
  * @param  Ypos:   Y轴坐标
  * @param  Radius: 圆的半径
  * @retval 无
  */
void LCD_DrawCircle(uint16_t Xpos, uint16_t Ypos, uint16_t Radius)
{
  int32_t   decision;    /* 决策变量 */ 
  uint32_t  current_x;   /* 当前x坐标值 */
  uint32_t  current_y;   /* 当前y坐标值 */
  
  decision = 3 - (Radius << 1);
  current_x = 0;
  current_y = Radius;
  
  while (current_x <= current_y)
  {
    LCD_DrawPixel((Xpos + current_x), (Ypos - current_y), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos - current_x), (Ypos - current_y), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos + current_y), (Ypos - current_x), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos - current_y), (Ypos - current_x), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos + current_x), (Ypos + current_y), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos - current_x), (Ypos + current_y), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos + current_y), (Ypos + current_x), DrawProp[ActiveLayer].TextColor);
    
    LCD_DrawPixel((Xpos - current_y), (Ypos + current_x), DrawProp[ActiveLayer].TextColor);
    
    if (decision < 0)
    { 
      decision += (current_x << 2) + 6;
    }
    else
    {
      decision += ((current_x - current_y) << 2) + 10;
      current_y--;
    }
    current_x++;
  } 
}

/**
  * @brief  绘制一条折线
  * @param  Points: 指向点数组的指针
  * @param  PointCount: 点数
  * @retval 无
  */
void LCD_DrawPolygon(pPoint Points, uint16_t PointCount)
{
  int16_t x = 0, y = 0;
  
  if(PointCount < 2)
  {
    return;
  }
  
  LCD_DrawLine(Points->X, Points->Y, (Points+PointCount-1)->X, (Points+PointCount-1)->Y);
  
  while(--PointCount)
  {
    x = Points->X;
    y = Points->Y;
    Points++;
    LCD_DrawLine(x, y, Points->X, Points->Y);
  }
}

/**
  * @brief  绘制一个椭圆
  * @param  Xpos:   X轴坐标
  * @param  Ypos:   Y轴坐标
  * @param  XRadius: 椭圆X轴半径
  * @param  YRadius: 椭圆Y轴半径
  * @retval 无
  */
void LCD_DrawEllipse(int Xpos, int Ypos, int XRadius, int YRadius)
{
  int x = 0, y = -YRadius, err = 2-2*XRadius, e2;
  float k = 0, rad1 = 0, rad2 = 0;
  
  rad1 = XRadius;
  rad2 = YRadius;
  
  k = (float)(rad2/rad1);  
  
  do { 
    LCD_DrawPixel((Xpos-(uint16_t)(x/k)), (Ypos+y), DrawProp[ActiveLayer].TextColor);
    LCD_DrawPixel((Xpos+(uint16_t)(x/k)), (Ypos+y), DrawProp[ActiveLayer].TextColor);
    LCD_DrawPixel((Xpos+(uint16_t)(x/k)), (Ypos-y), DrawProp[ActiveLayer].TextColor);
    LCD_DrawPixel((Xpos-(uint16_t)(x/k)), (Ypos-y), DrawProp[ActiveLayer].TextColor);      
    
    e2 = err;
    if (e2 <= x) {
      err += ++x*2+1;
      if (-y == x && e2 <= y) e2 = 0;
    }
    if (e2 > y) err += ++y*2+1;     
  }
  while (y <= 0);
}

/**
  * @brief  绘制一个点
  * @param  Xpos:   X轴坐标
  * @param  Ypos:   Y轴坐标
  * @param  RGB_Code: 像素颜色值
  * @retval 无
  */
void LCD_DrawPixel(uint16_t Xpos, uint16_t Ypos, uint32_t RGB_Code)
{
  /* 边界保护：越界直接返回。LCD_FillCircle / DrawLine 在边缘会算出
     负数坐标（uint16 回绕成 6553x），不拦会写到帧缓存之外 → 总线错误卡死 */
  if ((Xpos >= LCD_GetXSize()) || (Ypos >= LCD_GetYSize()))
  {
    return;
  }

  /* 颜色参数统一按 24 位（0x00RRGGBB）传入；
   * RGB565 图层在这里压成 16 位，上层代码切格式时不用改颜色常量 */
  if (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565)
  {
    RGB_Code = (uint32_t)LCD_RGB888to565(RGB_Code);
  }

  if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
  {
    *(__IO uint32_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (4*(Ypos*LCD_GetXSize() + Xpos))) = RGB_Code;
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
  {
    *(__IO uint8_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (3*(Ypos*LCD_GetXSize() + Xpos))+2) = 0xFF&(RGB_Code>>16);
		*(__IO uint8_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (3*(Ypos*LCD_GetXSize() + Xpos))+1) = 0xFF&(RGB_Code>>8);
		*(__IO uint8_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (3*(Ypos*LCD_GetXSize() + Xpos))) = 0xFF&RGB_Code;
  }
  else if((Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB4444) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_AL88))  
  {
    *(__IO uint16_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (2*(Ypos*LCD_GetXSize() + Xpos))) = (uint16_t)RGB_Code;   
  }
  else
  {
    /* L8 / AL44 是 1 字节/像素（原版这里误写成 2 字节），其余未知格式按 16 位处理 */
    if ((Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_L8) ||
        (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_AL44))
    {
      *(__IO uint8_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (1*(Ypos*LCD_GetXSize() + Xpos))) = (uint8_t)RGB_Code;
    }
    else
    {
      *(__IO uint16_t*) (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (2*(Ypos*LCD_GetXSize() + Xpos))) = (uint16_t)RGB_Code;
    }
  }

}

/**
  * @brief  绘制一幅从内部flash加载的ARGB888(32 bits per pixel)格式的图片
  * @param  Xpos: Bmp在液晶上X 轴坐标
  * @param  Ypos: Bmp在液晶上Y 轴坐标
  * @param  pbmp: 指针指向存在内部flash的Bmp图片的首地址
  * @retval 无
  */
void LCD_DrawBitmap(uint32_t Xpos, uint32_t Ypos, uint8_t *pbmp)
{
  uint32_t index = 0, width = 0, height = 0, bit_pixel = 0;
  uint32_t address;
  uint32_t input_color_mode = 0;
  
  /* 获取位图数据的基地址 */
  index = *(__IO uint16_t *) (pbmp + 10);
  index |= (*(__IO uint16_t *) (pbmp + 12)) << 16;
  
  /* 获取位图宽度 */
  width = *(uint16_t *) (pbmp + 18);
  width |= (*(uint16_t *) (pbmp + 20)) << 16;
  
  /* 获取位图高度 */
  height = *(uint16_t *) (pbmp + 22);
  height |= (*(uint16_t *) (pbmp + 24)) << 16; 
  
  /* 获取像素个数 */
  bit_pixel = *(uint16_t *) (pbmp + 28);   
  
  /* 设定地址 */
  address = Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress + (((LCD_GetXSize()*Ypos) + Xpos)*(4));
  
  /*判断层输入像素格式 */    
  if ((bit_pixel/8) == 4)
  {
    input_color_mode = DMA2D_INPUT_ARGB8888;
  }
  else if ((bit_pixel/8) == 2)
  {
    input_color_mode = DMA2D_INPUT_RGB565;   
  }
  else 
  {
    input_color_mode = DMA2D_INPUT_RGB888;
  }
  
  /* 绕过位图的帧头 */
  pbmp += (index + (width * (height - 1) * (bit_pixel/8)));  
  
  /* 将图片转换为 ARGB8888 像素格式 */
  for(index=0; index < height; index++)
  {
    /* 像素格式转换 */
    LL_ConvertLineToARGB8888((uint32_t *)pbmp, (uint32_t *)address, width, input_color_mode);
    
    /* 递增源和目标缓冲区 */
    address+=  (LCD_GetXSize()*4);
    pbmp -= width*(bit_pixel/8);
  } 
}

/**
  * @brief  填充一个实心矩形
  * @param  Xpos: X坐标值
  * @param  Ypos: Y坐标值
  * @param  Width:  矩形宽度 
  * @param  Height: 矩形高度
  * @retval 无
  */
void LCD_FillRect(uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height)
{
  uint32_t  x_address = 0;

  /* 边界裁剪：DMA2D 不会自己判断越界，超出帧缓存会写坏其他数据 */
  if ((Xpos >= LCD_GetXSize()) || (Ypos >= LCD_GetYSize()))
  {
    return;
  }
  if (((uint32_t)Xpos + Width) > LCD_GetXSize())
  {
    Width = (uint16_t)(LCD_GetXSize() - Xpos);
  }
  if (((uint32_t)Ypos + Height) > LCD_GetYSize())
  {
    Height = (uint16_t)(LCD_GetYSize() - Ypos);
  }
  
  /* 设置文字颜色 */
  LCD_SetTextColor(DrawProp[ActiveLayer].TextColor);
  
  /* 设置矩形开始地址 */
    if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
  {
    x_address = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 4*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
  {
    x_address = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 3*(LCD_GetXSize()*Ypos + Xpos);
  }
  else if((Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB4444) || \
          (Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_AL88))  
  {
    x_address = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);   
  }
  else
  {
    x_address = (Ltdc_Handler.LayerCfg[ActiveLayer].FBStartAdress) + 2*(LCD_GetXSize()*Ypos + Xpos);
  }	
  /* 填充矩形 */
  LL_FillBuffer(ActiveLayer, (uint32_t *)x_address, Width, Height, (LCD_GetXSize() - Width), DrawProp[ActiveLayer].TextColor);
}

/**
  * @brief  填充一个实心圆
  * @param  Xpos: X坐标值
  * @param  Ypos: Y坐标值
  * @param  Radius: 圆的半径
  * @retval 无
  */
void LCD_FillCircle(uint16_t Xpos, uint16_t Ypos, uint16_t Radius)
{
  int32_t   decision;    /* 决策变量 */ 
  uint32_t  current_x;   /* 当前x坐标值 */
  uint32_t  current_y;   /* 当前y坐标值 */
  
  decision = 3 - (Radius << 1);
  
  current_x = 0;
  current_y = Radius;
  
  LCD_SetTextColor(DrawProp[ActiveLayer].TextColor);
  
  while (current_x <= current_y)
  {
    if(current_y > 0) 
    {
      LCD_DrawHLine(Xpos - current_y, Ypos + current_x, 2*current_y);
      LCD_DrawHLine(Xpos - current_y, Ypos - current_x, 2*current_y);
    }
    
    if(current_x > 0) 
    {
      LCD_DrawHLine(Xpos - current_x, Ypos - current_y, 2*current_x);
      LCD_DrawHLine(Xpos - current_x, Ypos + current_y, 2*current_x);
    }
    if (decision < 0)
    { 
      decision += (current_x << 2) + 6;
    }
    else
    {
      decision += ((current_x - current_y) << 2) + 10;
      current_y--;
    }
    current_x++;
  }
  
  LCD_SetTextColor(DrawProp[ActiveLayer].TextColor);
  LCD_DrawCircle(Xpos, Ypos, Radius);
}

/**
  * @brief  绘制多边形
  * @param  Points: 指向点数组的指针
  * @param  PointCount: 点数
  * @retval 无
  */
void LCD_FillPolygon(pPoint Points, uint16_t PointCount)
{
  int16_t X = 0, Y = 0, X2 = 0, Y2 = 0, X_center = 0, Y_center = 0, X_first = 0, Y_first = 0, pixelX = 0, pixelY = 0, counter = 0;
  uint16_t  image_left = 0, image_right = 0, image_top = 0, image_bottom = 0;
  
  image_left = image_right = Points->X;
  image_top= image_bottom = Points->Y;
  
  for(counter = 1; counter < PointCount; counter++)
  {
    pixelX = POLY_X(counter);
    if(pixelX < image_left)
    {
      image_left = pixelX;
    }
    if(pixelX > image_right)
    {
      image_right = pixelX;
    }
    
    pixelY = POLY_Y(counter);
    if(pixelY < image_top)
    { 
      image_top = pixelY;
    }
    if(pixelY > image_bottom)
    {
      image_bottom = pixelY;
    }
  }  
  
  if(PointCount < 2)
  {
    return;
  }
  
  X_center = (image_left + image_right)/2;
  Y_center = (image_bottom + image_top)/2;
  
  X_first = Points->X;
  Y_first = Points->Y;
  
  while(--PointCount)
  {
    X = Points->X;
    Y = Points->Y;
    Points++;
    X2 = Points->X;
    Y2 = Points->Y;    
    
    FillTriangle(X, X2, X_center, Y, Y2, Y_center);
    FillTriangle(X, X_center, X2, Y, Y_center, Y2);
    FillTriangle(X_center, X2, X, Y_center, Y2, Y);   
  }
  
  FillTriangle(X_first, X2, X_center, Y_first, Y2, Y_center);
  FillTriangle(X_first, X_center, X2, Y_first, Y_center, Y2);
  FillTriangle(X_center, X2, X_first, Y_center, Y2, Y_first);   
}

/**
  * @brief  填充一个实心椭圆
  * @param  Xpos:   X轴坐标
  * @param  Ypos:   Y轴坐标
  * @param  XRadius: 椭圆X轴半径
  * @param  YRadius: 椭圆Y轴半径
  * @retval 无
  */
void LCD_FillEllipse(int Xpos, int Ypos, int XRadius, int YRadius)
{
  int x = 0, y = -YRadius, err = 2-2*XRadius, e2;
  float k = 0, rad1 = 0, rad2 = 0;
  
  rad1 = XRadius;
  rad2 = YRadius;
  
  k = (float)(rad2/rad1);
  
  do 
  {       
    LCD_DrawHLine((Xpos-(uint16_t)(x/k)), (Ypos+y), (2*(uint16_t)(x/k) + 1));
    LCD_DrawHLine((Xpos-(uint16_t)(x/k)), (Ypos-y), (2*(uint16_t)(x/k) + 1));
    
    e2 = err;
    if (e2 <= x) 
    {
      err += ++x*2+1;
      if (-y == x && e2 <= y) e2 = 0;
    }
    if (e2 > y) err += ++y*2+1;
  }
  while (y <= 0);
}

/**
  * @brief  使能显示
  * @retval 无
  */
void LCD_DisplayOn(void)
{
  /* 开显示 */
  __HAL_LTDC_ENABLE(&Ltdc_Handler);
//  HAL_GPIO_WritePin(LTDC_DISP_GPIO_PORT, LTDC_DISP_GPIO_PIN, GPIO_PIN_SET);/* LCD_DISP使能*/
  /* 背光由应用层通过 TIM2_CH3 PWM 控制（PA2），这里不碰 GPIO */
}

/**
  * @brief  禁能显示
  * @retval 无
  */
void LCD_DisplayOff(void)
{
  /* 关显示 */
  __HAL_LTDC_DISABLE(&Ltdc_Handler);
//  HAL_GPIO_WritePin(LTDC_DISP_GPIO_PORT, LTDC_DISP_GPIO_PIN, GPIO_PIN_RESET); /* LCD_DISP禁能*/
  /* 背光由应用层控制，这里只管 LTDC */
}


/*******************************************************************************
                            局部函数
*******************************************************************************/

/**
  * @brief  显示一个字符
  * @param  Xpos: 显示字符的行位置
  * @param  Ypos: 列起始位置
  * @param  c: 指向字体数据的指针
  * @retval 无
  */
static void DrawChar(uint16_t Xpos, uint16_t Ypos, const uint8_t *c)
{
  uint32_t i = 0, j = 0;
  uint16_t height, width;
  uint8_t  offset;
  uint8_t  *pchar;
  uint32_t line;
  
  height = DrawProp[ActiveLayer].pFont->Height;//获取正在使用字体高度
  width  = DrawProp[ActiveLayer].pFont->Width; //获取正在使用字体宽度
  
  offset =  8 *((width + 7)/8) -  width ;//计算字符的每一行像素的偏移值，实际存储大小-字体宽度
  
  for(i = 0; i < height; i++)//遍历字体高度绘点
  {
    pchar = ((uint8_t *)c + (width + 7)/8 * i);//计算字符的每一行像素的偏移地址
    
    switch(((width + 7)/8))//根据字体宽度来提取不同字体的实际像素值
    {
      
    case 1:
      line =  pchar[0];      //提取字体宽度小于8的字符的像素值
      break;
      
    case 2:
      line =  (pchar[0]<< 8) | pchar[1]; //提取字体宽度大于8小于16的字符的像素值     
      break;
      
    case 3:
    default:
      line =  (pchar[0]<< 16) | (pchar[1]<< 8) | pchar[2]; //提取字体宽度大于16小于24的字符的像素值     
      break;
    } 
    
    for (j = 0; j < width; j++)//遍历字体宽度绘点
    {
      if(line & (1 << (width- j + offset- 1))) //根据每一行的像素值及偏移位置按照当前字体颜色进行绘点
      {
        LCD_DrawPixel((Xpos + j), Ypos, DrawProp[ActiveLayer].TextColor);
      }
      else//如果这一行没有字体像素则按照背景颜色绘点
      {
        LCD_DrawPixel((Xpos + j), Ypos, DrawProp[ActiveLayer].BackColor);
      } 
    }
    Ypos++;
  }
}

/**
  * @brief  填充三角形（基于三点）
  * @param  x1: 第一点的X坐标值
  * @param  y1: 第一点的Y坐标值
  * @param  x2: 第二点的X坐标值
  * @param  y2: 第二点的Y坐标值
  * @param  x3: 第三点的X坐标值
  * @param  y3: 第三点的Y坐标值
  * @retval 无
  */
static void FillTriangle(uint16_t x1, uint16_t x2, uint16_t x3, uint16_t y1, uint16_t y2, uint16_t y3)
{ 
  int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0, 
  yinc1 = 0, yinc2 = 0, den = 0, num = 0, num_add = 0, num_pixels = 0,
  curpixel = 0;
  
  deltax = ABS(x2 - x1);        /* 求x轴的绝对值 */
  deltay = ABS(y2 - y1);        /* 求y轴的绝对值 */
  x = x1;                       /* 第一个像素的x坐标起始值 */
  y = y1;                       /* 第一个像素的y坐标起始值 */
  
  if (x2 >= x1)                 /* x坐标值为递增*/
  {
    xinc1 = 1;
    xinc2 = 1;
  }
  else                          /* x坐标值为递减 */
  {
    xinc1 = -1;
    xinc2 = -1;
  }
  
  if (y2 >= y1)                 /* y坐标值为递增*/
  {
    yinc1 = 1;
    yinc2 = 1;
  }
  else                          /* y坐标值为递减 */
  {
    yinc1 = -1;
    yinc2 = -1;
  }
  
  if (deltax >= deltay)         /* 每个 y 坐标值至少有一个x坐标值*/
  {
    xinc1 = 0;                  /* 当分子大于或等于分母时不要改变 x */
    yinc2 = 0;                  /* 不要为每次迭代更改 y */
    den = deltax;
    num = deltax / 2;
    num_add = deltay;
    num_pixels = deltax;         /* x比y多的值 */
  }
  else                          /* 每个 x 坐标值至少有一个y坐标值 */
  {
    xinc2 = 0;                  /* 不要为每次迭代更改 x */
    yinc1 = 0;                  /* 当分子大于或等于分母时不要改变 y */
    den = deltay;
    num = deltay / 2;
    num_add = deltax;
    num_pixels = deltay;         /* y比x多的值 */
  }
  
  for (curpixel = 0; curpixel <= num_pixels; curpixel++)
  {
    LCD_DrawLine(x, y, x3, y3);
    
    num += num_add;              /* 在分数的基础上增加分子 */
    if (num >= den)             /* 判断分子是否大于或等于分母 */
    {
      num -= den;               /* 计算新的分子值 */
      x += xinc1;               /* x值递增 */
      y += yinc1;               /* y值递增 */
    }
    x += xinc2;                 /* x值递增 */
    y += yinc2;                 /* y值递增 */
  } 
}

/**
  * @brief  填充一个缓冲区
  * @param  LayerIndex: 当前层
  * @param  pDst: 指向目标缓冲区指针
  * @param  xSize: 缓冲区宽度
  * @param  ySize: 缓冲区高度
  * @param  OffLine: 偏移量
  * @param  ColorIndex: 当前颜色
  * @retval None
  */

/* RGB888(0x00RRGGBB) -> RGB565
 * 约定：本工程所有绘图 API 的颜色参数都是 24 位 0x00RRGGBB，
 *       图层是 RGB565 时由这里自动压成 16 位。 */
uint16_t LCD_RGB888to565(uint32_t c)
{
  uint32_t r = (c >> 16) & 0xFFU;
  uint32_t g = (c >>  8) & 0xFFU;
  uint32_t b =  c        & 0xFFU;

  return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | ((b & 0xF8U) >> 3));
}

static void LL_FillBuffer(uint32_t LayerIndex, void *pDst, uint32_t xSize, uint32_t ySize, uint32_t OffLine, uint32_t ColorIndex) 
{

  Dma2d_Handler.Init.Mode         = DMA2D_R2M;
  if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565)
  { 
    Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
  { 
    Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_ARGB8888;
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
  { 
    Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_RGB888;
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB1555)
  { 
    Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_ARGB1555;
  }
  else if(Ltdc_Handler.LayerCfg[ActiveLayer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB4444)
  { 
    Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_ARGB4444;
  }
  Dma2d_Handler.Init.OutputOffset = OffLine;      
  
  /* 颜色统一按 24 位（0x00RRGGBB）传入，这里不做任何转换。
   * HAL_DMA2D_Start() -> DMA2D_SetConfig() 内部会按 ColorMode 处理：
   *   RGB565  输出 → (R>>19) | (G>>10) | (B>>3)  自动压成 16 位
   *   ARGB8888 输出 → 原样写入 OCOLR
   * 所以上层永远传 24 位即可。 */

  Dma2d_Handler.Instance = DMA2D;
  
  /* DMA2D 初始化 */
  if(HAL_DMA2D_Init(&Dma2d_Handler) == HAL_OK) 
  {
    if(HAL_DMA2D_ConfigLayer(&Dma2d_Handler, LayerIndex) == HAL_OK) 
    {
      if (HAL_DMA2D_Start(&Dma2d_Handler, ColorIndex, (uint32_t)pDst, xSize, ySize) == HAL_OK)
      {
        /* DMA轮询传输 */  
        HAL_DMA2D_PollForTransfer(&Dma2d_Handler, 200);   /* 超时 10ms 太小：全屏填充要 15ms+，会提前返回 */
      }
    }
  } 
}

/**
  * @brief  转换一行为ARGB8888像素格式
  * @param  pSrc: 指向源缓冲区的指针
  * @param  pDst: 输出颜色
  * @param  xSize: 缓冲区宽度
  * @param  ColorMode: 输入颜色模式   
  * @retval 无
  */
static void LL_ConvertLineToARGB8888(void *pSrc, void *pDst, uint32_t xSize, uint32_t ColorMode)
{    
  /* 配置DMA2D模式,颜色模式和输出偏移 */
  Dma2d_Handler.Init.Mode         = DMA2D_M2M_PFC;
  Dma2d_Handler.Init.ColorMode    = DMA2D_OUTPUT_ARGB8888;
  Dma2d_Handler.Init.OutputOffset = 0;     
  
  /* Foreground Configuration */
  Dma2d_Handler.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  Dma2d_Handler.LayerCfg[1].InputAlpha = 0xFF;
  Dma2d_Handler.LayerCfg[1].InputColorMode = ColorMode;
  Dma2d_Handler.LayerCfg[1].InputOffset = 0;
  
  Dma2d_Handler.Instance = DMA2D; 
  
  /* DMA2D 初始化 */
  if(HAL_DMA2D_Init(&Dma2d_Handler) == HAL_OK) 
  {
    if(HAL_DMA2D_ConfigLayer(&Dma2d_Handler, 1) == HAL_OK) 
    {
      if (HAL_DMA2D_Start(&Dma2d_Handler, (uint32_t)pSrc, (uint32_t)pDst, xSize, 1) == HAL_OK)
      {
        /* DMA轮询传输*/  
        HAL_DMA2D_PollForTransfer(&Dma2d_Handler, 200);   /* 超时 10ms 太小：全屏填充要 15ms+，会提前返回 */
      }
    }
  } 
}
//#if GBKCODE_FLASH

///*使用FLASH字模*/

////中文字库存储在FLASH的起始地址 ：
////GBKCODE_START_ADDRESS 在fonts.h文件定义
///**
//  * @brief  获取FLASH中文显示字库数据
//	* @param  pBuffer:存储字库矩阵的缓冲区
//	* @param  c ： 要获取的文字
//  * @retval None.
//  */
//int GetGBKCode_from_EXFlash( uint8_t * pBuffer, uint16_t c)
//{ 
//	unsigned char High8bit,Low8bit;
//	unsigned int pos;

//	static uint8_t everRead=0;

//	/*第一次使用，初始化FLASH*/
//	if(everRead == 0)
//	{
//		QSPI_FLASH_Init();
//		everRead = 1;
//	}

//	High8bit= c >> 8;     /* 取高8位数据 */
//	Low8bit= c & 0x00FF;  /* 取低8位数据 */		

//	/*GB2312 公式*/
//	pos = ((High8bit-0xa1)*94+Low8bit-0xa1)*24*24/8;
//	BSP_QSPI_Read(pBuffer,GBKCODE_START_ADDRESS+pos,24*24/8); //读取字库数据  
//	//	  printf ( "%02x %02x %02x %02x\n", pBuffer[0],pBuffer[1],pBuffer[2],pBuffer[3]);

//	return 0;  

//}

//#else

///*使用SD字模*/

//static FIL fnew;													/* file objects */
//static FATFS fs;													/* Work area (file system object) for logical drives */
//static FRESULT res_sd; 
//static UINT br;            					/* File R/W count */

////字库文件存储位置，fonts.h中的宏：
////#define GBKCODE_FILE_NAME			"0:/Font/GB2312_H2424.FON"

///**
//  * @brief  获取SD卡中文显示字库数据
//	* @param  pBuffer:存储字库矩阵的缓冲区
//	* @param  c ： 要获取的文字
//  * @retval None.
//  */
//int GetGBKCode_from_sd ( uint8_t * pBuffer, uint16_t c)
//{ 
//    unsigned char High8bit,Low8bit;
//    unsigned int pos;
//		
//		static uint8_t everRead = 0;
//	
//    High8bit= c >> 8;     /* 取高8位数据 */
//    Low8bit= c & 0x00FF;  /* 取低8位数据 */
//		
//    pos = ((High8bit-0xa1)*94+Low8bit-0xa1)*24*24/8;
//	
//	/*第一次使用，挂载文件系统，初始化sd*/
//	if(everRead == 0)
//	{
//		res_sd = f_mount(&fs,"0:",1);
//		everRead = 1;

//	}
//		
//    res_sd = f_open(&fnew , GBKCODE_FILE_NAME, FA_OPEN_EXISTING | FA_READ);
//    
//    if ( res_sd == FR_OK ) 
//    {
//        f_lseek (&fnew, pos);		//指针偏移
//        res_sd = f_read( &fnew, pBuffer, 24*24/8, &br );		 //24*24大小的汉字 其字模 占用24*24/8个字节
//        
//        f_close(&fnew);
//        
//        return 0;  
//    }    
//    else
//        return -1;    
//}

//#endif
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/


/* ============================================================================
 *                              背光控制
 * ============================================================================ */

/* 背光硬件绑定：换板子只改这两行 */
#define LCD_BACKLIGHT_TIMER         (&htim2)                /* 背光 PWM 定时器 */
#define LCD_BACKLIGHT_CHANNEL       (TIM_CHANNEL_3)         /* 背光 PWM 通道   */
#define LCD_BACKLIGHT_PERCENT_MAX   (100U)                  /* 亮度百分比上限  */

/**
  * @brief  启动背光 PWM 输出
  * @retval 无
  */
void BSP_LCD_Backlight_Init(void)
{
  (void)HAL_TIM_PWM_Start(LCD_BACKLIGHT_TIMER, LCD_BACKLIGHT_CHANNEL);
}

/**
  * @brief  设置背光亮度
  * @param  percent：0 ~ 100，超过 100 按 100 处理
  * @retval 无
  */
void BSP_LCD_Backlight_Set(uint8_t percent)
{
  uint32_t period;
  uint32_t compare;

  if (percent > LCD_BACKLIGHT_PERCENT_MAX)
  {
    percent = (uint8_t)LCD_BACKLIGHT_PERCENT_MAX;
  }

  period = LCD_BACKLIGHT_TIMER->Init.Period;

  /* 占空比 = compare / (period + 1)：
   * 100% 时 compare = period + 1，输出恒高；
   * 0%   时 compare = 0，输出恒低。 */
  compare = (((period + 1U) * (uint32_t)percent) / LCD_BACKLIGHT_PERCENT_MAX);

  __HAL_TIM_SET_COMPARE(LCD_BACKLIGHT_TIMER, LCD_BACKLIGHT_CHANNEL, compare);
}

/**
  * @brief  读取当前背光亮度
  * @retval 亮度百分比 0 ~ 100
  */
uint8_t BSP_LCD_Backlight_Get(void)
{
  uint32_t period;
  uint32_t compare;

  period  = LCD_BACKLIGHT_TIMER->Init.Period;
  compare = __HAL_TIM_GET_COMPARE(LCD_BACKLIGHT_TIMER, LCD_BACKLIGHT_CHANNEL);

  if (compare > (period + 1U))
  {
    compare = (period + 1U);
  }

  return (uint8_t)(((compare * LCD_BACKLIGHT_PERCENT_MAX) / (period + 1U)));
}
