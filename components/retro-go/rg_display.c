#include "rg_system.h"
#include "rg_display.h"
#include "bitmaps/osd_icons.h"

#include <stdlib.h>
#include <string.h>

#define LCD_BUFFER_LENGTH (RG_SCREEN_WIDTH * 4) // In pixels
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

// static rg_display_driver_t driver;
static rg_task_t *display_task_queue;
static rg_display_counters_t counters;
static rg_display_config_t config;
// static rg_surface_t *osd;
static rg_surface_t *border;
static rg_display_t display;
static int16_t map_viewport_to_source_x[RG_SCREEN_WIDTH + 1];
static int16_t map_viewport_to_source_y[RG_SCREEN_HEIGHT + 1];
static uint32_t screen_line_checksum[RG_SCREEN_HEIGHT + 1];

#define LINE_IS_REPEATED(Y) (map_viewport_to_source_y[(Y)] == map_viewport_to_source_y[(Y) - 1])
// This is to avoid flooring a number that is approximated to .9999999 and be explicit about it
#define FLOAT_TO_INT(x) ((int)((x) + 0.1f))

static const char *SETTING_BACKLIGHT = "DispBacklight";
static const char *SETTING_SCALING = "DispScaling";
static const char *SETTING_FILTER = "DispFilter";
static const char *SETTING_ROTATION = "DispRotation";
static const char *SETTING_BORDER = "DispBorder";
static const char *SETTING_CUSTOM_ZOOM = "DispCustomZoom";

static void lcd_init(void);
static void lcd_deinit(void);
static void lcd_sync(void);
static void lcd_set_rotation(int rotation);
static void lcd_set_backlight(float percent);
static void lcd_set_window(int left, int top, int width, int height);
static inline uint16_t *lcd_get_buffer(size_t length);
static inline void lcd_send_buffer(uint16_t *buffer, size_t length);

#if RG_SCREEN_DRIVER == 0 || RG_SCREEN_DRIVER == 1 /* ILI9341/ST7789 */
#include "drivers/display/ili9341.h"
#elif RG_SCREEN_DRIVER == 2 /* ST7701 MIPI DSI */
#include "drivers/display/st7701.h"
#elif RG_SCREEN_DRIVER == 99
#include "drivers/display/sdl2.h"
#else
#include "drivers/display/dummy.h"
#endif

static volume_osd_t volume_osd = {0};
// 音量OSD显示函数
void rg_volume_osd(int volume)
{
    volume_osd.volume = volume;
    volume_osd.show_until = rg_system_timer() + 2000000; // 显示2秒
    volume_osd.visible = true;
    
    // 关键：失效相关区域的checksum，强制重绘这些行
    for (int y = VOLUME_BAR_TOP; y < VOLUME_BAR_BOTTOM && y < RG_SCREEN_HEIGHT; ++y) {
        if (y >= 0) {
            screen_line_checksum[y] = 0; // 使checksum失效，强制重绘
        }
    }
    
    // 可选：触发重绘事件，让系统尽快更新
    rg_system_event(RG_EVENT_REDRAW, NULL);
}

static int draw_on_screen_display(int region_start, int region_end)
{
    static unsigned int area_dirty = 0;
    rg_margins_t margins = rg_gui_get_safe_area();
    int left = display.screen.width - margins.right - 28;
    int top = margins.top + 4;
    int border = 3;
    int width = 20;
    int height = 14;

    if (region_end < top + height)
        return top + height;

    // Low battery indicator
    if (rg_system_get_indicator(RG_INDICATOR_POWER_LOW) && ((counters.totalFrames / 20) & 1))
    {
        rg_display_clear_rect(left, top, width, height, C_RED); // Main body
        rg_display_clear_rect(left + width, top + height / 4, border, height / 2, C_RED); // The tab
        rg_display_clear_rect(left + border, top + border, width - border * 2, height - border * 2, C_BLACK); // The fill
        // memset(&screen_line_checksum[top], 0, sizeof(uint32_t) * height);
        area_dirty |= (1 << RG_INDICATOR_POWER_LOW);
    }
    else if (area_dirty)
    {
        if (display.viewport.width < display.screen.width || display.viewport.height < display.screen.height)
            rg_display_clear_rect(left, top, width + border, height, C_BLACK);
        memset(&screen_line_checksum[top], 0, sizeof(uint32_t) * height);
        area_dirty = 0;
    }
    return 0;
}

static inline unsigned blend_pixels(unsigned a, unsigned b)
{
    // Fast path (taken 80-90% of the time)
    if (a == b)
        return a;

    // Not the original author, but a good explanation is found at:
    // https://medium.com/@luc.trudeau/fast-averaging-of-high-color-16-bit-pixels-cb4ac7fd1488
    a = (a << 8) | (a >> 8);
    b = (b << 8) | (b >> 8);
    unsigned s = a ^ b;
    unsigned v = ((s & 0xF7DEU) >> 1) + (a & b) + (s & 0x0821U);
    return (v << 8) | (v >> 8);

    // This is my attempt at averaging two 565BE values without swapping bytes (3x the speed of the code above)
    // return (((a ^ b) & 0b1101111011110110U) >> 1) + (a & b);
}

static inline void write_update(const rg_surface_t *update)
{
    const int64_t time_start = rg_system_timer();

    bool filter_x = display.viewport.filter_x;
    bool filter_y = display.viewport.filter_y;
    int draw_left = display.viewport.left;
    int draw_top = display.viewport.top;
    int draw_width = display.viewport.width;
    int draw_height = display.viewport.height;

    int crop_left = 0;
    int crop_top = 0;

    if (draw_left < 0)
    {
        crop_left += -draw_left * display.viewport.step_x;
        draw_width += draw_left * 2;
        draw_left = 0;
    }

    if (draw_top < 0)
    {
        crop_top += -draw_top * display.viewport.step_y;
        draw_height += draw_top * 2;
        draw_top = 0;
    }

    const int format = update->format;
    const int stride = update->stride;
    const void *data = update->data + update->offset + (crop_top * stride) + (crop_left * RG_PIXEL_GET_SIZE(format));
    const uint16_t *palette = update->palette;

    const int screen_left = display.screen.margins.left + draw_left;
    const int screen_top = display.screen.margins.top + draw_top;
    const bool partial_update = RG_SCREEN_PARTIAL_UPDATES;
    // const bool interlace = false;

    int lines_per_buffer = LCD_BUFFER_LENGTH / draw_width;
    int lines_remaining = draw_height;
    int lines_updated = 0;
    int window_top = -1;
    int osd_next_call = 20;

    for (int y = 0; y < draw_height;)
    {
        int lines_to_copy = RG_MIN(lines_per_buffer, lines_remaining);

        if (lines_to_copy < 1)
            break;

        // The vertical filter requires a block to start and end with unscaled lines
        if (filter_y)
        {
            while (lines_to_copy > 1 && (LINE_IS_REPEATED(y + lines_to_copy - 1) ||
                                         LINE_IS_REPEATED(y + lines_to_copy)))
                --lines_to_copy;
        }

        uint16_t *line_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        uint16_t *line_buffer_ptr = line_buffer;

        uint32_t checksum = 0xFFFFFFFF;
        bool need_update = !partial_update;

        for (int i = 0; i < lines_to_copy; ++i)
        {
            if (i > 0 && LINE_IS_REPEATED(y))
            {
                memcpy(line_buffer_ptr, line_buffer_ptr - draw_width, draw_width * 2);
                line_buffer_ptr += draw_width;
            }
            else
            {
                #define RENDER_LINE(PTR_TYPE, PIXEL) { \
                    PTR_TYPE *buffer = (PTR_TYPE *)(data + map_viewport_to_source_y[y] * stride);\
                    for (int xx = 0; xx < draw_width; ++xx) { \
                        int x = map_viewport_to_source_x[xx]; \
                        *line_buffer_ptr++ = (PIXEL); \
                    } \
                }
                if (format & RG_PIXEL_PALETTE)
                    RENDER_LINE(uint8_t, palette[buffer[x]])
                else if (format == RG_PIXEL_565_LE)
                    RENDER_LINE(uint16_t, (buffer[x] << 8) | (buffer[x] >> 8))
                else
                    RENDER_LINE(uint16_t, buffer[x])

                if (partial_update)
                    checksum = rg_hash((void*)(line_buffer_ptr - draw_width), draw_width * 2);
            }

            if (screen_line_checksum[draw_top + y] != checksum)
            {
                screen_line_checksum[draw_top + y] = checksum;
                need_update = true;
            }

            ++y;
        }

        if (filter_x && need_update)
        {
            for (int i = 0; i < lines_to_copy; ++i)
            {
                uint16_t *buffer = line_buffer + i * draw_width;
                for (int x = 1; x < draw_width - 1; ++x)
                {
                    if (map_viewport_to_source_x[x] == map_viewport_to_source_x[x - 1])
                    {
                        buffer[x] = blend_pixels(buffer[x - 1], buffer[x + 1]);
                    }
                }
            }
        }

        if (filter_y && need_update)
        {
            int top = y - lines_to_copy;
            for (int i = 1; i < lines_to_copy - 1; ++i)
            {
                if (LINE_IS_REPEATED(top + i))
                {
                    uint16_t *lineA = line_buffer + (i - 1) * draw_width;
                    uint16_t *lineB = line_buffer + (i + 0) * draw_width;
                    uint16_t *lineC = line_buffer + (i + 1) * draw_width;
                    for (size_t x = 0; x < draw_width; ++x)
                    {
                        lineB[x] = blend_pixels(lineA[x], lineC[x]);
                    }
                }
            }
        }

        if (need_update)
        {
            int top = screen_top + y - lines_to_copy;
            if (top != window_top)
                lcd_set_window(screen_left, top, draw_width, lines_remaining);
            lcd_send_buffer(line_buffer, draw_width * lines_to_copy);
            window_top = top + lines_to_copy;
            lines_updated += lines_to_copy;
        }
        else
        {
            // Return unused buffer
            lcd_send_buffer(line_buffer, 0);
        }

        // Drawing the OSD as we progress reduces flicker compared to doing it once at the end
        if (osd_next_call && draw_top + y >= osd_next_call)
        {
            osd_next_call = draw_on_screen_display(0, draw_top + y);
            window_top = -1;
        }

        lines_remaining -= lines_to_copy;
    }

    if (lines_updated > draw_height * 0.80f)
        counters.fullFrames++;
    else
        counters.partFrames++;
    counters.busyTime += rg_system_timer() - time_start;
}

static inline void write_update_ppa(const rg_surface_t *update)
{
    const int64_t time_start = rg_system_timer();

    // 获取显示配置信息
    int src_width = display.source.width;
    int src_height = display.source.height;
    int screen_width = display.screen.width;
    int screen_height = display.screen.height;
    int viewport_width = display.viewport.width;
    int viewport_height = display.viewport.height;
    int viewport_left = display.viewport.left;
    int viewport_top = display.viewport.top;
    // int draw_left = display.viewport.left;
    // int draw_top = display.viewport.top;
    // int draw_width = display.viewport.width;
    int draw_height = display.viewport.height;
    // bool filter_x = display.viewport.filter_x;
    bool filter_y = display.viewport.filter_y;

    // 准备源数据缓冲区（转换格式）
    const int format = update->format;
    const int stride = update->stride;
    const void *data = update->data + update->offset;
    const uint16_t *palette = update->palette;

    size_t in_size = ALIGN_UP(src_width * src_height, 32) * 2;
    uint16_t *transfer_buf;
    int pixel_count = src_width * src_height;

    // 格式转换：转换为 RGB565 LE 格式
    if(format & RG_PIXEL_PALETTE){
        transfer_buf = heap_caps_aligned_calloc(32, in_size, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        for(int i = 0; i < src_height; i++){
            for(int j = 0; j < src_width; j++){
                uint16_t pixel = palette[((uint8_t*)data)[i * stride + j]];
                if(format & RG_PIXEL_565_BE){
                    transfer_buf[i * src_width + j] = (pixel << 8) | (pixel >> 8);
                }else{
                    transfer_buf[i * src_width + j] = pixel;
                }
            }
        }
    }else if(format == RG_PIXEL_565_BE){
        transfer_buf = heap_caps_aligned_calloc(32, in_size, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        for (int i = 0; i < pixel_count; i++) {
            transfer_buf[i] = (((uint16_t*)data)[i] << 8) | (((uint16_t*)data)[i] >> 8);
        }
    }else{
        transfer_buf = (uint16_t*) data;
    }

    // 分配输出缓冲区（最大可能尺寸）
    size_t out_size = screen_width * screen_height * 2;
    //uint16_t *out_buf = heap_caps_aligned_calloc(32, out_size, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

    uint16_t *out_buf = lcd_get_buffer_ppa();
    //RG_LOGI("PPA OUT BUF: %p", out_buf);

    int out_width = 0;
    int out_height = 0;
    int display_left = 0;
    int display_top = 0;
    float scale_x = (float)(viewport_width) / src_width;
    float scale_y = (float)(viewport_height) / src_height;
    float scale = 1.0f;
    // 根据缩放模式调用相应的 PPA 函数
    switch (config.scaling) {
        case RG_DISPLAY_SCALING_OFF:
            out_width =  RG_SCREEN_HEIGHT;
            out_height =  RG_SCREEN_WIDTH;
            display_left = 0;
            display_top = 0;

            s_srm_ops_fit(transfer_buf, out_buf, out_size,
                          src_width, src_height,
                          1.0f,
                          viewport_top, viewport_left);

            break;

        case RG_DISPLAY_SCALING_FIT:
            // 选择较小的缩放比例
            if (scale_x > scale_y) {
                scale = scale_y;
            } else {
                scale = scale_x;
            }
            out_width =  RG_SCREEN_HEIGHT;
            out_height =  RG_SCREEN_WIDTH;
            display_left = 0;
            display_top = 0;
            s_srm_ops_fit(transfer_buf, out_buf, out_size,
                          src_width, src_height,
                          scale,
                          viewport_top, viewport_left);
            break;

        case RG_DISPLAY_SCALING_FULL:

            display_left = viewport_top;
            display_top = viewport_left;
            out_width =  viewport_height;
            out_height =  viewport_width;
            // 填满整个屏幕
            s_srm_ops_full(transfer_buf, out_buf, out_size,
                           src_width, src_height,
                           scale_x,scale_y,
                           out_width, out_height);
            break;

        case RG_DISPLAY_SCALING_ZOOM:
        default:
            // 选择较小的缩放比例
            if (scale_x > scale_y) {
                scale = scale_y;
            } else {
                scale = scale_x;
            }
            out_width =  RG_SCREEN_HEIGHT;
            out_height =  RG_SCREEN_WIDTH;
            // viewport 已经计算了居中位置，但需要转换坐标（旋转90度）
            display_left = 0;
            display_top = 0;
            //RG_LOGI("PPA FIT: src=%dx%d, out=%dx%d, pos=(%d,%d)\n", src_width, src_height, out_width, out_height, viewport_top, viewport_left);
            // 保持宽高比缩放,旋转90度后输出宽度 = viewport_height，输出高度 = viewport_width
            s_srm_ops_fit(transfer_buf, out_buf, out_size,
                          src_width, src_height,
                          scale,
                          viewport_top, viewport_left);


            break;
    }

    // ===================== 绘制音量OSD =====================
    if (volume_osd.visible && time_start <= volume_osd.show_until)
    {
        int volume = volume_osd.volume;
        int fill_pixels = (VOLUME_INNER_HEIGHT * volume) / 100;
        int fill_start = VOLUME_BAR_BOTTOM - TOTAL_PADDING_VALUE - fill_pixels + 1;
        // 音量条
        for (int y = VOLUME_BAR_TOP; y <= VOLUME_BAR_BOTTOM; y++) {
            for (int x = VOLUME_BAR_LEFT; x <= VOLUME_BAR_RIGHT; x++) {
                bool border = (y == VOLUME_BAR_TOP || y == VOLUME_BAR_BOTTOM || 
                            x == VOLUME_BAR_LEFT || x == VOLUME_BAR_RIGHT);
                bool fill = (!border && y >= fill_start && y < VOLUME_BAR_FILL_END_Y && 
                            x >= VOLUME_BAR_INNER_LEFT && x <= VOLUME_BAR_INNER_RIGHT);
                
                if (border || fill) {
                    out_buf[y * out_width + x] = C_WHITE;
                }
            }
        }
        // 喇叭
        for (int r = 0; r < SPEAKER_ICON_HEIGHT; r++) {
            uint16_t bits = SPEAKER_ICON_BITMAP[r];
            int y = SPEAKER_ICON_Y + r;
            
            for (int c = 0; c < SPEAKER_ICON_WIDTH; c++) {
                if ((bits >> (13 - c)) & 1) {
                    out_buf[y * out_width + (SPEAKER_ICON_X + c)] = C_WHITE;
                }
            }
        }
    }
    else if (volume_osd.visible)
    {
        volume_osd.visible = false;
    }

    // ===================== 绘制低电量OSD =====================
    rg_app_t *app = rg_system_get_app();
    bool is_in_game = (app->configNs && strcmp(app->configNs, "launcher") != 0);
    if (is_in_game && rg_system_get_indicator(RG_INDICATOR_POWER_LOW) && ((counters.totalFrames / 20) & 1))
    {
        for (int row = 0; row < BATTERY_ICON_HEIGHT; row++) {
        int screen_y = BATTERY_BULGE_Y + row;
        uint16_t* row_ptr = &out_buf[screen_y * out_width];
        uint32_t bits = battery_icon_low[row];

        for (int col = 0; col < BATTERY_BODY_WIDTH; col++) {
            uint32_t bit_mask = 1UL << col;
            if ((bits & bit_mask) != 0) {
                row_ptr[BATTERY_BODY_X + col] = C_RED;
            }
        }
    }
    }
    // ===================== OSD绘制结束 =====================

    // 发送到显示屏
    lcd_send_buffer_ppa(out_buf, out_size, display_left, display_top,
                        display_left + out_width, display_top + out_height);

    // 释放缓冲区
    //free(out_buf);
    if(format & RG_PIXEL_PALETTE)
        free(transfer_buf);
    else if(format == RG_PIXEL_565_BE)
        free(transfer_buf);


    //下面这部分删除就报错，但没啥用，换成延时也不行，不懂为什么
    int lines_per_buffer = 480;//LCD_BUFFER_LENGTH / draw_width;
    int lines_remaining = draw_height;
    for (int y = 0; y < draw_height;)
    {
        int lines_to_copy = RG_MIN(lines_per_buffer, lines_remaining);
        if (lines_to_copy < 1){
            break;
        }
        //这个分支从不进入，但是删除也不行，不懂为什么
        if (filter_y)
        {
            while (lines_to_copy > 1 && (LINE_IS_REPEATED(y + lines_to_copy - 1) ||
                                         LINE_IS_REPEATED(y + lines_to_copy))){
                --lines_to_copy;
            }
        }
        lines_remaining -= lines_to_copy;
        y += lines_to_copy;

    }



    counters.fullFrames++;
    counters.busyTime += rg_system_timer() - time_start;
}

static void update_viewport_scaling(void)
{
    int screen_width = display.screen.width;
    int screen_height = display.screen.height;
    int src_width = display.source.width;
    int src_height = display.source.height;
    int new_width = src_width;
    int new_height = src_height;

    if (config.scaling == RG_DISPLAY_SCALING_FULL)
    {
        new_width = screen_width;
        new_height = screen_height;
    }
    else if (config.scaling == RG_DISPLAY_SCALING_FIT)
    {
        new_width = FLOAT_TO_INT(screen_height * ((float)src_width / src_height));
        new_height = screen_height;
        if (new_width > screen_width) {
            new_width = screen_width;
            new_height = FLOAT_TO_INT(screen_width * ((float)src_height / src_width));
        }
    }
    else if (config.scaling == RG_DISPLAY_SCALING_ZOOM)
    {
        new_width = FLOAT_TO_INT(src_width * config.custom_zoom);
        new_height = FLOAT_TO_INT(src_height * config.custom_zoom);
    }

    // Everything works better when we use even dimensions!
    new_width &= ~1;
    new_height &= ~1;

    display.viewport.left = (screen_width - new_width) / 2;
    display.viewport.top = (screen_height - new_height) / 2;
    display.viewport.width = new_width;
    display.viewport.height = new_height;

    display.viewport.step_x = (float)src_width / display.viewport.width;
    display.viewport.step_y = (float)src_height / display.viewport.height;

    display.viewport.filter_x = (config.filter == RG_DISPLAY_FILTER_HORIZ || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.width % src_width) != 0);
    display.viewport.filter_y = (config.filter == RG_DISPLAY_FILTER_VERT || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.height % src_height) != 0);

    memset(screen_line_checksum, 0, sizeof(screen_line_checksum));

    for (int x = 0; x < screen_width; ++x)
        map_viewport_to_source_x[x] = FLOAT_TO_INT(x * display.viewport.step_x);
    for (int y = 0; y < screen_height; ++y)
        map_viewport_to_source_y[y] = FLOAT_TO_INT(y * display.viewport.step_y);
}

static bool load_border_file(const char *filename)
{
    RG_LOGI("Loading border file: %s", filename ?: "(none)");

    free(border), border = NULL;
    display.changed = true;

    if (filename && (border = rg_surface_load_image_file(filename, 0)))
    {
        if (border->width != rg_display_get_width() || border->height != rg_display_get_height())
        {
            rg_surface_t *resized = rg_surface_resize(border, rg_display_get_width(), rg_display_get_height());
            if (resized)
            {
                rg_surface_free(border);
                border = resized;
            }
        }
        return true;
    }
    return false;
}

IRAM_ATTR
static void display_task(void *arg)
{
    rg_task_msg_t msg;

    while (rg_task_peek(&msg, -1))
    {
        // Received a shutdown request!
        if (msg.type == RG_TASK_MSG_STOP)
            break;

        if (display.changed)
        {
            update_viewport_scaling();
            #if  RG_SCREEN_DRIVER  == 2
                        rg_display_clear(C_BLACK);
            #endif
            // Clear the screen if the viewport doesn't cover the entire screen because garbage could remain on the sides
            if (display.viewport.width < display.screen.width || display.viewport.height < display.screen.height)
            {
                if (border)
                    rg_display_write_rect(0, 0, border->width, border->height, 0, border->data, RG_DISPLAY_WRITE_NOSYNC);
                else
                    rg_display_clear_except(display.viewport.left, display.viewport.top, display.viewport.width, display.viewport.height, C_BLACK);
            }

            display.changed = false;
        }

        #if  RG_SCREEN_DRIVER  == 2
                write_update_ppa(msg.dataPtr);
        #else
                write_update(msg.dataPtr);
        #endif

        rg_task_receive(&msg, -1);

        lcd_sync();
    }
}

void rg_display_force_redraw(void)
{
    display.changed = true;
    // memset(screen_line_checksum, 0, sizeof(screen_line_checksum));
    rg_system_event(RG_EVENT_REDRAW, NULL);
    rg_display_sync(true);
}

const rg_display_t *rg_display_get_info(void)
{
    return &display;
}

rg_display_counters_t rg_display_get_counters(void)
{
    return counters;
}

int rg_display_get_width(void)
{
    // return display.screen.real_width - (display.screen.margins.left + display.screen.margins.right);
    return display.screen.width;
}

int rg_display_get_height(void)
{
    // return display.screen.real_height - (display.screen.margins.top + display.screen.margins.bottom);
    return display.screen.height;
}

void rg_display_set_scaling(display_scaling_t scaling)
{
    config.scaling = RG_MIN(RG_MAX(0, scaling), RG_DISPLAY_SCALING_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_SCALING, config.scaling);
    display.changed = true;
}

display_scaling_t rg_display_get_scaling(void)
{
    return config.scaling;
}

void rg_display_set_custom_zoom(double factor)
{
    config.custom_zoom = RG_MIN(RG_MAX(0.1, factor), 2.0);
    rg_settings_set_number(NS_APP, SETTING_CUSTOM_ZOOM, config.custom_zoom);
    display.changed = true;
}

double rg_display_get_custom_zoom(void)
{
    return config.custom_zoom;
}

void rg_display_set_filter(display_filter_t filter)
{
    config.filter = RG_MIN(RG_MAX(0, filter), RG_DISPLAY_FILTER_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_FILTER, config.filter);
    display.changed = true;
}

display_filter_t rg_display_get_filter(void)
{
    return config.filter;
}

void rg_display_set_rotation(display_rotation_t rotation)
{
    config.rotation = RG_MIN(RG_MAX(0, rotation), RG_DISPLAY_ROTATION_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_ROTATION, config.rotation);
    display.changed = true;
}

display_rotation_t rg_display_get_rotation(void)
{
    return config.rotation;
}

void rg_display_set_backlight(display_backlight_t percent)
{
    config.backlight = RG_MIN(RG_MAX(percent, RG_DISPLAY_BACKLIGHT_MIN), RG_DISPLAY_BACKLIGHT_MAX);
    rg_settings_set_number(NS_GLOBAL, SETTING_BACKLIGHT, config.backlight);
    lcd_set_backlight(config.backlight);
}

display_backlight_t rg_display_get_backlight(void)
{
    return config.backlight;
}

void rg_display_set_border(const char *filename)
{
    free(config.border_file);
    config.border_file = NULL;

    if (load_border_file(filename))
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, filename);
        config.border_file = strdup(filename);
    }
    else
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, NULL);
        config.border_file = NULL;
    }
    display.changed = true;
}

char *rg_display_get_border(void)
{
    return rg_settings_get_string(NS_APP, SETTING_BORDER, NULL);
}

void rg_display_submit(const rg_surface_t *update, uint32_t flags)
{
    const int64_t time_start = rg_system_timer();

    // Those things should probably be asserted, but this is a new system let's be forgiving...
    if (!update || !update->data)
        return;

    if (display.source.width != update->width || display.source.height != update->height)
    {
        rg_display_sync(true);
        display.source.width = update->width;
        display.source.height = update->height;
        display.changed = true;
    }

    rg_task_send(display_task_queue, &(rg_task_msg_t){.dataPtr = update}, -1);

    counters.blockTime += rg_system_timer() - time_start;
    counters.totalFrames++;
}

bool rg_display_is_busy(void)
{
    return rg_task_messages_waiting(display_task_queue) != 0;
}

bool rg_display_sync(bool block)
{
    while (block && rg_task_messages_waiting(display_task_queue))
        continue; // We should probably yield?
    return !rg_task_messages_waiting(display_task_queue);
}

void rg_display_write_rect_ppa(int left, int top, int width, int height, int stride, const uint16_t *buffer, uint32_t flags)
{
    RG_ASSERT_ARG(buffer);
    size_t out_size = ALIGN_UP(width * height, 32) * 2;


    //RG_LOGI("rg_display_write_rect: left: %d, top: %d, width: %d, height: %d, stride: %d", left, top, width, height, stride);


    if(width  == RG_SCREEN_WIDTH && height == RG_SCREEN_HEIGHT && stride == width*2){
        uint16_t *out_buf = lcd_get_buffer_ppa();
        s_srm_ops(buffer,out_buf,out_size, width,height);
        lcd_send_buffer_ppa(out_buf, out_size, 0, 0, height, width);
        rg_task_delay(1);
        return;
    }


    uint16_t *out_buf = heap_caps_aligned_calloc(32, out_size, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    s_srm_ops(buffer,out_buf,out_size, width,height);

    buffer = out_buf;

    int temp = width;
    width = height;
    height = temp;
    int top_temp = top;
    top =  left;
    left = top_temp;
    top =  display.screen.width  - top - height -1;
    // calc stride before clipping width
    stride = RG_MAX(stride, width * 2);

    // Clipping
    width = RG_MIN(width, display.screen.height - left);
    height = RG_MIN(height, display.screen.width - top);

    // This can happen when left or top is out of bound
    if (width < 0 || height < 0){
        free(out_buf);
        return;

    }


    // This will work for now because we rarely draw from different threads (so all we need is ensure
    // that we're not interrupting a display update). But what we SHOULD be doing is acquire a lock
    // before every call to lcd_set_window and release it only after the last call to lcd_send_buffer.
    if (!(flags & RG_DISPLAY_WRITE_NOSYNC))
        rg_display_sync(true);

    // This isn't really necessary but it makes sense to invalidate
    // the lines we're about to overwrite...
    for (size_t y = 0; y < height; ++y)
        screen_line_checksum[top + y] = 0;

    //lcd_set_window( top + display.screen.margins.top,left + display.screen.margins.left, width, height);

    // int window_top = -1;
    for (size_t y = 0; y < height;)
    {
        uint16_t *lcd_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        size_t num_lines = RG_MIN(LCD_BUFFER_LENGTH / width, height - y);

        // Copy line by line because stride may not match width
        for (size_t line = 0; line < num_lines; ++line)
        {
            uint16_t *src = (void *)buffer + ((y + line) * stride);
            uint16_t *dst = lcd_buffer + (line * width);
            if (flags & RG_DISPLAY_WRITE_NOSWAP)
            {
                memcpy(dst, src, width * 2);
            }
            else
            {
                for (size_t i = 0; i < width; ++i)
                    dst[i] = (src[i] >> 8) | (src[i] << 8);
            }
        }

        //RG_LOGI("set_window: top: %d, left: %d, width: %d, height: %d", y + top + display.screen.margins.left,display.screen.margins.top+left, width, num_lines);
        lcd_set_window( display.screen.margins.top+left ,y + top + display.screen.margins.left , width, num_lines);


        lcd_send_buffer(lcd_buffer, width * num_lines);
        rg_usleep(50);
        y += num_lines;
    }



    free(out_buf);
    lcd_sync();
}

void rg_display_clear_rect_ppa(int left, int top, int width, int height, uint16_t color_le)
{
    const uint16_t color_be = (color_le << 8) | (color_le >> 8);
    int pixels_remaining = width * height;

    if(width  == RG_SCREEN_WIDTH && height == RG_SCREEN_HEIGHT){

        for( int i= 0; i < FB_NUM; i++){
            uint16_t *color_be_buf = lcd_get_buffer_ppa();
            memset(color_be_buf, color_be, pixels_remaining * sizeof(uint16_t));
            lcd_send_buffer_ppa(color_be_buf, pixels_remaining, 0, 0, height, width);
        }
        return;
    }



    if (pixels_remaining > 0)
    {
        int new_left = top + display.screen.margins.top;
        int new_top = left + display.screen.margins.left  ;
        int new_width = height;
        int new_height = width;
        // RG_LOGI("left: %d, top: %d, width: %d, height: %d, new_left: %d, new_top: %d, new_width: %d, new_height: %d", left, top, width, height, new_left, new_top, new_width, new_height);
        lcd_set_window(new_left,new_top ,  new_width,new_height);
        while (pixels_remaining > 0)
        {
            uint16_t *buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
            int pixels = RG_MIN(pixels_remaining, LCD_BUFFER_LENGTH);
            for (size_t j = 0; j < pixels; ++j)
                buffer[j] = color_be;
            lcd_send_buffer(buffer, pixels);
            pixels_remaining -= pixels;
        }
    }
}

void rg_display_write_rect(int left, int top, int width, int height, int stride, const uint16_t *buffer, uint32_t flags)
{
#if  RG_SCREEN_DRIVER  == 2
    rg_display_write_rect_ppa(left, top, width, height, stride, buffer, flags);
#else

    RG_ASSERT_ARG(buffer);

    // calc stride before clipping width
    stride = RG_MAX(stride, width * 2);

    // Clipping
    width = RG_MIN(width, display.screen.width - left);
    height = RG_MIN(height, display.screen.height - top);

    // This can happen when left or top is out of bound
    if (width < 0 || height < 0)
        return;

    // This will work for now because we rarely draw from different threads (so all we need is ensure
    // that we're not interrupting a display update). But what we SHOULD be doing is acquire a lock
    // before every call to lcd_set_window and release it only after the last call to lcd_send_buffer.
    if (!(flags & RG_DISPLAY_WRITE_NOSYNC))
        rg_display_sync(true);

    // This isn't really necessary but it makes sense to invalidate
    // the lines we're about to overwrite...
    for (size_t y = 0; y < height; ++y)
        screen_line_checksum[top + y] = 0;

    lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);

    for (size_t y = 0; y < height;)
    {
        uint16_t *lcd_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        size_t num_lines = RG_MIN(LCD_BUFFER_LENGTH / width, height - y);

        // Copy line by line because stride may not match width
        for (size_t line = 0; line < num_lines; ++line)
        {
            uint16_t *src = (void *)buffer + ((y + line) * stride);
            uint16_t *dst = lcd_buffer + (line * width);
            if (flags & RG_DISPLAY_WRITE_NOSWAP)
            {
                memcpy(dst, src, width * 2);
            }
            else
            {
                for (size_t i = 0; i < width; ++i)
                    dst[i] = (src[i] >> 8) | (src[i] << 8);
            }
        }

        lcd_send_buffer(lcd_buffer, width * num_lines);
        y += num_lines;
    }

    lcd_sync();

#endif
}

void rg_display_clear_rect(int left, int top, int width, int height, uint16_t color_le)
{
#if RG_SCREEN_DRIVER == 2
    rg_display_clear_rect_ppa(left,top, width, height,color_le);
#else
    const uint16_t color_be = (color_le << 8) | (color_le >> 8);
    int pixels_remaining = width * height;
    if (pixels_remaining > 0)
    {
        lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);
        while (pixels_remaining > 0)
        {
            uint16_t *buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
            int pixels = RG_MIN(pixels_remaining, LCD_BUFFER_LENGTH);
            for (size_t j = 0; j < pixels; ++j)
                buffer[j] = color_be;
            lcd_send_buffer(buffer, pixels);
            pixels_remaining -= pixels;
        }
    }

#endif
}

void rg_display_clear_except(int left, int top, int width, int height, uint16_t color_le)
{
    // Clear everything except the specified area
    // FIXME: Do not ignore left/top...
    int left_offset = -display.screen.margins.left;
    int top_offset = -display.screen.margins.top;
    int horiz = (display.screen.real_width - width + 1) / 2;
    int vert = (display.screen.real_height - height + 1) / 2;
    rg_display_clear_rect(left_offset, top_offset, horiz, display.screen.real_height, color_le); // Left
    rg_display_clear_rect(left_offset + horiz + width, top_offset, horiz, display.screen.real_height, color_le); // Right
    rg_display_clear_rect(left_offset + horiz, top_offset, display.screen.real_width - horiz * 2, vert, color_le); // Top
    rg_display_clear_rect(left_offset + horiz, top_offset + vert + height, display.screen.real_width - horiz * 2, vert, color_le); // Bottom
}

void rg_display_clear(uint16_t color_le)
{
    // We ignore margins here, we want to fill the entire screen
    rg_display_clear_rect(-display.screen.margins.left, -display.screen.margins.top, display.screen.real_width,
                          display.screen.real_height, color_le);
}

bool rg_display_set_geometry(int width, int height, const rg_margins_t *margins)
{
    RG_ASSERT(width >= 64 && height >= 64, "Invalid resolution");
    // Temporary limitation because we have some fixed buffers to fix (and no, it's not as simple as moving them to the heap)...
    RG_ASSERT(width <= RG_SCREEN_WIDTH && height <= RG_SCREEN_HEIGHT, "Resolution cannot exceed RG_SCREEN_WIDTH*RG_SCREEN_HEIGHT!");
    // FIXME: Not thread safe at all, we should block any access to the display until this function returns...
    display.screen.real_width = width;
    display.screen.real_height = height;
    display.screen.margins = margins ? *margins : (rg_margins_t){0, 0, 0, 0};
    display.screen.width = display.screen.real_width - (display.screen.margins.left + display.screen.margins.right);
    display.screen.height = display.screen.real_height - (display.screen.margins.top + display.screen.margins.bottom);
    // display.screen.format = RG_PIXEL_565_BE;
    display.changed = true;
    // update_viewport_scaling();             // This will be implicitly done by the display task
    rg_gui_update_geometry();              // Let the GUI know that the geometry has changed
    rg_system_event(RG_EVENT_GEOMETRY, 0); // Let everybody know that the geometry has changed
    RG_LOGI("Screen: resolution=%dx%d (eff. %dx%d), margins=(%d %d %d %d), format=%d",
            display.screen.real_width, display.screen.real_height, display.screen.width, display.screen.height,
            display.screen.margins.left, display.screen.margins.top, display.screen.margins.right, display.screen.margins.bottom,
            display.screen.format);
    return true;
}

void rg_display_deinit(void)
{
    rg_task_send(display_task_queue, &(rg_task_msg_t){.type = RG_TASK_MSG_STOP}, -1);
    // lcd_set_backlight(0);
    lcd_deinit();
    RG_LOGI("Display terminated.\n");
}

void rg_display_init(void)
{
    RG_LOGI("Initialization...\n");
    // TO DO: We probably should call the setters to ensure valid values...
    config = (rg_display_config_t){
        .backlight = rg_settings_get_number(NS_GLOBAL, SETTING_BACKLIGHT, 80),
        .scaling = rg_settings_get_number(NS_APP, SETTING_SCALING, RG_DISPLAY_SCALING_FIT),
        .filter = rg_settings_get_number(NS_APP, SETTING_FILTER, RG_DISPLAY_FILTER_BOTH),
        .rotation = rg_settings_get_number(NS_APP, SETTING_ROTATION, RG_DISPLAY_ROTATION_AUTO),
        .border_file = rg_settings_get_string(NS_APP, SETTING_BORDER, NULL),
        .custom_zoom = rg_settings_get_number(NS_APP, SETTING_CUSTOM_ZOOM, 1.0),
    };
    display = (rg_display_t){
        .screen.real_width = RG_SCREEN_WIDTH,
        .screen.real_height = RG_SCREEN_HEIGHT,
        .screen.width = RG_SCREEN_WIDTH,
        .screen.height = RG_SCREEN_HEIGHT,
        .screen.margins = RG_SCREEN_VISIBLE_AREA,
        .changed = true,
    };
    display.screen.width -= display.screen.margins.left + display.screen.margins.right;
    display.screen.height -= display.screen.margins.top + display.screen.margins.bottom;
    lcd_init();
    rg_display_clear(C_BLACK);
    rg_task_delay(80); // Wait for the screen be cleared before turning on the backlight (40ms doesn't seem to be enough...)
    lcd_set_backlight(config.backlight);
    display_task_queue = rg_task_create("rg_display", &display_task, NULL, 4 * 1024, 1, RG_TASK_PRIORITY_6, 1);
    if (config.border_file)
        load_border_file(config.border_file);
    RG_LOGI("Display ready.\n");
}
