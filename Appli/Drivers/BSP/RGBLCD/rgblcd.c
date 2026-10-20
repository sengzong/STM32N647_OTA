/**
 ****************************************************************************************************
 * @file        rgblcd.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       RGB LCD驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "rgblcd.h"
#include "rgblcdfont.h"

/* LTDC句柄 */
extern LTDC_HandleTypeDef hltdc;

/* DMA2D句柄 */
extern DMA2D_HandleTypeDef hdma2d;

/* 绘制LCD时的背景色 */
uint32_t g_back_color = 0xFFFF;

/* RGB LCD重要参数 */
_rgblcd_dev rgblcddev;

/* LTDC帧缓冲区(内部 SRAM, 768KB, 不再依赖外部 PSRAM) */
uint16_t g_ltdc_lcd_framebuf[800 * 480];

/* 函数声明 */
static uint16_t rgblcd_panelid_read(void);
static uint8_t rgblcd_ltdc_clk_set(uint32_t clock);
static uint32_t rgblcd_pow(uint8_t m, uint8_t n);

/**
 * @brief   初始化RGB LED
 * @param   无
 * @retval  无
 */
void rgblcd_init(void)
{
    LTDC_LayerCfgTypeDef ltdc_layer_cfg_struct = {0};

    rgblcddev.id = rgblcd_panelid_read();
    if (rgblcddev.id == 0x4342)         /* ATK-MD0430R-480272 */
    {
        rgblcddev.pwidth = 480;         /* LCD面板的宽度 */
        rgblcddev.pheight = 272;        /* LCD面板的高度 */
        rgblcddev.hsw = 1;              /* 水平同步宽度 */
        rgblcddev.vsw = 1;              /* 垂直同步宽度 */
        rgblcddev.hbp = 40;             /* 水平后廊 */
        rgblcddev.vbp = 8;              /* 垂直后廊 */
        rgblcddev.hfp = 5;              /* 水平前廊 */
        rgblcddev.vfp = 8;              /* 垂直前廊 */
    }
    else if (rgblcddev.id == 0x7084)    /* ATK-MD0700R-800480 */
    {
        rgblcddev.pwidth = 800;         /* LCD面板的宽度 */
        rgblcddev.pheight = 480;        /* LCD面板的高度 */
        rgblcddev.hsw = 1;              /* 水平同步宽度 */
        rgblcddev.vsw = 1;              /* 垂直同步宽度 */
        rgblcddev.hbp = 46;             /* 水平后廊 */
        rgblcddev.vbp = 23;             /* 垂直后廊 */
        rgblcddev.hfp = 210;            /* 水平前廊 */
        rgblcddev.vfp = 22;             /* 垂直前廊 */
    }
    else if (rgblcddev.id == 0x7016)    /* ATK-MD0700R-1024600 */
    {
        rgblcddev.pwidth = 1024;        /* LCD面板的宽度 */
        rgblcddev.pheight = 600;        /* LCD面板的高度 */
        rgblcddev.hsw = 20;             /* 水平同步宽度 */
        rgblcddev.vsw = 3;              /* 垂直同步宽度 */
        rgblcddev.hbp = 140;            /* 水平后廊 */
        rgblcddev.vbp = 20;             /* 垂直后廊 */
        rgblcddev.hfp = 160;            /* 水平前廊 */
        rgblcddev.vfp = 12;             /* 垂直前廊 */
    }
    else if (rgblcddev.id == 0x7018)    /* ATK-MD0700R-1280800 */
    {
        rgblcddev.pwidth = 1280;        /* LCD面板的宽度 */
        rgblcddev.pheight = 800;        /* LCD面板的高度 */
        /* 其他参数待定 */
    }
    else if (rgblcddev.id == 0x4384)    /* ATK-MD0430R-800480 */
    {
        rgblcddev.pwidth = 800;         /* LCD面板的宽度 */
        rgblcddev.pheight = 480;        /* LCD面板的高度 */
        rgblcddev.hsw = 88;             /* 水平同步宽度 */
        rgblcddev.vsw = 40;             /* 垂直同步宽度 */
        rgblcddev.hbp = 48;             /* 水平后廊 */
        rgblcddev.vbp = 32;             /* 垂直后廊 */
        rgblcddev.hfp = 13;             /* 水平前廊 */
        rgblcddev.vfp = 3;              /* 垂直前廊 */
    }
    else if (rgblcddev.id == 0x1018)    /* ATK-MD1018R-1280800 */
    {
        rgblcddev.pwidth = 1280;        /* LCD面板的宽度 */
        rgblcddev.pheight = 800;        /* LCD面板的高度 */
        rgblcddev.hsw = 140;            /* 水平同步宽度 */
        rgblcddev.vsw = 10;             /* 垂直同步宽度 */
        rgblcddev.hbp = 10;             /* 水平后廊 */
        rgblcddev.vbp = 10;             /* 垂直后廊 */
        rgblcddev.hfp = 10;             /* 水平前廊 */
        rgblcddev.vfp = 3;              /* 垂直前廊 */
    }

    hltdc.Init.PCPolarity = (rgblcddev.id == 0x1018) ? LTDC_PCPOLARITY_IIPC : LTDC_PCPOLARITY_IPC;
    hltdc.Init.HorizontalSync = rgblcddev.hsw - 1;
    hltdc.Init.VerticalSync = rgblcddev.vsw - 1;
    hltdc.Init.AccumulatedHBP = rgblcddev.hsw + rgblcddev.hbp - 1;
    hltdc.Init.AccumulatedVBP = rgblcddev.vsw + rgblcddev.vbp - 1;
    hltdc.Init.AccumulatedActiveW = rgblcddev.hsw + rgblcddev.hbp + rgblcddev.pwidth - 1;
    hltdc.Init.AccumulatedActiveH = rgblcddev.vsw + rgblcddev.vbp + rgblcddev.pheight - 1;
    hltdc.Init.TotalWidth = rgblcddev.hsw + rgblcddev.hbp + rgblcddev.pwidth + rgblcddev.hfp - 1;
    hltdc.Init.TotalHeigh = rgblcddev.vsw + rgblcddev.vbp + rgblcddev.pheight + rgblcddev.vfp - 1;
    HAL_LTDC_DeInit(&hltdc);
    HAL_LTDC_Init(&hltdc);

    if (rgblcddev.id == 0x4342) /* ATK-MD0430R-480272 */
    {
        rgblcd_ltdc_clk_set(9000000);   /* LTDC_CLK = 9MHz */
    }
    else if (rgblcddev.id == 0x7084)    /* ATK-MD0700R-800480 */
    {
        rgblcd_ltdc_clk_set(33333333);  /* LTDC_CLK = 33MHz (800x480 面板规格, 与 ATK/参考一致; 50MHz 超品面规格->蓝屏) */
    }
    else if (rgblcddev.id == 0x7016)    /* ATK-MD0700R-1024600 */
    {
        rgblcd_ltdc_clk_set(40000000);  /* LTDC_CLK = 40MHz */
    }
    else if (rgblcddev.id == 0x7018)    /* ATK-MD0700R-1280800 */
    {
        /* 参数待定 */
    }
    else if (rgblcddev.id == 0x4384)    /* ATK-MD0430R-800480 */
    {
        rgblcd_ltdc_clk_set(33333333);  /* LTDC_CLK = 33MHz (800x480 面板规格, 与 ATK/参考一致; 50MHz 超品面规格->蓝屏) */
    }
    else if (rgblcddev.id == 0x1018)    /* ATK-MD1018R-1280800 */
    {
        rgblcd_ltdc_clk_set(45000000);  /* LTDC_CLK = 45MHz */
    }

    ltdc_layer_cfg_struct.WindowX0 = 0;
    ltdc_layer_cfg_struct.WindowX1 = rgblcddev.pwidth;
    ltdc_layer_cfg_struct.WindowY0 = 0;
    ltdc_layer_cfg_struct.WindowY1 = rgblcddev.pheight;
    ltdc_layer_cfg_struct.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    ltdc_layer_cfg_struct.Alpha = 255;
    ltdc_layer_cfg_struct.Alpha0 = 0;
    ltdc_layer_cfg_struct.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    ltdc_layer_cfg_struct.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    ltdc_layer_cfg_struct.FBStartAdress = 0;
    ltdc_layer_cfg_struct.ImageWidth = rgblcddev.pwidth;
    ltdc_layer_cfg_struct.ImageHeight = rgblcddev.pheight;
    ltdc_layer_cfg_struct.Backcolor.Blue = 0;
    ltdc_layer_cfg_struct.Backcolor.Green = 0;
    ltdc_layer_cfg_struct.Backcolor.Red = 0;
    HAL_LTDC_ConfigLayer(&hltdc, &ltdc_layer_cfg_struct, 0);
    HAL_LTDC_SetAddress(&hltdc, (uint32_t)g_ltdc_lcd_framebuf, 0);

    rgblcd_display_dir(0);
    rgblcd_clear(0x08A4); /* Dark background before LVGL draws its first frame. */
    RGBLCD_BL(1);
}

/**
 * @brief   设置RGB LCD显示方向
 * @param   dir: RGB LCD显示方向
 * @arg     0: 竖屏
 * @arg     1: 横屏
 * @retval  无
 */
void rgblcd_display_dir(uint8_t dir)
{
    rgblcddev.dir = dir;
    if (rgblcddev.dir != 0)
    {
        rgblcddev.width = rgblcddev.pwidth;
        rgblcddev.height = rgblcddev.pheight;
    }
    else
    {
        rgblcddev.width = rgblcddev.pheight;
        rgblcddev.height = rgblcddev.pwidth;
    }
}

/**
 * @brief   在RGB LCD指定区域内填充单个颜色
 * @param   sx: 指定区域的起始X坐标
 * @param   sy: 指定区域的起始Y坐标
 * @param   ex: 指定区域的结束X坐标
 * @param   ey: 指定区域的结束Y坐标
 * @param   color: 要填充的颜色
 * @retval  无
 */
void rgblcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
#define CONVERTRGB5652ARGB8888(Color)                                               \
            ((((((((Color) >> (11U)) & 0x1FU) * 527U) + 23U) >> (6U)) << (16U)) |   \
            (((((((Color) >> (5U)) & 0x3FU) * 259U) + 33U) >> (6U)) << (8U)) |      \
            (((((Color) & 0x1FU) * 527U) + 23U) >> (6U)) | (0xFF000000U))

    uint16_t psx;
    uint16_t psy;
    uint16_t pex;
    uint16_t pey;

    if (rgblcddev.dir == 0)
    {
        psx = sy;
        psy = rgblcddev.pheight - ex - 1;
        pex = ey;
        pey = rgblcddev.pheight - sx - 1;
    }
    else
    {
        psx = sx;
        psy = sy;
        pex = ex;
        pey = ey;
    }

    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.OutputOffset = rgblcddev.pwidth - (pex - psx + 1);
    HAL_DMA2D_Init(&hdma2d);
    HAL_DMA2D_Start(&hdma2d, CONVERTRGB5652ARGB8888(color), (uint32_t)&g_ltdc_lcd_framebuf[psy * rgblcddev.pwidth + psx], pex - psx + 1, pey - psy + 1);
    HAL_DMA2D_PollForTransfer(&hdma2d, 50);
}

/**
 * @brief   在RGB LCD指定区域内填充指定颜色块
 * @param   sx: 指定区域的起始X坐标
 * @param   sy: 指定区域的起始Y坐标
 * @param   ex: 指定区域的结束X坐标
 * @param   ey: 指定区域的结束Y坐标
 * @param   color: 指定颜色数组的首地址
 * @retval  无
 */
void rgblcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint16_t psx;
    uint16_t psy;
    uint16_t pex;
    uint16_t pey;

    if (rgblcddev.dir == 0)
    {
        psx = sy;
        psy = rgblcddev.pheight - ex - 1;
        pex = ey;
        pey = rgblcddev.pheight - sx - 1;
    }
    else
    {
        psx = sx;
        psy = sy;
        pex = ex;
        pey = ey;
    }

    hdma2d.Init.Mode = DMA2D_M2M;
    hdma2d.Init.OutputOffset = rgblcddev.pwidth - (pex - psx + 1);
    HAL_DMA2D_Init(&hdma2d);
    HAL_DMA2D_Start(&hdma2d, (uint32_t)color, (uint32_t)&g_ltdc_lcd_framebuf[psy * rgblcddev.pwidth + psx], pex - psx + 1, pey - psy + 1);
    HAL_DMA2D_PollForTransfer(&hdma2d, 50);
}

/**
 * @brief   RGB LCD画点
 * @param   x: 点的X坐标
 * @param   y: 点的Y坐标
 * @param   color: 点的颜色
 * @retval  无
 */
void rgblcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t px;
    uint16_t py;

    if (rgblcddev.dir == 0)
    {
        px = y;
        py = rgblcddev.pheight - x - 1;
    }
    else
    {
        px = x;
        py = y;
    }

    g_ltdc_lcd_framebuf[rgblcddev.pwidth * py + px] = color;
}

/**
 * @brief   RGB LCD读点
 * @param   x: 点的X坐标
 * @param   y: 点的Y坐标
 * @retval  点的颜色
 */
uint16_t rgblcd_read_point(uint16_t x, uint16_t y)
{
    uint16_t px;
    uint16_t py;

    if (rgblcddev.dir == 0)
    {
        px = y;
        py = rgblcddev.pheight - x - 1;
    }
    else
    {
        px = x;
        py = y;
    }

    g_ltdc_lcd_framebuf[rgblcddev.pwidth * py + px] = g_ltdc_lcd_framebuf[rgblcddev.pwidth * py + px];

    return g_ltdc_lcd_framebuf[rgblcddev.pwidth * py + px];
}

/**
 * @brief   RGB LCD清屏
 * @param   color: 清屏的颜色
 * @retval  无
 */
void rgblcd_clear(uint16_t color)
{
    rgblcd_fill(0, 0, rgblcddev.width - 1, rgblcddev.height - 1, color);
}

/**
 * @brief   RGB LCD画线
 * @param   x1   : 线的起始X坐标
 * @param   y1   : 线的起始Y坐标
 * @param   x2   : 线的结束X坐标
 * @param   y2   : 线的结束Y坐标
 * @param   color: 线的颜色
 * @retval  无
 */
void rgblcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0;
    int yerr = 0;
    int delta_x;
    int delta_y;
    int distance;
    int incx;
    int incy;
    int row;
    int col;

    /* 计算坐标增量 */
    delta_x = x2 - x1;
    delta_y = y2 - y1;

    row = x1;
    col = y1;

    /* 设置X单步方向 */
    if (delta_x > 0)
    {
        incx = 1;
    }
    else if (delta_x == 0)
    {
        incx = 0;
    }
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    /* 设置Y单步方向 */
    if (delta_y > 0)
    {
        incy = 1;
    }
    else if (delta_y == 0)
    {
        incy = 0;
    }
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    /* 选取基本增量坐标轴 */
    if (delta_x > delta_y)
    {
        distance = delta_x;
    }
    else
    {
        distance = delta_y;
    }

    for (t = 0; t <= (distance + 1); t++)
    {
        rgblcd_draw_point(row, col, color);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }
        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief   RGB LCD画水平线
 * @param   x    : 线的起始X坐标
 * @param   y    : 线的起始Y坐标
 * @param   len  : 线的长度
 * @param   color: 线的颜色
 * @retval  无
 */
void rgblcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if ((len == 0) || (x > rgblcddev.width) || (y > rgblcddev.height))
    {
        return;
    }

    rgblcd_fill(x, y, x + len - 1, y, color);
}

/**
 * @brief   RGB LCD画矩形
 * @param   x1   : 矩形左上角X坐标
 * @param   y1   : 矩形左上角Y坐标
 * @param   x2   : 矩形右下角X坐标
 * @param   y2   : 矩形右下角Y坐标
 * @param   color: 矩形的颜色
 * @retval  无
 */
void rgblcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    rgblcd_draw_line(x1, y1, x2, y1, color);
    rgblcd_draw_line(x1, y1, x1, y2, color);
    rgblcd_draw_line(x1, y2, x2, y2, color);
    rgblcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief   RGB LCD画圆
 * @param   x0   : 圆心的X坐标
 * @param   y0   : 圆心的Y坐标
 * @param   r    : 圆的半径
 * @param   color: 圆的颜色
 * @retval  无
 */
void rgblcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a;
    int b;
    int di;

    a = 0;
    b = r;
    di = 3 - (r << 1);  /* 判断下个点位置的标志 */

    while (a <= b)      /* 使用Bresenham算法画圆 */
    {
        rgblcd_draw_point(x0 + a, y0 - b, color);
        rgblcd_draw_point(x0 + b, y0 - a, color);
        rgblcd_draw_point(x0 + b, y0 + a, color);
        rgblcd_draw_point(x0 + a, y0 + b, color);
        rgblcd_draw_point(x0 - a, y0 + b, color);
        rgblcd_draw_point(x0 - b, y0 + a, color);
        rgblcd_draw_point(x0 - a, y0 - b, color);
        rgblcd_draw_point(x0 - b, y0 - a, color);
        a++;
        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/**
 * @brief   RGB LCD画实心圆
 * @param   x    : 圆心的X坐标
 * @param   y    : 圆心的Y坐标
 * @param   r    : 圆的半径
 * @param   color: 圆的颜色
 * @retval  无
 */
void rgblcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    uint32_t i;
    uint32_t imax;
    uint32_t sqmax;
    uint32_t xr;

    imax = ((uint32_t)r * 707) / 1000 + 1;
    sqmax = (uint32_t)r * (uint32_t)r + (uint32_t)r / 2;
    xr = r;

    rgblcd_draw_hline(x - r, y, 2 * r, color);
    for (i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            if (xr > imax)
            {
                rgblcd_draw_hline (x - i + 1, y + xr, 2 * (i - 1), color);
                rgblcd_draw_hline (x - i + 1, y - xr, 2 * (i - 1), color);
            }
            xr--;
        }
        rgblcd_draw_hline(x - xr, y + i, 2 * xr, color);
        rgblcd_draw_hline(x - xr, y - i, 2 * xr, color);
    }
}

/**
 * @brief   RGB LCD在指定位置显示一个字符
 * @param   x    : 指定位置的X坐标
 * @param   y    : 指定位置的Y坐标
 * @param   chr  : 要显示的字符，范围：' '~'~'
 * @param   size : 字体
 * @arg     12: 12*12 ASCII字符
 * @arg     16: 16*16 ASCII字符
 * @arg     24: 24*24 ASCII字符
 * @arg     32: 32*32 ASCII字符
 * @param   mode : 显示模式
 * @arg     0: 非叠加方式
 * @arg     1: 叠加方式
 * @param   color: 字符的颜色
 * @retval  无
 */
void rgblcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t csize;
    uint8_t *pfont;
    uint16_t y0;
    uint8_t t;
    uint8_t t1;
    uint8_t temp;

    csize = ((size >> 3) + (((size & 0x7) != 0) ? 1 : 0)) * (size >> 1);    /* 计算所选字体对应一个字符所占的字节数 */
    chr -= ' ';                                                             /* 计算偏移后的值，因为字库是从空格开始的 */

    switch (size)
    {
        case 12:                                                            /* 12*12 ASCII字符 */
        {
            pfont = (uint8_t *)asc2_1206[(uint8_t)chr];
            break;
        }
        case 16:                                                            /* 16*16 ASCII字符 */
        {
            pfont = (uint8_t *)asc2_1608[(uint8_t)chr];
            break;
        }
        case 24:                                                            /* 24*24 ASCII字符 */
        {
            pfont = (uint8_t *)asc2_2412[(uint8_t)chr];
            break;
        }
        case 32:                                                            /* 32*32 ASCII字符 */
        {
            pfont = (uint8_t *)asc2_3216[(uint8_t)chr];
            break;
        }
        default:
        {
            return;
        }
    }

    y0 = y;
    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];                                                    /* 获取字符的点阵数据 */
        for (t1 = 0; t1 < 8; t1++)                                          /* 遍历一个字节的8个位 */
        {
            if ((temp & 0x80) != 0)                                         /* 需要显示的有效点 */
            {
                rgblcd_draw_point(x, y, color);                             /* 以字符颜色绘制这个点 */
            }
            else if (mode == 0)                                             /* 不需要显示的无效点 */
            {
                rgblcd_draw_point(x, y, g_back_color);                      /* 绘制背景色 */
            }

            temp <<= 1;                                                     /* 移位至下一个位 */
            y++;
            if (y >= rgblcddev.height)                                      /* 判断Y坐标是否超出显示区域 */
            {
                return;
            }
            if ((y - y0) == size)                                           /* 本行绘制完成 */
            {
                y = y0;                                                     /* Y坐标复位 */
                x++;                                                        /* 下一行 */
                if (x >= rgblcddev.width)                                   /* 判断X坐标是否超出显示区域 */
                {
                    return;
                }
                break;
            }
        }
    }
}

/**
 * @brief   RGB LCD显示len个数字
 * @param   x    : 起始X坐标
 * @param   y    : 起始Y坐标
 * @param   num  : 数值，范围：0~2^32
 * @param   len  : 显示数字的位数
 * @param   size : 字体
 * @arg     12: 12*12 ASCII字符
 * @arg     16: 16*16 ASCII字符
 * @arg     24: 24*24 ASCII字符
 * @arg     32: 32*32 ASCII字符
 * @param   color: 数字的颜色
 * @retval  无
 */
void rgblcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t;
    uint8_t temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                   /* 按总显示位数循环 */
    {
        temp = (num / rgblcd_pow(10, len - t - 1)) % 10;                        /* 获取对应位的数字 */
        if((enshow == 0) && (t < (len - 1)))                                    /* 没有使能显示，且还有位要显示 */
        {
            if (temp == 0)
            {
                rgblcd_show_char(x + (size >> 1) * t, y, ' ', size, 0, color);  /* 显示空格，占位 */
                continue;                                                       /* 继续下一个位 */
            }
            else
            {
                enshow = 1;                                                     /* 使能显示 */
            }
        }

        rgblcd_show_char(x + (size >> 1) * t, y, temp + '0', size, 0, color);   /* 显示字符 */
    }
}

/**
 * @brief   RGB LCD扩展显示len个数字（显示高位0）
 * @param   x    : 起始X坐标
 * @param   y    : 起始Y坐标
 * @param   num  : 数值，范围：0~2^32
 * @param   len  : 显示数字的位数
 * @param   size : 字体
 * @arg     12: 12*12 ASCII字符
 * @arg     16: 16*16 ASCII字符
 * @arg     24: 24*24 ASCII字符
 * @arg     32: 32*32 ASCII字符
 * @param   mode : 显示模式
 * @arg     0: 非叠加方式
 * @arg     1: 叠加方式
 * @param   color: 数字的颜色
 * @retval  无
 */
void rgblcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t;
    uint8_t temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                                   /* 按总显示位数循环 */
    {
        temp = (num / rgblcd_pow(10, len - t - 1)) % 10;                                        /* 获取对应位的数字 */
        if((enshow == 0) && (t < (len - 1)))                                                    /* 没有使能显示，且还有位要显示 */
        {
            if (temp == 0)
            {
                if ((mode & 0x80) != 0)                                                         /* 高位需要填充0 */
                {
                    rgblcd_show_char(x + (size >> 1) * t, y, '0', size, mode & 0x01, color);    /* 显示0，占位 */
                }
                else
                {
                    rgblcd_show_char(x + (size >> 1) * t, y, ' ', size, mode & 0x01, color);    /* 显示空格，占位 */
                }
                continue;                                                                       /* 继续下一个位 */
            }
            else
            {
                enshow = 1;                                                                     /* 使能显示 */
            }
        }

        rgblcd_show_char(x + (size >> 1) * t, y, temp + '0', size, mode & 0x01, color);        /* 显示字符 */
    }
}

/**
 * @brief   RGB LCD显示字符串
 * @param   x     : 起始X坐标
 * @param   y     : 起始Y坐标
 * @param   width : 显示区域宽度
 * @param   height: 显示区域高度
 * @param   size  : 字体
 * @arg     12: 12*12 ASCII字符
 * @arg     16: 16*16 ASCII字符
 * @arg     24: 24*24 ASCII字符
 * @arg     32: 32*32 ASCII字符
 * @param   *p    : 字符串指针
 * @param   color : 字符串的颜色
 * @retval  无
 */
void rgblcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0;

    x0 = x;
    width += x;
    height += y;
    while ((*p <= '~') && (*p >= ' '))              /* 判断是否为非法字符 */
    {
        if (x >= width)                             /* 宽度越界 */
        {
            x = x0;                                 /* 换行 */
            y += size;
        }

        if (y >= height)                            /* 高度越界 */
        {
            break;                                  /* 退出 */
        }

        rgblcd_show_char(x, y, *p, size, 0, color); /* 显示一个字符 */
        x += (size >> 1);                           /* ASCII字符宽度为高度的一半 */
        p++;
    }
}

/**
 * @brief   读取RGB LCD ID
 * @param   无
 * @retval  RGB LCD ID
 */
static uint16_t rgblcd_panelid_read(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    uint16_t pids[] = {
        0x4342, /* ATK-MD0430R-480272 */
        0x7084, /* ATK-MD0700R-800480 */
        0x7016, /* ATK-MD0700R-1024600 */
        0x7018, /* ATK-MD0700R-1280800 */
        0x4384, /* ATK-MD0430R-800480 */
        0x1018, /* ATK-MD1018R-1280800 */
        0xFFFF, /* Unknow */
    };
    uint8_t id;

    gpio_init_struct.Pin = RGBLCD_B7_GPIO_PIN;              /* LTDC_B7 */
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RGBLCD_B7_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = RGBLCD_G7_GPIO_PIN;              /* LTDC_G7 */
    HAL_GPIO_Init(RGBLCD_G7_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = RGBLCD_R7_GPIO_PIN;              /* LTDC_R7 */
    HAL_GPIO_Init(RGBLCD_R7_GPIO_PORT, &gpio_init_struct);

    HAL_Delay(100);

    id = ((HAL_GPIO_ReadPin(RGBLCD_R7_GPIO_PORT, RGBLCD_R7_GPIO_PIN) == GPIO_PIN_RESET) ? 0 : 1);
    id |= ((HAL_GPIO_ReadPin(RGBLCD_G7_GPIO_PORT, RGBLCD_G7_GPIO_PIN) == GPIO_PIN_RESET) ? 0 : 1) << 1;
    id |= ((HAL_GPIO_ReadPin(RGBLCD_B7_GPIO_PORT, RGBLCD_B7_GPIO_PIN) == GPIO_PIN_RESET) ? 0 : 1) << 2;

    if (id >= (sizeof(pids) / sizeof(pids[0]))) {
        id = sizeof(pids) / sizeof(pids[0]) - 1;
    }

    gpio_init_struct.Pin = RGBLCD_B7_GPIO_PIN;              /* LTDC_B7 */
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_struct.Alternate = RGBLCD_B7_GPIO_AF;
    HAL_GPIO_Init(RGBLCD_B7_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = RGBLCD_G7_GPIO_PIN;              /* LTDC_G7 */
    gpio_init_struct.Alternate = RGBLCD_G7_GPIO_AF;
    HAL_GPIO_Init(RGBLCD_G7_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = RGBLCD_R7_GPIO_PIN;              /* LTDC_R7 */
    gpio_init_struct.Alternate = RGBLCD_R7_GPIO_AF;
    HAL_GPIO_Init(RGBLCD_R7_GPIO_PORT, &gpio_init_struct);

    return pids[id];
}

/**
 * @brief   LTDC时钟设置
 * @param   clock: LTDC时钟目标时钟
 * @retval  设置结果
 * @arg     0: 设置成功
 * @arg     1: 设置失败
 */
static uint8_t rgblcd_ltdc_clk_set(uint32_t clock)
{
    RCC_PeriphCLKInitTypeDef rcc_periph_clk_init_struct = {0};

    rcc_periph_clk_init_struct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    rcc_periph_clk_init_struct.LtdcClockSelection = RCC_LTDCCLKSOURCE_IC16;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC16].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    rcc_periph_clk_init_struct.ICSelection[RCC_IC16].ClockDivider = HAL_RCCEx_GetPLL1CLKFreq() / clock;
    if (HAL_RCCEx_PeriphCLKConfig(&rcc_periph_clk_init_struct) != HAL_OK)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief   RGB LCD平方函数
 * @param   m: 底数
 * @param   n: 指数
 * @retval  m^n
 */
static uint32_t rgblcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}
