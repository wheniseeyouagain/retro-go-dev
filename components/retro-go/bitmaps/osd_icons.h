#ifndef OSD_ICONS_H
#define OSD_ICONS_H
#include <stdint.h>

// 电池图标
#define BATTERY_BULGE_Y       7       // 凸起的Y坐标
#define BATTERY_ICON_HEIGHT   34      // 总高度
#define BATTERY_BODY_WIDTH    18      // 主体宽度
#define BATTERY_BODY_X        5  //(BATTERY_BULGE_X + (BATTERY_BULGE_WIDTH - BATTERY_BODY_WIDTH) / 2)  // 11 + (4-18)/2 = 4

// 电池图标位图（18x34）
static const uint32_t battery_icon_low[34] = {
    // === 凸起部分（2行）===
    0x00000780,  // 行0: 凸起（col7~10）- 与主体X=5对齐
    0x00000780,
    
    // === 间距部分（2行）===
    0x00000000,  // 行2: 全0
    0x00000000,  // 行3: 全0
    
    // === 主体部分（30行）===
    // 顶部边框（2行）- 仅18列（0~17）全亮，屏蔽bit18+
    0x0003FFFF & 0x0003FFFF,  // 行4: 低18位全1（0~17）
    0x0003FFFF & 0x0003FFFF,  // 行5: 低18位全1
    
    // 中间边框（24行）- 左2像素（bit0~1） + 右2像素（bit15~16），中间空
    // 0x00030003 = 二进制 00000000 00000011 00000000 00000011
    // 严格仅：col0~1（左2）、col15~16（右2），col17=0（无多余）
    0x00030003,  // 行6
    0x00030003,  // 行7
    0x00030003,  // 行8
    0x00030003,  // 行9
    0x00030003,  // 行10
    0x00030003,  // 行11
    0x00030003,  // 行12
    0x00030003,  // 行13
    0x00030003,  // 行14
    0x00030003,  // 行15
    0x00030003,  // 行16
    0x00030003,  // 行17
    0x00030003,  // 行18
    0x00030003,  // 行19
    0x00030003,  // 行20
    0x00030003,  // 行21
    0x00030003,  // 行22
    0x00030003,  // 行23
    0x00030003,  // 行24
    0x00030003,  // 行25
    0x00030003,  // 行26
    0x00030003,  // 行27
    0x00030003,  // 行28
    0x00030003,  // 行29
    
    // 底部边框（2行）- 仅18列全亮
    0x0003FFFF & 0x0003FFFF,  // 行30: 低18位全1
    0x0003FFFF & 0x0003FFFF,  // 行31: 低18位全1
    
    // 补齐到34行
    0x00000000,  // 行32
    0x00000000,  // 行33
};

// 定义音量条的位置和尺寸（基于屏幕旋转前的坐标系）
// 屏幕默认竖屏480x640,顺时针旋转90度得到横屏
#define VOLUME_BAR_TOP          510     // 音量条Y坐标
#define VOLUME_BAR_BOTTOM       589     // 音量条Y坐标 + 音量条高 - 1 = 510 + 80 - 1
#define VOLUME_BAR_LEFT         8       // 音量条X坐标
#define VOLUME_BAR_RIGHT        19      // 音量条X坐标 + 音量条宽度（包含边框） - 1 = 8 + 12 - 1

// 内边距相关
#define TOTAL_PADDING_VALUE     3       // 边框宽度 + 边框和填充之间的间距 = 1 + 2
#define VOLUME_INNER_HEIGHT     74      // 音量条高 - TOTAL_PADDING_VALUE * 2 = 80 - 3*2

// 内部填充区域边界
#define VOLUME_BAR_INNER_LEFT   11      // 音量条X坐标 + TOTAL_PADDING_VALUE = 8 + 3
#define VOLUME_BAR_INNER_RIGHT  16      // VOLUME_BAR_RIGHT - TOTAL_PADDING_VALUE = 19 - 3

// 填充结束位置（固定值）
#define VOLUME_BAR_FILL_END_Y   587     // VOLUME_BAR_BOTTOM - TOTAL_PADDING_VALUE + 1 = 589 - 3 + 1

// ========== 喇叭相关宏定义 ==========
#define SPEAKER_ICON_WIDTH      14      // 喇叭最宽处（梯形宽边）
#define SPEAKER_ICON_HEIGHT     9       // 喇叭总高度（梯形4 + 矩形5）
#define ICON_SPACING            4       // 喇叭和音量条的间距

// 喇叭位置
#define SPEAKER_ICON_X          8       // 修正后的水平位置：VOLUME_BAR_X + (VOLUME_BAR_WIDTH - SPEAKER_ICON_WIDTH) / 2 + 1
#define SPEAKER_ICON_Y          594     // VOLUME_BAR_Y + VOLUME_BAR_HEIGHT + ICON_SPACING = 510 + 80 + 4

// 喇叭图标位图
static const uint16_t SPEAKER_ICON_BITMAP[9] = {
    0x3FFC,  // 14像素
    0x3FFC,  // 14像素
    0x1FF8,  // 12像素
    0x0FF0,  // 8像素
    0x0FF0,  // 8像素
    0x0FF0,  // 8像素
    0x0FF0,  // 8像素
    0x0FF0,  // 8像素
    0x0FF0,  // 8像素
};

// 音量
typedef struct {
    int volume;             // 当前要显示的音量值 (0-100)
    uint64_t show_until;    // 显示截止时间（微秒）
    bool visible;           // 是否可见
} volume_osd_t;

#endif // OSD_ICONS_H