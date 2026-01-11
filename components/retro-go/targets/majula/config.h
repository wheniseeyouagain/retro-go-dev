#define RG_TARGET_NAME             "majula"
#define RG_LANG_DEFAULT RG_LANG_ZH
#define RG_FONT_DEFAULT RG_FONT_CHINESE_24
#define RG_PROJECT_WEBSITE "https://github.com/wheniseeyouagain/retro-go-hack"


/****************************************************************************
 * Storage                                                                  *
 ****************************************************************************/
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_HIGHSPEED
#define RG_GPIO_SDMMC_CLK           GPIO_NUM_43
#define RG_GPIO_SDMMC_CMD	        GPIO_NUM_44
#define RG_GPIO_SDMMC_D0	        GPIO_NUM_39
#define RG_GPIO_SDMMC_D1	        GPIO_NUM_40
#define RG_GPIO_SDMMC_D2	        GPIO_NUM_41
#define RG_GPIO_SDMMC_D3	        GPIO_NUM_42


/****************************************************************************
 * Audio                                                                    *
 ****************************************************************************/
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_7
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_8
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_6
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_9


/****************************************************************************
 * Video                                                                    *
 ****************************************************************************/
#define RG_SCREEN_DRIVER            2   // 0 = ILI9341/ST7789
#define RG_SCREEN_BACKLIGHT         1
#define RG_GPIO_LCD_BCKL_INVERT     1
#define RG_SCREEN_WIDTH             640
#define RG_SCREEN_HEIGHT            480
#define RG_SCREEN_ROTATE            0   // 90度软件旋转
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}  // Left, Top, Right, Bottom
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}  // Left, Top, Right, Bottom
#define RG_SCREEN_PARTIAL_UPDATES   1
#define RG_GPIO_LCD_RST             GPIO_NUM_23
#define RG_GPIO_LCD_BCKL            GPIO_NUM_5


/****************************************************************************
 * Input                                                                    *
 ****************************************************************************/
// Refer to rg_input.h to see all available RG_KEY_* and RG_GAMEPAD_*_MAP types
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_LEFT,   .num = GPIO_NUM_20, .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_18, .pullup = 1, .level = 0},\
    {RG_KEY_UP,     .num = GPIO_NUM_17, .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_19, .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_21, .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_10, .pullup = 1, .level = 0},\
    {RG_KEY_MENU,   .num = GPIO_NUM_15, .pullup = 1, .level = 0},\
    {RG_KEY_OPTION, .num = GPIO_NUM_16,  .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_14, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_13, .pullup = 1, .level = 0},\
    {RG_KEY_X,      .num = GPIO_NUM_12, .pullup = 1, .level = 0},\
    {RG_KEY_Y,      .num = GPIO_NUM_11, .pullup = 1, .level = 0},\
    {RG_KEY_L,      .num = GPIO_NUM_1, .pullup = 1, .level = 0},\
    {RG_KEY_R,      .num = GPIO_NUM_0,  .pullup = 1, .level = 0},\
    {RG_KEY_VOLUP,  .num = GPIO_NUM_34, .pullup = 1, .level = 0},\
    {RG_KEY_VOLDOWN,.num = GPIO_NUM_35,  .pullup = 1, .level = 0},\
}


/****************************************************************************
 * Battery                                                                  *
 ****************************************************************************/
 #define RG_BATTERY_DRIVER            1   // 1 = ADC, 2 = MRGC
 #define RG_BATTERY_ADC_UNIT          ADC_UNIT_1
 #define RG_BATTERY_ADC_CHANNEL       ADC_CHANNEL_6
 #define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3150.f) / (4100.f - 3150.f) * 100.f)
 #define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)