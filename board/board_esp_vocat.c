/*
 * Espressif ESP-VoCat (喵伴 / EchoEar) —— Muse ESP32 Device SDK 板级驱动
 *
 * 本文件是「官方 xiaozhi-esp32 esp-vocat 板级定义」到 Muse 框架的适配层：
 *   - 硬件契约、GPIO、双 PCB 版本逻辑来自 78/xiaozhi-esp32
 *     main/boards/espressif/esp-vocat/{config.h,esp_vocat.cc}（MIT）。
 *   - muse_board_t 接口与 LVGL/audio 装配方式来自 facebookincubator/muse-gadget-sdk
 *     esp32/components/muse/boards/board_ostb_echoear_2st.c（Apache-2.0）。
 *   - ST77916 初始化表见同目录 esp_vocat_display_init.h（MIT 声明已保留）。
 *
 * 注意：Muse SDK 里已有的 ostb-echoear-2st 是第三方复刻板，其文档明确写着
 * "Stock ESP-VoCat GPIOs do not match"。本文件只复用它的代码骨架，不复用引脚。
 *
 * 已知边界（首次适配，见 devices/esp-vocat.md）：
 *   - 音频按 Muse 框架的 16 kHz / 16-bit / STD 双槽；官方的 24 kHz TDM 4 槽 + AEC
 *     参考通道未移植，故 ES7210 只选 MIC1|MIC2 并混合两路（mic_slot = -1）。
 *   - BMI270、microSD、旋转底座 UART1、原生电容触摸、UVC 摄像头未接入。
 *   - codec_power 留 NULL 常供电：GPIO48 同时是 PCB 版本探测脚，反复断电会丢
 *     codec 寄存器并可能触发重启循环。
 */
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"
#include "esp_vocat_display_init.h"
#include "esp_vocat_touch.h"

#define TAG "esp_vocat"

#define LCD_HOST SPI2_HOST
#define LCD_RES 360
#define DRAW_BUF_LINES 10                                  /* LVGL 局部刷新缓冲行数 */
#define MAX_TRANSFER_SZ (LCD_RES * 80 * sizeof(uint16_t)) /* 官方 InitializeSpi() 的值 */

/* ---- 两版 PCB 共用的 GPIO（官方 config.h） ---- */
#define I2C_SDA_GPIO 2
#define I2C_SCL_GPIO 1
#define I2S_MCLK_GPIO 42
#define I2S_BCLK_GPIO 40
#define I2S_WS_GPIO 39
#define I2S_DOUT_GPIO 41
#define LCD_PCLK_GPIO 18
#define LCD_CS_GPIO 14
#define LCD_D0_GPIO 46
#define LCD_D1_GPIO 13
#define LCD_D2_GPIO 11
#define LCD_D3_GPIO 12
#define LCD_BL_GPIO 44
#define TP_INT_GPIO 10
#define POWER_CTRL_GPIO 9  /* 低有效，官方 InitializeButtons() 置 0 */
#define CODEC_POWER_GPIO 48 /* 同时是 PCB 版本探测脚 */
#define BOOT_BUTTON_GPIO 0

/* ---- V1.0 / V1.2 差异引脚 ---- */
#define V10_LCD_RST_GPIO 3
#define V10_I2S_DIN_GPIO 15
#define V10_PA_GPIO 4
#define V12_LCD_RST_GPIO 47 /* 且复位极性改为高有效 */
#define V12_I2S_DIN_GPIO 3
#define V12_PA_GPIO 15

/* 官方 6 个可变引脚中，本驱动实际消费这 3 个。
 * 另外 3 个（UART1_TX/RX、TOUCH_PAD2）服务于官方固件的旋转底座与电容触摸，
 * Muse 框架没有这两类外设，故不移植、也不需要切换。 */
static int s_lcd_rst = V10_LCD_RST_GPIO;
static int s_i2s_din = V10_I2S_DIN_GPIO;
static int s_pa_pin = V10_PA_GPIO;

/* PCB 版本缓存：软复位不重新探测，因为 codec 电源轨残电会造成假 ACK（issue #1202）。
 * 只在真正的上电复位时探测，其余复位沿用 RTC_NOINIT 里的结果。 */
#define PCB_VERSION_MAGIC 0x31434256u /* "VBC1"，与官方一致 */
static RTC_NOINIT_ATTR uint32_t s_pcb_magic;
static RTC_NOINIT_ATTR uint8_t s_pcb_cached;
static uint8_t s_pcb_version; /* 0 = V1.0, 1 = V1.2 */

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_touch;
static i2c_master_dev_handle_t s_gauge;
static esp_lcd_panel_handle_t s_panel;
static muse_gpio_button_t s_boot;

/* display_start() 要注册 LVGL 输入回调，但实现放在后面的「触摸」小节。 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

/* 电量计：0x55，小端 16 位寄存器。芯片型号未证实，按官方 Charge 类的
 * BQ27220/BQ27441 风格寄存器读取（reg 0x08/0x0A/0x0C/0x14/0x2C）。 */
#define GAUGE_ADDR 0x55
#define GAUGE_VOLTAGE 0x08        /* mV */
#define GAUGE_BATTERY_STATUS 0x0A /* BIT0 = DSG（放电中） */
#define GAUGE_CURRENT 0x0C        /* mA，有符号，流入电芯为正 */
#define GAUGE_AVG_CURRENT 0x14    /* mA，有符号 */
#define GAUGE_SOC 0x2C            /* % */
#define CHARGING_MA 30            /* 官方 kChargingCurrentMa */

/* 官方 InitializeSt77916Display() 不反转颜色；OSTB 复刻板的板文件调用了
 * esp_lcd_panel_invert_color(panel, true)。上板若发现颜色反了，编译时加
 * -DESP_VOCAT_INVERT_COLOR=1 即可，不用改代码。 */
#ifndef ESP_VOCAT_INVERT_COLOR
#define ESP_VOCAT_INVERT_COLOR 0
#endif

static esp_err_t gauge_read(uint8_t reg, uint16_t *out)
{
    uint8_t b[2];
    /* 官方 ReadPeripheralRegs() 最多重试 3 次，失败只告警不影响系统 */
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (i2c_master_transmit_receive(s_gauge, &reg, 1, b, sizeof(b), 50) == ESP_OK) {
            *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10 << attempt));
    }
    return ESP_FAIL;
}

/* 把板级可变引脚整体切到 V1.2 布局。
 * 必须在「缓存命中」路径上也调用：audio_init()/display_start() 远晚于探测函数返回，
 * 且它们读的是这些全局量。 */
static void apply_v12_pins(void)
{
    s_lcd_rst = V12_LCD_RST_GPIO;
    s_i2s_din = V12_I2S_DIN_GPIO;
    s_pa_pin = V12_PA_GPIO;
}

/* 原样移植官方 DetectPcbVersion()。顺序和延时都不要简化：
 *  1) 先写 GPIO48 输出锁存器，再 gpio_config 配成输出。外部复位后锁存器默认 0，
 *     若先配输出会瞬间拉低 V1.2 的 codec 电源轨，首次触摸/电量计读失败并重启。
 *  2) 只有 ESP_RST_POWERON 才真正探测；其它复位直接读 RTC 缓存。
 *  3) 探测失败时保持 GPIO48 高电平（安全兜底），且「不写缓存」，下次启动重试。 */
static uint8_t detect_pcb_version(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    gpio_set_level(CODEC_POWER_GPIO, reason != ESP_RST_POWERON ? 1 : 0);
    const gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << CODEC_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));

    if (reason != ESP_RST_POWERON && s_pcb_magic == PCB_VERSION_MAGIC && s_pcb_cached <= 1) {
        if (s_pcb_cached == 1) {
            /* V1.2 的 codec 电源由 GPIO48 供给，先确保供电并等触摸控制器上电，
             * 否则对未就绪芯片的 I2C 读会 abort 整个系统。 */
            ESP_ERROR_CHECK(gpio_set_level(CODEC_POWER_GPIO, 1));
            for (int i = 0; i < 10; i++) {
                if (i2c_master_probe(s_i2c, 0x15, 50) == ESP_OK) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            apply_v12_pins();
        }
        ESP_LOGI(TAG, "PCB V1.%d (cached, reset reason %d)", (int)s_pcb_cached, (int)reason);
        return s_pcb_cached;
    }

    /* 上电复位：GPIO48 拉低 100 ms 后探 ES8311(0x18)。
     * 仍应答 = codec 电源不受 GPIO48 控制 = V1.0；
     * 不应答 = V1.2，拉高后等 SY8088 电源轨和 ES8311 起来（冷启动 50 ms 不够）。 */
    ESP_ERROR_CHECK(gpio_set_level(CODEC_POWER_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    bool codec_alive = (i2c_master_probe(s_i2c, 0x18, 100) == ESP_OK);
    uint8_t version = 0;

    if (codec_alive) {
        ESP_LOGI(TAG, "PCB V1.0 (codec alive with GPIO48 low)");
    } else {
        ESP_ERROR_CHECK(gpio_set_level(CODEC_POWER_GPIO, 1));
        for (int i = 0; i < 10 && !codec_alive; i++) {
            vTaskDelay(pdMS_TO_TICKS(200));
            codec_alive = (i2c_master_probe(s_i2c, 0x18, 100) == ESP_OK);
        }
        if (!codec_alive) {
            /* 两次都失败：GPIO48 保持高电平。V1.0 上该脚悬空无影响；
             * V1.2 上能保住 codec 供电，避免未供电芯片经 ESD 钳位 I2C 总线导致重启循环。
             * 不写缓存，下一次上电重新探测。 */
            ESP_LOGE(TAG, "PCB version detection failed; codec rail left on");
            return 0;
        }
        ESP_LOGI(TAG, "PCB V1.2 (codec powered by GPIO48)");
        version = 1;
        apply_v12_pins();
    }

    s_pcb_magic = PCB_VERSION_MAGIC;
    s_pcb_cached = version;
    return version;
}

/* muse_board_t.init：最先运行。顺序对齐官方构造函数：
 * InitializeI2c → DetectPcbVersion → 触摸/电量计 → InitializeButtons(POWER_CTRL=0)。
 * SPI/显示/音频分别由 display_start()/audio_init() 稍后触发（框架保证 init 先跑）。 */
static esp_err_t init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "I2C bus (SDA2/SCL1)");

    s_pcb_version = detect_pcb_version();

    /* CST816S @0x15，INT=GPIO10。官方用中断，Muse 的其它板用轮询读取，
     * 这里按框架惯例轮询 reg 0x02 六字节，不注册 ISR（见 touch_read）。 */
    const i2c_device_config_t tp_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x15,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &tp_cfg, &s_touch), TAG, "touch device");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c, 0x15, 100), TAG, "touch probe");

    /* 电量计是可选外设：读不到只降级为「无电池数据」，不让整板启动失败。 */
    const i2c_device_config_t gauge_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_i2c, &gauge_cfg, &s_gauge) == ESP_OK) {
        uint16_t mv;
        if (gauge_read(GAUGE_VOLTAGE, &mv) != ESP_OK) {
            ESP_LOGW(TAG, "no fuel gauge at 0x55: battery readings unavailable");
            i2c_master_bus_rm_device(s_gauge);
            s_gauge = NULL;
        }
    }

    /* POWER_CTRL(GPIO9) 低有效，官方置 0 保持外设电源域开启。 */
    const gpio_config_t pwr = {
        .pin_bit_mask = (1ULL << POWER_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pwr), TAG, "POWER_CTRL");
    ESP_RETURN_ON_ERROR(gpio_set_level(POWER_CTRL_GPIO, 0), TAG, "POWER_CTRL low");

    /* BOOT(GPIO0) 作为 talk 按钮：官方用它切换聊天/配网，Muse 用它产生 TALK 边沿。 */
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_boot, BOOT_BUTTON_GPIO), TAG, "BOOT button");
    ESP_LOGI(TAG, "init done: PCB V1.%d, LCD_RST=%d I2S_DIN=%d PA=%d",
             (int)s_pcb_version, s_lcd_rst, s_i2s_din, s_pa_pin);
    return ESP_OK;
}

/* ---- 显示 ---- */
static lv_display_t *display_start(lv_indev_t **touch)
{
    *touch = NULL;
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_ch) != ESP_OK) {
        return NULL;
    }

    /* 官方 TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG：4 根数据线 + CLK，
     * CS 在 panel_io 里给。data0..data3 与 mosi/miso/quadwp/quadhd 是同一组别名。 */
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_PCLK_GPIO,
        .data0_io_num = LCD_D0_GPIO,
        .data1_io_num = LCD_D1_GPIO,
        .data2_io_num = LCD_D2_GPIO,
        .data3_io_num = LCD_D3_GPIO,
        .max_transfer_sz = MAX_TRANSFER_SZ,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = -1, /* QSPI 无 D/C 线，命令以 32 位发送 */
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {.quad_mode = true},
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }

    st77916_vendor_config_t vendor = {
        .init_cmds = esp_vocat_init_cmds,
        .init_cmds_size = sizeof(esp_vocat_init_cmds) / sizeof(esp_vocat_init_cmds[0]),
        .flags = {.use_qspi_interface = true},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = s_lcd_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        /* 官方：reset_active_high = pcb_version。V1.2 的 LCD RST 是高有效，
         * 写错会导致面板一直处于复位、黑屏。 */
        .flags = {.reset_active_high = s_pcb_version},
        .vendor_config = &vendor,
    };
    if (esp_lcd_new_panel_st77916(io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    if (esp_lcd_panel_reset(s_panel) != ESP_OK || esp_lcd_panel_init(s_panel) != ESP_OK) {
        return NULL;
    }
#if ESP_VOCAT_INVERT_COLOR
    if (esp_lcd_panel_invert_color(s_panel, true) != ESP_OK) {
        return NULL;
    }
#endif
    if (esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK ||
        esp_lcd_panel_swap_xy(s_panel, false) != ESP_OK ||
        esp_lcd_panel_mirror(s_panel, false, false) != ESP_OK) {
        return NULL;
    }

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = io,
        .profile =
            {
                .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
                .rotation = ESP_LV_ADAPTER_ROTATE_0,
                .hor_res = LCD_RES,
                .ver_res = LCD_RES,
                .buffer_height = DRAW_BUF_LINES,
                .use_psram = false,
                .require_double_buffer = true,
            },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        return NULL;
    }
    *touch = lv_indev_create();
    if (!*touch) {
        return NULL;
    }
    lv_indev_set_type(*touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(*touch, disp);
    lv_indev_set_read_cb(*touch, touch_read_cb);
    if (esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep); /* SLPIN/SLPOUT，GRAM 保留 */
}

/* ---- 触摸 / 按键 / 电源 ---- */
/* reg 0x02 六字节：count, X high/low, Y high/low, 尾字节。
 * 一次失败只当作「没按下」，绝不合成 press/release 边沿。 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static uint16_t x, y;
    uint8_t reg = 2, raw[6];
    const bool pressed = i2c_master_transmit_receive(s_touch, &reg, 1, raw, sizeof(raw), 20) == ESP_OK &&
                         esp_vocat_touch_decode(raw, &x, &y);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
}

static unsigned poll_buttons(void) { return muse_gpio_button_poll(&s_boot); }

static esp_err_t read_power(muse_power_t *out)
{
    if (!s_gauge) {
        return ESP_ERR_NOT_FOUND;
    }
    uint16_t mv, status, current, average, soc;
    if (gauge_read(GAUGE_SOC, &soc) != ESP_OK ||
        gauge_read(GAUGE_BATTERY_STATUS, &status) != ESP_OK ||
        gauge_read(GAUGE_AVG_CURRENT, &average) != ESP_OK ||
        gauge_read(GAUGE_CURRENT, &current) != ESP_OK ||
        gauge_read(GAUGE_VOLTAGE, &mv) != ESP_OK || soc > 100) {
        return ESP_FAIL;
    }
    const bool discharging = (status & 0x01) != 0; /* BQ DSG bit */
    out->battery_mv = mv;
    out->battery_pct = (int)soc;
    /* 官方 Charge::Update()：电流作兜底，因为 AverageCurrent 在插入充电器时会滞后。 */
    out->charging = !discharging || (int16_t)average > CHARGING_MA || (int16_t)current > CHARGING_MA;
    out->usb = out->charging || usb_serial_jtag_is_connected();
    return ESP_OK;
}

/* ---- 音频：方案 A，对齐 Muse 固定 16 kHz / 16-bit / STD 双槽 ----
 * 官方 BoxAudioCodec 用 24 kHz TDM 4 槽（含 AEC 参考），Muse 共享层全库无 i2s_tdm。
 * 这里让硬件迁就框架：ES7210 只选 MIC1|MIC2，mic_slot=-1 让共享层混合两路。 */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c, 0x18, 100), TAG, "ES8311 probe (7-bit 0x18)");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c, 0x40, 100), TAG, "ES7210 probe (7-bit 0x40)");

    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx, &rx), TAG, "I2S channels");
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_MCLK_GPIO,
                     .bclk = I2S_BCLK_GPIO,
                     .ws = I2S_WS_GPIO,
                     .dout = I2S_DOUT_GPIO,
                     .din = s_i2s_din},
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "I2S TX enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "I2S RX enable");

    audio_codec_i2s_cfg_t data_cfg = {.port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    /* audio_codec_i2c_cfg_t.addr 用 8-bit 地址：0x18<<1=0x30, 0x40<<1=0x80。
     * probe 用的是 7-bit，写错会表现为「probe 成功但 codec 初始化失败」。 */
    audio_codec_i2c_cfg_t ctrl_cfg = {.port = I2C_NUM_0, .addr = 0x30, .bus_handle = s_i2c};
    const audio_codec_ctrl_if_t *dac_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    ctrl_cfg.addr = 0x80;
    const audio_codec_ctrl_if_t *adc_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data && dac_ctrl && adc_ctrl && gpio, ESP_ERR_NO_MEM, TAG, "codec interfaces");

    es8311_codec_cfg_t dac = {
        .ctrl_if = dac_ctrl,
        .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = s_pa_pin, /* NS4150B，V1.0=GPIO4 / V1.2=GPIO15 */
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    es7210_codec_cfg_t adc = {
        .ctrl_if = adc_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *out = es8311_codec_new(&dac), *in = es7210_codec_new(&adc);
    ESP_RETURN_ON_FALSE(out && in, ESP_FAIL, TAG, "codec initialization");

    esp_codec_dev_cfg_t out_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = out, .data_if = data};
    esp_codec_dev_cfg_t in_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = in, .data_if = data};
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    ESP_RETURN_ON_FALSE(*spk && *mic, ESP_FAIL, TAG, "codec handles");
    ESP_LOGI(TAG, "ES8311/ES7210 ready at %d Hz STD stereo (16-bit)", MUSE_AUDIO_RATE);
    return ESP_OK;
}

static const muse_board_t s_board = {
    .name = "Espressif ESP-VoCat",
    .width = 360,
    .height = 360,
    .round = true,
    .touch = true,
    .touch_talk = true,
    .diagonal_in = 1.85f,
    .talk_button = "screen microphone",
    .frame_ms = 40,
    .talk_hint = {LV_ALIGN_BOTTOM_MID, 0, -40},
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = -1, /* 混合 ES7210 两路麦；Muse 共享层会施加 mic gain */
    .poll_buttons = poll_buttons,
    .read_power = read_power,
    /* .codec_power 留 NULL：GPIO48 是版本探测脚，不做周期性断电。
     * .audio_power 留 NULL：PA 由 es8311 驱动的 pa_pin 控制。
     * .power_off 留 NULL：官方用 SAM8108 物理开关，没有软件关机通路。 */
};

const muse_board_t *muse_board_get(void) { return &s_board; }
