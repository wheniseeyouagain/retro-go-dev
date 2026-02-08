// Target definition
#define RG_TARGET_NAME             "Katarina"
#define RG_LANG_DEFAULT RG_LANG_ZH
#define ENABLE_CHINESE_12           1
#define RG_FONT_DEFAULT RG_FONT_CHINESE_12

// Storage
#define RG_STORAGE_SDSPI_HOST       SPI3_HOST
#define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT
#define RG_STORAGE_ROOT             "/sd"

// Audio
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_80M //SPI_MASTER_FREQ_40M、 SPI_MASTER_FREQ_80M
#define RG_SCREEN_BACKLIGHT         1
#define RG_GPIO_LCD_BCKL_INVERT     1
#define RG_SCREEN_WIDTH             240
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_SCREEN_INIT()                                                                                     \
    ILI9341_CMD(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);                                                         \
    ILI9341_CMD(0xB7, 0x35);                                                                                 \
    ILI9341_CMD(0xBB, 0x38);                                                                                 \
    ILI9341_CMD(0xC0, 0x2C);                                                                                 \
    ILI9341_CMD(0xC2, 0x01);                                                                                 \
    ILI9341_CMD(0xC3, 0x11);                                                                                 \
    ILI9341_CMD(0xC4, 0x20);                                                                                 \
    ILI9341_CMD(0xC6, 0x0F);                                                                                 \
    ILI9341_CMD(0xD0, 0xA4, 0xA1);                                                                           \
    ILI9341_CMD(0xE0, 0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19);   \
    ILI9341_CMD(0xE1, 0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19);

// Input
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     GPIO_NUM_38,  .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  GPIO_NUM_14,  .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   GPIO_NUM_21,  .pullup = 1, .level = 0},\
    {RG_KEY_LEFT,   GPIO_NUM_39,  .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, GPIO_NUM_18,  .pullup = 1, .level = 0},\
    {RG_KEY_START,  GPIO_NUM_17,  .pullup = 1, .level = 0},\
    {RG_KEY_MENU,   GPIO_NUM_4,  .pullup = 1, .level = 0},\
    {RG_KEY_OPTION, GPIO_NUM_2,  .pullup = 1, .level = 0},\
    {RG_KEY_A,      GPIO_NUM_16,  .pullup = 1, .level = 0},\
    {RG_KEY_B,      GPIO_NUM_8,  .pullup = 1, .level = 0},\
}

// Battery
#define RG_BATTERY_DRIVER           1
#define RG_BATTERY_ADC_UNIT         ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL      ADC_CHANNEL_0
// #define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3150.f) / (4100.f - 3150.f) * 100.f)
// #define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// SPI Display (back up working)
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_9
#define RG_GPIO_LCD_CLK             GPIO_NUM_10
#define RG_GPIO_LCD_CS              GPIO_NUM_3
#define RG_GPIO_LCD_DC              GPIO_NUM_12
#define RG_GPIO_LCD_BCKL            GPIO_NUM_13
#define RG_GPIO_LCD_RST             GPIO_NUM_11

// SD Card
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_15
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_6
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_7
#define RG_GPIO_SDSPI_CS            GPIO_NUM_5

// External I2S DAC
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_41
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_42
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_40
