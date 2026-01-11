#include "rg_system.h"
#include "rg_input.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <driver/adc.h>
// This is a lazy way to silence deprecation notices on some esp-idf versions...
// This hardcoded value is the first thing to check if something stops working!
#define ADC_ATTEN_DB_11 3
#else
#include <SDL2/SDL.h>
#endif

#include "esp_idf_version.h"
#if RG_BATTERY_DRIVER == 1
    #if ESP_IDF_VERSION_MAJOR >= 5  // IDF 5.x 及以上
        #include "esp_err.h"
        #include "esp_adc/adc_oneshot.h"
        #include "esp_adc/adc_cali.h"
        #include "esp_adc/adc_cali_scheme.h"
        static adc_oneshot_unit_handle_t battery_adc_handle = NULL;
        static adc_cali_handle_t battery_adc_cali_handle = NULL;
        static bool battery_adc_calibrated = false;
    #else  // IDF 4.x 及以下
        #include <esp_adc_cal.h>
        static esp_adc_cal_characteristics_t adc_chars;
    #endif
#endif

#ifdef RG_GAMEPAD_ADC_MAP
static rg_keymap_adc_t keymap_adc[] = RG_GAMEPAD_ADC_MAP;
#endif
#ifdef RG_GAMEPAD_GPIO_MAP
static rg_keymap_gpio_t keymap_gpio[] = RG_GAMEPAD_GPIO_MAP;
#endif
#ifdef RG_GAMEPAD_I2C_MAP
static rg_keymap_i2c_t keymap_i2c[] = RG_GAMEPAD_I2C_MAP;
#endif
#ifdef RG_GAMEPAD_KBD_MAP
static rg_keymap_kbd_t keymap_kbd[] = RG_GAMEPAD_KBD_MAP;
#endif
#ifdef RG_GAMEPAD_SERIAL_MAP
static rg_keymap_serial_t keymap_serial[] = RG_GAMEPAD_SERIAL_MAP;
#endif
#ifdef RG_GAMEPAD_VIRT_MAP
static rg_keymap_virt_t keymap_virt[] = RG_GAMEPAD_VIRT_MAP;
#endif
static bool input_task_running = false;
static uint32_t gamepad_state = -1; // _Atomic
static uint32_t gamepad_mapped = 0;
static rg_battery_t battery_state = {0};

#define UPDATE_GLOBAL_MAP(keymap)                 \
    for (size_t i = 0; i < RG_COUNT(keymap); ++i) \
        gamepad_mapped |= keymap[i].key;          \

#ifdef ESP_PLATFORM
static inline int adc_get_raw(adc_unit_t unit, adc_channel_t channel)
{
    if (unit == ADC_UNIT_1)
    {
        return adc1_get_raw(channel);
    }
    else if (unit == ADC_UNIT_2)
    {
        int adc_raw_value = -1;
        if (adc2_get_raw(channel, ADC_WIDTH_MAX - 1, &adc_raw_value) != ESP_OK)
            RG_LOGE("ADC2 reading failed, this can happen while wifi is active.");
        return adc_raw_value;
    }
    RG_LOGE("Invalid ADC unit %d", (int)unit);
    return -1;
}
#endif

#if ESP_IDF_VERSION_MAJOR >= 5 && RG_BATTERY_DRIVER == 1
static bool battery_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        RG_LOGI("Battery ADC calibration success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        RG_LOGW("eFuse not burnt, skip software calibration");
    } else {
        RG_LOGE("Invalid arg or no memory");
    }
    return calibrated;
}

static void battery_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
#endif

bool rg_input_read_battery_raw(rg_battery_t *out)
{
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;


#if RG_BATTERY_DRIVER == 1 /* ADC */
    #if ESP_IDF_VERSION_MAJOR >= 5  // IDF 5.x 及以上
        if (!battery_adc_handle)
            return false;

        int adc_raw = 0;
        int voltage_mv = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (adc_oneshot_read(battery_adc_handle, RG_BATTERY_ADC_CHANNEL, &adc_raw) != ESP_OK)
                return false;

            if (adc_cali_raw_to_voltage(battery_adc_cali_handle, adc_raw, &voltage_mv) != ESP_OK)
                return false;

            raw_value += voltage_mv;
        }
        raw_value /= 4;
    #else
        for (int i = 0; i < 4; ++i)
        {
            int value = adc_get_raw(RG_BATTERY_ADC_UNIT, RG_BATTERY_ADC_CHANNEL);
            if (value < 0)
                return false;
            raw_value += esp_adc_cal_raw_to_voltage(value, &adc_chars);
        }
        raw_value /= 4;
    #endif
#elif RG_BATTERY_DRIVER == 2 /* I2C */
    uint8_t data[5];
    if (!rg_i2c_read(0x20, -1, &data, 5))
        return false;
    raw_value = data[4];
    charging = data[4] == 255;
#else
    return false;
#endif

    if (!out)
        return true;

    *out = (rg_battery_t){
        .level = RG_MAX(0.f, RG_MIN(100.f, RG_BATTERY_CALC_PERCENT(raw_value))),
        .volts = RG_BATTERY_CALC_VOLTAGE(raw_value),
        .present = present,
        .charging = charging,
    };
    return true;
}

bool rg_input_read_gamepad_raw(uint32_t *out)
{
    uint32_t state = 0;

#if defined(RG_GAMEPAD_ADC_MAP)
    static int old_adc_values[RG_COUNT(keymap_adc)];
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        int value = adc_get_raw(mapping->unit, mapping->channel);
        if (value >= mapping->min && value <= mapping->max)
        {
            if (abs(old_adc_values[i] - value) < RG_GAMEPAD_ADC_FILTER_WINDOW)
                state |= mapping->key;
            // else
            //     RG_LOGD("Rejected input: %d", old_adc_values[i] - value);
            old_adc_values[i] = value;
        }
    }
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (gpio_get_level(mapping->num) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    uint32_t buttons = 0;
#if defined(RG_I2C_GPIO_DRIVER)
    int data0 = rg_i2c_gpio_read_port(0), data1 = rg_i2c_gpio_read_port(1);
    if (data0 > -1) // && data1 > -1)
    {
        buttons = (data1 << 8) | (data0);
#elif defined(RG_TARGET_T_DECK_PLUS)
    uint8_t data[5];
    if (rg_i2c_read(T_DECK_KBD_ADDRESS, -1, &data, 5))
    {
        buttons = ((data[0] << 25) | (data[1] << 18) | (data[2] << 11) | ((data[3] & 0xF8) << 4) | (data[4]));
#else
    uint8_t data[5];
    if (rg_i2c_read(RG_I2C_GPIO_ADDR, -1, &data, 5))
    {
        buttons = (data[2] << 8) | (data[1]);
#endif
        for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
        {
            const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
            if (((buttons >> mapping->num) & 1) == mapping->level)
                state |= mapping->key;
        }
    }
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
#ifdef RG_TARGET_SDL2
    int numkeys = 0;
    const uint8_t *keys = SDL_GetKeyboardState(&numkeys);
    for (size_t i = 0; i < RG_COUNT(keymap_kbd); ++i)
    {
        const rg_keymap_kbd_t *mapping = &keymap_kbd[i];
        if (mapping->src < 0 || mapping->src >= numkeys)
            continue;
        if (keys[mapping->src])
            state |= mapping->key;
    }
#else
#warning "not implemented"
#endif
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    rg_usleep(5);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 1);
    rg_usleep(1);
    uint32_t buttons = 0;
    for (int i = 0; i < 16; i++)
    {
        buttons |= gpio_get_level(RG_GPIO_GAMEPAD_DATA) << (15 - i);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 0);
        rg_usleep(1);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
        rg_usleep(1);
    }
    for (size_t i = 0; i < RG_COUNT(keymap_serial); ++i)
    {
        const rg_keymap_serial_t *mapping = &keymap_serial[i];
        if (((buttons >> mapping->num) & 1) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_VIRT_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_virt); ++i)
    {
        if (state == keymap_virt[i].src)
            state = keymap_virt[i].key;
    }
#endif

    if (out)
        *out = state;
    return true;
}

static void input_task(void *arg)
{
    uint8_t debounce[RG_KEY_COUNT];
    uint32_t local_gamepad_state = 0;
    uint32_t state;
    int64_t next_battery_update = 0;

    // Start the task with debounce history full to allow a button held during boot to be detected
    memset(debounce, 0xFF, sizeof(debounce));
    input_task_running = true;

    while (input_task_running)
    {
        if (rg_input_read_gamepad_raw(&state))
        {
            for (int i = 0; i < RG_KEY_COUNT; ++i)
            {
                uint32_t val = ((debounce[i] << 1) | ((state >> i) & 1));
                debounce[i] = val & 0xFF;

                if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1)) == ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1))
                {
                    local_gamepad_state |= (1 << i); // Pressed
                }
                else if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_RELEASE) - 1)) == 0)
                {
                    local_gamepad_state &= ~(1 << i); // Released
                }
            }
            gamepad_state = local_gamepad_state;
        }

        if (rg_system_timer() >= next_battery_update)
        {
            rg_battery_t temp = {0};
            if (rg_input_read_battery_raw(&temp))
            {
                if (fabsf(battery_state.level - temp.level) < RG_BATTERY_UPDATE_THRESHOLD)
                    temp.level = battery_state.level;
                if (fabsf(battery_state.volts - temp.volts) < RG_BATTERY_UPDATE_THRESHOLD_VOLT)
                    temp.volts = battery_state.volts;
            }
            battery_state = temp;
            next_battery_update = rg_system_timer() + 2 * 1000000; // update every 2 seconds
        }

        rg_task_delay(10);
    }

    input_task_running = false;
    gamepad_state = -1;
}

void rg_input_init(void)
{
    RG_ASSERT(!input_task_running, "Input already initialized!");

#if defined(RG_GAMEPAD_ADC_MAP)
    RG_LOGI("Initializing ADC gamepad driver...");
    adc1_config_width(ADC_WIDTH_MAX - 1);
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        if (mapping->unit == ADC_UNIT_1)
            adc1_config_channel_atten(mapping->channel, mapping->atten);
        else if (mapping->unit == ADC_UNIT_2)
            adc2_config_channel_atten(mapping->channel, mapping->atten);
        else
            RG_LOGE("Invalid ADC unit %d!", (int)mapping->unit);
    }
    UPDATE_GLOBAL_MAP(keymap_adc);
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    RG_LOGI("Initializing GPIO gamepad driver...");
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        gpio_set_direction(mapping->num, GPIO_MODE_INPUT);
        if (mapping->pullup && mapping->pulldown)
            gpio_set_pull_mode(mapping->num, GPIO_PULLUP_PULLDOWN);
        else if (mapping->pullup || mapping->pulldown)
            gpio_set_pull_mode(mapping->num, mapping->pullup ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
        else
            gpio_set_pull_mode(mapping->num, GPIO_FLOATING);
    }
    UPDATE_GLOBAL_MAP(keymap_gpio);
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    RG_LOGI("Initializing I2C gamepad driver...");
    rg_i2c_init();
#if defined(RG_I2C_GPIO_DRIVER)
    for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
    {
        const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
        if (mapping->pullup)
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT_PULLUP);
        else
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT);
    }
#elif defined(RG_TARGET_T_DECK_PLUS)
    rg_i2c_write_byte(T_DECK_KBD_ADDRESS, -1, T_DECK_KBD_MODE_RAW_CMD);
#endif
    UPDATE_GLOBAL_MAP(keymap_i2c);
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
    RG_LOGI("Initializing KBD gamepad driver...");
    UPDATE_GLOBAL_MAP(keymap_kbd);
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    RG_LOGI("Initializing SERIAL gamepad driver...");
    gpio_set_direction(RG_GPIO_GAMEPAD_CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_DATA, GPIO_MODE_INPUT);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
    UPDATE_GLOBAL_MAP(keymap_serial);
#endif


#if RG_BATTERY_DRIVER == 1 /* ADC */
    RG_LOGI("Initializing ADC battery driver...");
    #if ESP_IDF_VERSION_MAJOR >= 5
        RG_LOGI("Initializing ADC battery driver (new API)...");
        // Initialize ADC oneshot unit
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = RG_BATTERY_ADC_UNIT,
        };
        if (RG_BATTERY_ADC_UNIT == ADC_UNIT_2)
        {
            init_config.ulp_mode = ADC_ULP_MODE_DISABLE;
        }
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &battery_adc_handle));

        // Configure ADC channel
        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(battery_adc_handle, RG_BATTERY_ADC_CHANNEL, &config));

        // Initialize calibration
        battery_adc_calibrated = battery_adc_calibration_init(RG_BATTERY_ADC_UNIT, RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12, &battery_adc_cali_handle);
    #else
        if (RG_BATTERY_ADC_UNIT == ADC_UNIT_1)
        {
            adc1_config_width(ADC_WIDTH_MAX - 1); // there is no adc2_config_width
            adc1_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
            esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
        }
        else if (RG_BATTERY_ADC_UNIT == ADC_UNIT_2)
        {
            adc2_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
            esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
        }
        else
        {
            RG_LOGE("Only ADC1 and ADC2 are supported for ADC battery driver!");
        }
    #endif
#endif

    // The first read returns bogus data in some drivers, waste it.
    rg_input_read_gamepad_raw(NULL);

    // Start background polling
    rg_task_create("rg_input", &input_task, NULL, 3 * 1024, 1, RG_TASK_PRIORITY_6, 1);
    while (gamepad_state == -1)
        rg_task_yield();
    RG_LOGI("Input ready. state=" PRINTF_BINARY_16 "\n", PRINTF_BINVAL_16(gamepad_state));
}

void rg_input_deinit(void)
{
    input_task_running = false;
    // while (gamepad_state != -1)
    //     rg_task_yield();
    RG_LOGI("Input terminated.\n");
}

bool rg_input_key_is_present(rg_key_t mask)
{
    return (gamepad_mapped & mask) == mask;
}

uint32_t rg_input_read_gamepad(void)
{
#ifdef RG_TARGET_SDL2
    SDL_PumpEvents();
#endif
    return gamepad_state;
}

bool rg_input_key_is_pressed(rg_key_t mask)
{
    return (bool)(rg_input_read_gamepad() & mask);
}

bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + timeout_ms * 1000);
    while (rg_input_key_is_pressed(mask) != pressed)
    {
        if (rg_system_timer() > expiration)
            return false;
        rg_task_delay(10);
    }
    return true;
}

rg_battery_t rg_input_read_battery(void)
{
    return battery_state;
}

const char *rg_input_get_key_name(rg_key_t key)
{
    switch (key)
    {
    case RG_KEY_UP: return "Up";
    case RG_KEY_RIGHT: return "Right";
    case RG_KEY_DOWN: return "Down";
    case RG_KEY_LEFT: return "Left";
    case RG_KEY_SELECT: return "Select";
    case RG_KEY_START: return "Start";
    case RG_KEY_MENU: return "Menu";
    case RG_KEY_OPTION: return "Option";
    case RG_KEY_A: return "A";
    case RG_KEY_B: return "B";
    case RG_KEY_X: return "X";
    case RG_KEY_Y: return "Y";
    case RG_KEY_L: return "Left Shoulder";
    case RG_KEY_R: return "Right Shoulder";
    case RG_KEY_NONE: return "None";
    default: return "Unknown";
    }
}
