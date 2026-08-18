#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

/* Hardware includes */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* Bluetooth includes */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/* Storage includes (NVS) */
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/settings/settings.h>

/* ДОДАНО: Бібліотека для ШИМ (PWM) */
#include <zephyr/drivers/pwm.h>
#include <zephyr/devicetree/pwms.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

/* ДОДАНО: Бібліотека для Battary manegement */
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/services/bas.h>

/* Event system */
#include "app_events.h"
#include "app_sm.h"
#include "hog.h"

#define DEBUG_ENCODER_LOG 0
#define DEBUG_INACTIVITY_LOG 0
#define ENCODER_WHEEL_SEND_RELEASE 1
/* Set to 0 temporarily to isolate matrix wake leakage in SYSTEM OFF. */
#define SYSTEM_OFF_WAKE_ON_MATRIX 1

/* ========================================================================== *
 * 0. ГЛОБАЛЬНІ ЗМІННІ ТА НАЛАШТУВАННЯ АПАРАТНОЇ ЧАСТИНИ
 * ========================================================================== */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec cols[] = {
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, col0_gpios),
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, col1_gpios),
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, col2_gpios),
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, col3_gpios),
};
static const struct gpio_dt_spec rows[] = {
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, row0_gpios),
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, row1_gpios),
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, row2_gpios),
};
static const struct device *const qdec_dev = DEVICE_DT_GET(DT_NODELABEL(qdec));
static const struct gpio_dt_spec enc_btn = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_btn_gpios);
static const struct adc_dt_spec battery_adc = ADC_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, 0);

static int16_t battery_adc_raw;

#define NUM_COLS ARRAY_SIZE(cols)
#define NUM_ROWS ARRAY_SIZE(rows)
#define DEBOUNCE_MS 20 
#define BATTERY_VOLTAGE_MULTIPLIER 2
#define QDEC_A_PIN NRF_GPIO_PIN_MAP(0, 9)
#define QDEC_B_PIN NRF_GPIO_PIN_MAP(0, 10)
#define BATTERY_ADC_PIN NRF_GPIO_PIN_MAP(0, 31)

#define BATTERY_UPDATE_INTERVAL_MS (60U * 1000U)

static uint32_t last_battery_update_time = 0;

/* Стан кнопок */
static bool key_state[NUM_ROWS][NUM_COLS] = {false};
static uint32_t last_time[NUM_ROWS][NUM_COLS] = {0};
static bool enc_btn_state = false;
static uint32_t enc_btn_last_time = 0; 
static bool combo_unpair_done = false;
static bool power_save_active = false;
static bool input_scan_enabled = true;
static bool backlight_was_on_before_sleep = true;
static uint32_t last_activity_time = 0;
/* Default sleep timeout in minutes (integer minutes). Use 10 minutes by default. */
static uint8_t sleep_timeout_minutes = 1;

/* ДИНАМІЧНА РОЗКЛАДКА (Без const, щоб можна було перезаписувати) */
/* Типи дій, які може виконувати кнопка або енкодер */
#define ACTION_TYPE_NONE     0
#define ACTION_TYPE_KEY      1  /* Звичайна клавіатура (з модифікаторами Ctrl, Shift) */
#define ACTION_TYPE_MOUSE    2  /* Миша (кліки або скрол) */
#define ACTION_TYPE_MEDIA    3  /* Медіа (Гучність, Play/Pause) - на майбутнє */
#define ACTION_TYPE_LIGHT    4  /* ДОДАНО: Керування підсвіткою (код 1 - яскравіше, код 2 - тьмяніше) */
#define ACTION_TYPE_SEQUENCE 5  /* Виконати збережену послідовність */

/* Структура однієї дії (3 байти) */
typedef struct __packed{
    uint8_t type; /* Тип дії (ACTION_TYPE_KEY, ACTION_TYPE_MOUSE, тощо) */
    uint8_t mod;  /* Модифікатори (наприклад, 0x01 для Ctrl). Для миші - нуль. */
    uint8_t code; /* Скан-код клавіші (наприклад, 0x04 для 'A') або дельта скролу */
} Action_t;

#define PROFILE_NAME_LEN 16
#define MAX_PROFILES 3 /* Для початку зробимо 3 профілі, щоб зекономити RAM */

/* Структура цілого профілю */
typedef struct __packed{
    char name[PROFILE_NAME_LEN];              /* Назва профілю ("Photoshop", "Coding") */
    Action_t matrix[NUM_ROWS][NUM_COLS];      /* Дії для 12 кнопок (3x4) */
    Action_t enc_btn;                         /* Дія для натискання енкодера */
    Action_t enc_cw;                          /* Дія для повороту за годинниковою (Clockwise) */
    Action_t enc_ccw;                         /* Дія для повороту проти годинникової (Counter-CW) */
} Profile_t;

static void handle_local_action(Action_t act);
static void execute_sequence(uint8_t idx);

/* ДОДАНО: Керування підсвіткою */
static const struct pwm_dt_spec backlight = PWM_DT_SPEC_GET(DT_NODELABEL(backlight_led));
static const struct gpio_dt_spec backlight_gpio = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, backlight_gpios);
static uint8_t current_brightness = 50; /* За замовчуванням 50% */

static bool backlight_on = true; /* Поточний стан підсвітки */

static void battery_init(void)
{
    if (!adc_is_ready_dt(&battery_adc)) {
        printk("Battery: ADC device not ready\n");
        return;
    }

    int err = adc_channel_setup_dt(&battery_adc);
    if (err) {
        printk("Battery: ADC channel setup failed, err %d\n", err);
        return;
    }

    printk("Battery: ADC initialized on AIN7 / P0.31\n");
}

static uint8_t battery_percent_from_mv(int32_t mv)
{
    if (mv >= 4200) return 100;
    if (mv >= 4100) return 90;
    if (mv >= 4000) return 80;
    if (mv >= 3900) return 70;
    if (mv >= 3800) return 60;
    if (mv >= 3700) return 50;
    if (mv >= 3600) return 35;
    if (mv >= 3500) return 20;
    if (mv >= 3400) return 10;
    if (mv >= 3300) return 5;
    return 0;
}

static int battery_read_mv(void)
{
    struct adc_sequence sequence = {0};
    int32_t mv;
    int err;

    if (!adc_is_ready_dt(&battery_adc)) {
        return -ENODEV;
    }

    err = adc_sequence_init_dt(&battery_adc, &sequence);
    if (err) {
        return err;
    }

    sequence.buffer = &battery_adc_raw;
    sequence.buffer_size = sizeof(battery_adc_raw);

    err = adc_read(battery_adc.dev, &sequence);
    if (err) {
        return err;
    }

    mv = battery_adc_raw;

    err = adc_raw_to_millivolts_dt(&battery_adc, &mv);
    if (err) {
        return err;
    }

    return mv * BATTERY_VOLTAGE_MULTIPLIER;
}

static void battery_update_ble(void)
{
    int mv = battery_read_mv();

    if (mv < 0) {
        printk("Battery: read failed, err %d\n", mv);
        return;
    }
     printk("Battery debug: raw=%d, mv=%d\n", battery_adc_raw, mv);
    uint8_t percent = battery_percent_from_mv(mv);

    bt_bas_set_battery_level(percent);

    if (percent <= 20) {
        printk("Battery: LOW %d mV, %u%%\n", mv, percent);
    } else {
        printk("Battery: %d mV, %u%%\n", mv, percent);
    }
    printk("Battery: %d mV, %u%% - sent to BLE BAS\n", mv, percent);
}

static void battery_update_periodic(void)
{
    uint32_t now = k_uptime_get_32();

    if ((now - last_battery_update_time) >= BATTERY_UPDATE_INTERVAL_MS) {
        last_battery_update_time = now;
        battery_update_ble();
    }
}


/* Оновлена функція встановлення яскравості */
static void set_backlight(uint8_t percent, bool on) {
    if (!pwm_is_ready_dt(&backlight)) {
        return;
    }

    current_brightness = (percent > 100) ? 100 : percent;
    backlight_on = on;

    uint32_t pulse = 0;
    if (backlight_on) {
        pulse = (backlight.period * current_brightness) / 100;
    }

    int err = pwm_set_pulse_dt(&backlight, pulse);
    if (!backlight_on && gpio_is_ready_dt(&backlight_gpio)) {
        gpio_pin_configure_dt(&backlight_gpio, GPIO_OUTPUT_INACTIVE);
    }
    
    if (err && !backlight_on && gpio_is_ready_dt(&backlight_gpio)) {
        printk("Backlight: PWM off failed, forcing GPIO low\n");
    }
}

/* ========================================================================== *
 * INACTIVITY TIMER (k_work_delayable)
 * ========================================================================== */

/* Must define sleep_timeout_ms before using it */
#define NVS_SLEEP_MIN_ID 106
static struct nvs_fs fs;

static inline uint32_t sleep_timeout_ms(void) {
    return (uint32_t)sleep_timeout_minutes * 60U * 1000U;
}

static void inactivity_timeout_handler(struct k_work *work)
{
    printk("Inactivity timeout fired\n");
    app_post_simple(APP_EVT_INACTIVITY_TIMEOUT);
}

static K_WORK_DELAYABLE_DEFINE(inactivity_work, inactivity_timeout_handler);

void inactivity_timer_init(void)
{
    /* Initialize work, do not schedule yet */
    printk("Inactivity timer initialized\n");
}

void inactivity_timer_reset(void)
{
    if (sleep_timeout_minutes == 0) {
        /* Sleep disabled */
        k_work_cancel_delayable(&inactivity_work);
        return;
    }

    uint32_t timeout_ms = sleep_timeout_ms();
    k_work_reschedule_for_queue(&k_sys_work_q, &inactivity_work, K_MSEC(timeout_ms));
#if DEBUG_INACTIVITY_LOG
    printk("Inactivity timer reset: %u ms\n", timeout_ms);
#endif
}

void inactivity_timer_stop(void)
{
    k_work_cancel_delayable(&inactivity_work);
}

static void power_force_app_peripherals_off(void);
static void prepare_wakeup_pins(void);

/* Forward declarations for power management functions */
void power_prepare_system_off(void);
void power_enter_system_off(void);

static void set_sleep_timeout_minutes(uint8_t minutes) {
    if (minutes > 120) {
        minutes = 120;
    }
    sleep_timeout_minutes = minutes;
    nvs_write(&fs, NVS_SLEEP_MIN_ID, &sleep_timeout_minutes, sizeof(sleep_timeout_minutes));
    printk("Power save: timeout set to %u min\n", sleep_timeout_minutes);
}

/* Weak symbol to allow linking even if main.c doesn't define it */
extern uint8_t active_connections __attribute__((weak));

static uint32_t nrf_pin_from_dt(const struct gpio_dt_spec *spec)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
    static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

    if (spec->port == gpio1_dev) {
        return NRF_GPIO_PIN_MAP(1, spec->pin);
    }
#endif

    return NRF_GPIO_PIN_MAP(0, spec->pin);
}

static void force_backlight_off_for_sleep(void)
{
    backlight_on = false;

    if (pwm_is_ready_dt(&backlight)) {
        pwm_set_pulse_dt(&backlight, 0);
    }

    if (gpio_is_ready_dt(&backlight_gpio)) {
        gpio_pin_configure_dt(&backlight_gpio, GPIO_OUTPUT_INACTIVE);
        gpio_pin_set_dt(&backlight_gpio, 0);
    }

    printk("Power save: LED forced OFF before sleep\n");
}

static void power_force_app_peripherals_off(void)
{
    printk("Power save: forcing app peripherals OFF\n");

    force_backlight_off_for_sleep();

#ifdef NRF_PWM0
    NRF_PWM0->TASKS_STOP = 1;
    NRF_PWM0->ENABLE = 0;
#endif

#ifdef NRF_RADIO
    NRF_RADIO->TASKS_DISABLE = 1;
#endif

#ifdef NRF_QDEC
    NRF_QDEC->TASKS_STOP = 1;
    NRF_QDEC->ENABLE = 0;
#endif

#ifdef NRF_SAADC
    NRF_SAADC->TASKS_STOP = 1;
    NRF_SAADC->ENABLE = 0;
#endif

#ifdef NRF_GPIOTE
    NRF_GPIOTE->INTENCLR = 0xFFFFFFFF;
    for (uint8_t i = 0; i < 8; i++) {
        NRF_GPIOTE->CONFIG[i] = 0;
        NRF_GPIOTE->EVENTS_IN[i] = 0;
    }
    NRF_GPIOTE->EVENTS_PORT = 0;
#endif

    nrf_gpio_cfg_default(QDEC_A_PIN);
    nrf_gpio_cfg_default(QDEC_B_PIN);
    nrf_gpio_cfg_default(BATTERY_ADC_PIN);

    if (gpio_is_ready_dt(&backlight_gpio)) {
        uint32_t pin = nrf_pin_from_dt(&backlight_gpio);

        nrf_gpio_cfg_output(pin);
        nrf_gpio_pin_clear(pin);
    }

    printk("Power save: PWM/QDEC/SAADC stopped, non-wakeup pins disconnected\n");
}

/* Legacy function: power save mode entry (now delegated to app_sm) */
__attribute__((unused))
static void enter_power_save_mode(void) {
    if (power_save_active || sleep_timeout_minutes == 0) {
        return;
    }

    printk("Power save: timeout reached after %u min inactivity\n", sleep_timeout_minutes);

    app_post_simple(APP_EVT_PREPARE_SLEEP);
}

void power_prepare_system_off(void)
{
    backlight_was_on_before_sleep = backlight_on;

    printk("Power: Preparing for system off...\n");

    power_force_app_peripherals_off();

    /* Prepare pins for wakeup from system-off */
    prepare_wakeup_pins();

    printk("Power: Going to SYSTEM OFF now\n");
}

void power_enter_system_off(void)
{
    printk("Power: calling nrf_power_system_off()\n");
    nrf_power_system_off(NRF_POWER);

    /* Should never return. If it does, reboot to avoid repeated broken sleep loop */
    printk("Power ERROR: nrf_power_system_off() returned\n");
    k_msleep(100);
    sys_reboot(SYS_REBOOT_COLD);
}

/* Public input state query helpers for app_sm combo detection */

uint8_t input_keyboard_pressed_count(void) {
    uint8_t count = 0;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (key_state[r][c]) {
                count++;
            }
        }
    }
    return count;
}

bool input_encoder_button_is_pressed(void) {
    return enc_btn_state;
}

bool input_combo_unpair_condition(void) {
    return enc_btn_state && input_keyboard_pressed_count() >= 2;
}

bool input_combo_unpair_already_done(void) {
    return combo_unpair_done;
}

void input_combo_unpair_set_done(bool done) {
    combo_unpair_done = done;
}

void input_scan_set_enabled(bool enabled)
{
    input_scan_enabled = enabled;
}

/* --- Наші глобальні змінні для профілів --- */
static Profile_t profiles[MAX_PROFILES];      /* Масив усіх профілів у RAM (183 байти) */
static uint8_t active_profile_index = 0;      /* Індекс поточного профілю */

/* Цей макрос дозволить старому коду працювати без змін: 
   всюди, де написано current_profile, компілятор підставить profiles[active_profile_index] */
#define current_profile (profiles[active_profile_index])
/* ========================================================================== *
 * 1. МОДУЛЬ ПАМ'ЯТІ (NVS STORAGE)
 * Відповідає за збереження налаштувань між перезавантаженнями
 * ========================================================================== */
#define NVS_PARTITION		app_storage_partition
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_KEYMAP_ID 1

#define NVS_ACTIVE_PROF_ID 100
#define NVS_PROFILE_BASE_ID 300
#define NVS_LED_STATE_ID 105 /* Новий ID для збереження стану (ON/OFF) */
/* NVS base for sequences */
#define NVS_SEQUENCE_BASE_ID 400

/* ===== Sequences storage and types ===== */
#define MAX_SEQUENCES 8
#define SEQUENCE_NAME_LEN 16
#define SEQUENCE_MAX_STEPS 32

typedef struct __packed {
    uint8_t mod;
    uint8_t code;
    uint16_t delay_ms;
} SequenceStep_t;

typedef struct __packed {
    char name[SEQUENCE_NAME_LEN];
    uint8_t len;
    SequenceStep_t steps[SEQUENCE_MAX_STEPS];
} KeySequence_t; /* 16 + 1 + 32*4 = 145 bytes */

static KeySequence_t sequences[MAX_SEQUENCES];

static void load_all_sequences(void) {
    for (uint8_t i = 0; i < MAX_SEQUENCES; i++) {
        int rc = nvs_read(&fs, NVS_SEQUENCE_BASE_ID + i, &sequences[i], sizeof(KeySequence_t));
        if (rc == sizeof(KeySequence_t)) {
            printk("Storage: Sequence %d loaded\n", i);
        } else {
            printk("Storage: Sequence %d not found, creating default\n", i);
            memset(&sequences[i], 0, sizeof(KeySequence_t));
            snprintf(sequences[i].name, SEQUENCE_NAME_LEN, "Seq %d", i + 1);
            sequences[i].len = 0;
            nvs_write(&fs, NVS_SEQUENCE_BASE_ID + i, &sequences[i], sizeof(KeySequence_t));
        }
    }
}

static void load_all_profiles(void) {
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        /* Читаємо. rc - це кількість прочитаних байтів */
        int rc = nvs_read(&fs, NVS_PROFILE_BASE_ID + i, &profiles[i], sizeof(Profile_t));
        
        /* Профіль вважається дійсним, тільки якщо прочитано рівно 61 байт */
        if (rc == sizeof(Profile_t)) {
            printk("Storage: Profile %d loaded from Flash successfully!\n", i);
        } else {
            printk("Storage: Profile %d not found or invalid (rc=%d). Creating default...\n", i, rc);
            memset(&profiles[i], 0, sizeof(Profile_t));
            snprintf(profiles[i].name, PROFILE_NAME_LEN, "Profile %d", i + 1);
            
            profiles[i].matrix[0][0].type = ACTION_TYPE_KEY;
            profiles[i].matrix[0][0].code = 0x04 + i;
            
            profiles[i].enc_cw.type = ACTION_TYPE_MOUSE;
            profiles[i].enc_cw.code = 1; 
            profiles[i].enc_ccw.type = ACTION_TYPE_MOUSE;
            profiles[i].enc_ccw.code = (uint8_t)-1; 
            
            nvs_write(&fs, NVS_PROFILE_BASE_ID + i, &profiles[i], sizeof(Profile_t));
        }
    }
}

static void storage_init(void) {
    int rc;
    struct flash_pages_info info;

    fs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(fs.flash_device)) {
        printk("Storage Error: Flash device not ready\n");
        return;
    }

    fs.offset = NVS_PARTITION_OFFSET;
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) { return; }
    
    fs.sector_size = info.size;
    fs.sector_count = 3U;

    rc = nvs_mount(&fs);
    if (rc) {
        printk("Storage Error: Error mounting NVS\n");
        return;
    }
    /* 1. Читаємо індекс активного профілю */
    rc = nvs_read(&fs, NVS_ACTIVE_PROF_ID, &active_profile_index, sizeof(active_profile_index));
    if (rc <= 0 || active_profile_index >= MAX_PROFILES) {
        active_profile_index = 0; 
        nvs_write(&fs, NVS_ACTIVE_PROF_ID, &active_profile_index, sizeof(active_profile_index));
    }
    
    /* 2. Завантажуємо ВСІ профілі в масив у RAM */
    load_all_profiles();
    /* Завантажуємо всі послідовності */
    load_all_sequences();
    printk("Storage: Loaded all %d profiles. Active: %d\n", MAX_PROFILES, active_profile_index);

    uint8_t saved_sleep_min;
    rc = nvs_read(&fs, NVS_SLEEP_MIN_ID, &saved_sleep_min, sizeof(saved_sleep_min));
    if (rc > 0 && saved_sleep_min <= 120) {
        sleep_timeout_minutes = saved_sleep_min;
    }

    /* У функції storage_init */
    uint8_t saved_led_state;
    rc = nvs_read(&fs, NVS_LED_STATE_ID, &saved_led_state, sizeof(saved_led_state));
    if (rc > 0) {
        backlight_on = (bool)saved_led_state;
    }
    set_backlight(current_brightness, backlight_on);

    last_activity_time = k_uptime_get_32();
}


/* ========================================================================== *
 * 2. МОДУЛЬ BLUETOOTH HID (КЛАВІАТУРА ТА МИША)
 * Відповідає за передачу натискань в операційну систему
 * ========================================================================== */
enum { HIDS_REMOTE_WAKE = BIT(0), HIDS_NORMALLY_CONNECTABLE = BIT(1) };
struct hids_info { uint16_t version; uint8_t code; uint8_t flags; } __packed;
struct hids_report { uint8_t id; uint8_t type; } __packed;

static struct hids_info info = { .version = 0x0111, .code = 0x00, .flags = HIDS_NORMALLY_CONNECTABLE | HIDS_REMOTE_WAKE };
static struct hids_report input_kbd = { .id = 0x01, .type = 0x01 };
static struct hids_report input_mouse = { .id = 0x02, .type = 0x01 };
static uint8_t ctrl_point;

static uint8_t report_map[] = {
    /* КЛАВІАТУРА (Report ID 1) */
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    /* МИШКА (Report ID 2) */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0
};

static ssize_t read_info(struct bt_conn *c, const struct bt_gatt_attr *a, void *b, uint16_t l, uint16_t o) { return bt_gatt_attr_read(c, a, b, l, o, a->user_data, sizeof(struct hids_info)); }
static ssize_t read_report_map(struct bt_conn *c, const struct bt_gatt_attr *a, void *b, uint16_t l, uint16_t o) { return bt_gatt_attr_read(c, a, b, l, o, report_map, sizeof(report_map)); }
static ssize_t read_report(struct bt_conn *c, const struct bt_gatt_attr *a, void *b, uint16_t l, uint16_t o) { return bt_gatt_attr_read(c, a, b, l, o, a->user_data, sizeof(struct hids_report)); }
static void input_ccc_changed(const struct bt_gatt_attr *a, uint16_t v) {}
static ssize_t read_input_report(struct bt_conn *c, const struct bt_gatt_attr *a, void *b, uint16_t l, uint16_t o) { return bt_gatt_attr_read(c, a, b, l, o, NULL, 0); }
static ssize_t write_ctrl_point(struct bt_conn *c, const struct bt_gatt_attr *a, const void *b, uint16_t l, uint16_t o, uint8_t f) { return l; }

BT_GATT_SERVICE_DEFINE(hog_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info, NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_report_map, NULL, NULL),
    
    /* Attribute Index 6: Keyboard Report */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report, NULL, &input_kbd),
    
    /* Attribute Index 10: Mouse Report */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report, NULL, &input_mouse),
    
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point)
);

/* Універсальна функція для відправки і клавіатури, і миші */
static void ble_send_hid_reports(void) {
    uint8_t kbd_report[8] = {0};
    int kbd_idx = 2; 
    
    uint8_t mouse_rep[4] = {0, 0, 0, 0}; /* [0]=Кнопки, [1]=X, [2]=Y, [3]=Скрол */

    /* Перевіряємо матрицю 3x4 */
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (key_state[r][c]) {
                Action_t act = current_profile.matrix[r][c];
                
                if (act.type == ACTION_TYPE_KEY) {
                    kbd_report[0] |= act.mod; 
                    if (kbd_idx < 8 && act.code != 0) kbd_report[kbd_idx++] = act.code; 
                }
                else if (act.type == ACTION_TYPE_MOUSE) {
                    /* Розшифровуємо ваші Flutter-коди у стандартні HID-біти */
                    if (act.code == 1) mouse_rep[0] |= BIT(0); /* Лівий клік */
                    if (act.code == 2) mouse_rep[0] |= BIT(1); /* Правий клік */
                    if (act.code == 3) mouse_rep[0] |= BIT(2); /* Середній клік */
                    if (act.code == 4) mouse_rep[3] = 1;       /* Скрол Вгору */
                    if (act.code == 5) mouse_rep[3] = (uint8_t)-1; /* Скрол Вниз */
                }
            }
        }
    }
    
    /* Перевіряємо кнопку енкодера */
    if (enc_btn_state) {
        Action_t act = current_profile.enc_btn;
        if (act.type == ACTION_TYPE_KEY) {
            kbd_report[0] |= act.mod;
            if (kbd_idx < 8 && act.code != 0) kbd_report[kbd_idx++] = act.code; 
        }
        else if (act.type == ACTION_TYPE_MOUSE) {
            if (act.code == 1) mouse_rep[0] |= BIT(0);
            if (act.code == 2) mouse_rep[0] |= BIT(1);
            if (act.code == 3) mouse_rep[0] |= BIT(2);
            if (act.code == 4) mouse_rep[3] = 1;
            if (act.code == 5) mouse_rep[3] = (uint8_t)-1;
        }
    }

    /* Відправляємо обидва звіти в ОС. 
       Коли ви відпустите кнопку, масиви будуть нульовими, 
       і ОС зрозуміє, що клік завершився. */
    bt_gatt_notify(NULL, &hog_svc.attrs[6], kbd_report, sizeof(kbd_report));
    bt_gatt_notify(NULL, &hog_svc.attrs[10], mouse_rep, sizeof(mouse_rep));
}

/* External BLE helper used for HID guard */
extern bool ble_app_can_send_hid(void);

/* HID send function called from app_sm */
void hid_send_current_input_state(void)
{
    if (!ble_app_can_send_hid()) {
        return;
    }

    ble_send_hid_reports();
}

static void input_send_encoder_mouse_wheel(Action_t act, bool clockwise, int steps)
{
    (void)clockwise;

    if (!ble_app_can_send_hid()) {
        return;
    }

    int wheel = 0;

    if (act.code == 4 || act.code == 1) {
        wheel = steps;
    } else if (act.code == 5 || act.code == (uint8_t)-1) {
        wheel = -steps;
    } else {
        return;
    }

    if (wheel > 7) {
        wheel = 7;
    } else if (wheel < -7) {
        wheel = -7;
    }

#if DEBUG_ENCODER_LOG
    printk("HID: encoder wheel=%d\n", wheel);
#endif

    uint8_t report[4] = {0, 0, 0, (uint8_t)((int8_t)wheel)};

    int err = bt_gatt_notify(NULL, &hog_svc.attrs[10], report, sizeof(report));
    if (err) {
        printk("HID ERROR: encoder wheel notify failed err=%d\n", err);
    }

#if ENCODER_WHEEL_SEND_RELEASE
    k_busy_wait(10);

    uint8_t release[4] = {0};
    err = bt_gatt_notify(NULL, &hog_svc.attrs[10], release, sizeof(release));
    if (err) {
        printk("HID ERROR: encoder wheel release notify failed err=%d\n", err);
    }
#endif
}

static void input_send_encoder_key_pulse(Action_t act)
{
    if (!ble_app_can_send_hid() || act.code == 0) {
        return;
    }

    uint8_t report[8] = {0};
    report[0] = act.mod;
    report[2] = act.code;

    int err = bt_gatt_notify(NULL, &hog_svc.attrs[6], report, sizeof(report));
    if (err) {
        printk("HID ERROR: encoder key press notify failed err=%d\n", err);
        return;
    }

    k_busy_wait(10);

    uint8_t release[8] = {0};
    err = bt_gatt_notify(NULL, &hog_svc.attrs[6], release, sizeof(release));
    if (err) {
        printk("HID ERROR: encoder key release notify failed err=%d\n", err);
    }
}

void input_execute_encoder_action(bool clockwise)
{
    Action_t act = clockwise ? current_profile.enc_cw : current_profile.enc_ccw;

#if DEBUG_ENCODER_LOG
    printk("HID: encoder %s action type=%u mod=%u code=%u\n",
           clockwise ? "CW" : "CCW", act.type, act.mod, act.code);
#endif

    if (act.type == ACTION_TYPE_MOUSE) {
        input_send_encoder_mouse_wheel(act, clockwise, 1);
        return;
    }

    if (act.type == ACTION_TYPE_KEY) {
        input_send_encoder_key_pulse(act);
        return;
    }

    if (act.type == ACTION_TYPE_SEQUENCE) {
        input_execute_sequence(act.code);
        return;
    }

    if (act.type == ACTION_TYPE_LIGHT) {
        handle_local_action(act);
        return;
    }
}

void input_execute_encoder_delta(int delta)
{
    if (delta == 0) {
        return;
    }

    bool clockwise = delta > 0;
    int steps = clockwise ? delta : -delta;
    Action_t act = clockwise ? current_profile.enc_cw : current_profile.enc_ccw;

#if DEBUG_ENCODER_LOG
    printk("HID: encoder delta=%d action type=%u mod=%u code=%u\n",
           delta, act.type, act.mod, act.code);
#endif

    switch (act.type) {
    case ACTION_TYPE_MOUSE:
        input_send_encoder_mouse_wheel(act, clockwise, steps);
        break;

    case ACTION_TYPE_KEY:
        if (steps > 4) {
            steps = 4;
        }

        for (int i = 0; i < steps; i++) {
            input_send_encoder_key_pulse(act);
        }
        break;

    case ACTION_TYPE_LIGHT:
        if (steps > 8) {
            steps = 8;
        }

        for (int i = 0; i < steps; i++) {
            handle_local_action(act);
        }
        break;

    case ACTION_TYPE_SEQUENCE:
        handle_local_action(act);
        break;

    case ACTION_TYPE_NONE:
    default:
        break;
    }
}

void input_handle_key_pressed(uint8_t row, uint8_t col)
{
    if (row >= NUM_ROWS || col >= NUM_COLS) {
        return;
    }

    handle_local_action(current_profile.matrix[row][col]);
}

void input_apply_profile_change(uint8_t profile_index)
{
    if (profile_index >= MAX_PROFILES) {
        return;
    }

    active_profile_index = profile_index;
    nvs_write(&fs, NVS_ACTIVE_PROF_ID, &active_profile_index, sizeof(active_profile_index));
    printk("BLE: Switched to profile %d\n", active_profile_index);
}

void input_apply_backlight_command(uint8_t value)
{
    if (value == 0) {
        set_backlight(current_brightness, false);
    } else {
        set_backlight(value, true);
    }

    uint8_t state_to_save = (value > 0) ? 1 : 0;
    nvs_write(&fs, NVS_LED_STATE_ID, &state_to_save, sizeof(state_to_save));
}

void input_apply_sleep_timeout(uint8_t minutes)
{
    set_sleep_timeout_minutes(minutes);
}

void input_execute_sequence(uint8_t sequence_index)
{
    if (sequence_index < MAX_SEQUENCES) {
        printk("Local: Execute sequence %d\n", sequence_index);
        execute_sequence(sequence_index);
    }
}

void input_handle_encoder_button_event(enum app_event_type type)
{
    bool pressed = (type == APP_EVT_ENCODER_BUTTON_PRESSED);

    if (pressed) {
        Action_t act = current_profile.enc_btn;

        if (act.type == ACTION_TYPE_LIGHT ||
            act.type == ACTION_TYPE_SEQUENCE) {
            handle_local_action(act);
        }
    }

    hid_send_current_input_state();
}

/* ========================================================================== *
 * 3. МОДУЛЬ КАСТОМНОГО СЕРВІСУ (ДЛЯ FLUTTER)
 * ========================================================================== */
#define MACROPAD_CONFIG_SERVICE_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define MACROPAD_CHAR_INFO_UUID      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)
#define MACROPAD_CHAR_DUMP_UUID      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)
#define MACROPAD_CHAR_CTRL_UUID      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3)
#define MACROPAD_CHAR_SEQUENCES_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4)
#define MACROPAD_CHAR_EXPORT_UUID    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef5)

static struct bt_uuid_128 config_svc_uuid = BT_UUID_INIT_128(MACROPAD_CONFIG_SERVICE_UUID);
static struct bt_uuid_128 info_char_uuid = BT_UUID_INIT_128(MACROPAD_CHAR_INFO_UUID);
static struct bt_uuid_128 dump_char_uuid = BT_UUID_INIT_128(MACROPAD_CHAR_DUMP_UUID);
static struct bt_uuid_128 ctrl_char_uuid = BT_UUID_INIT_128(MACROPAD_CHAR_CTRL_UUID);
static struct bt_uuid_128 sequences_char_uuid = BT_UUID_INIT_128(MACROPAD_CHAR_SEQUENCES_UUID);
static struct bt_uuid_128 export_char_uuid = BT_UUID_INIT_128(MACROPAD_CHAR_EXPORT_UUID);

/* 1. Читання Info (Додаємо туди active_profile_index) */
/* 1. Читання Info (Додаємо туди інфо про наявність підсвітки) */
static ssize_t ble_flutter_read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    char json_info[150];
    snprintf(json_info, sizeof(json_info), 
        "{\"ver\":\"1.0\",\"hw\":{\"rows\":3,\"cols\":4,\"enc\":1,\"led\":1},"
        "\"active_prof\":%d,\"led_on\":%d,\"led_val\":%d,\"sleep_min\":%u}", 
        active_profile_index, (int)backlight_on, current_brightness, sleep_timeout_minutes);
    
    return bt_gatt_attr_read(conn, attr, buf, len, offset, json_info, strlen(json_info));
}

/* 2. Читання ВСІХ профілів (183 байти) */
static ssize_t ble_flutter_read_dump(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    /* Віддаємо масив profiles цілком */
    return bt_gatt_attr_read(conn, attr, buf, len, offset, profiles, sizeof(profiles));
}

/* 3. Керування: Зміна профілю або Запис нових налаштувань */
static K_THREAD_STACK_DEFINE(seq_stack, 1024);
static struct k_thread seq_thread;

static void sequence_thread_fn(void *p1, void *p2, void *p3) {
    int idx = (int)(intptr_t)p1;
    if (idx < 0 || idx >= MAX_SEQUENCES) {
        return;
    }
    KeySequence_t *sq = &sequences[idx];
    for (uint8_t i = 0; i < sq->len; i++) {
        SequenceStep_t *s = &sq->steps[i];
        uint8_t report[8] = {0};
        report[0] = s->mod;
        report[2] = s->code;
        bt_gatt_notify(NULL, &hog_svc.attrs[6], report, sizeof(report));
        /* short press */
        k_msleep(10);
        uint8_t report_stop[8] = {0};
        bt_gatt_notify(NULL, &hog_svc.attrs[6], report_stop, sizeof(report_stop));
        if (s->delay_ms) {
            k_msleep(s->delay_ms);
        }
    }
}

static void execute_sequence(uint8_t idx) {
    k_thread_create(&seq_thread, seq_stack, K_THREAD_STACK_SIZEOF(seq_stack), sequence_thread_fn,
                    (void *)(intptr_t)idx, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
}

static ssize_t ble_flutter_read_sequences(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    /* Return raw binary sequences array */
    return bt_gatt_attr_read(conn, attr, buf, len, offset, sequences, sizeof(sequences));
}

/* Use a static export buffer (avoid large stack usage) */
static char export_buf[4096];

static ssize_t ble_flutter_read_export(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    /* Build JSON export: active_profile, profiles, sequences */
    size_t used = 0;
    int ret = 0;
    size_t rem = sizeof(export_buf) - used;

    ret = snprintf(export_buf + used, rem, "{\"active_profile\":%d,\"profiles\":[", active_profile_index);
    if (ret < 0 || (size_t)ret >= rem) return 0;
    used += (size_t)ret;

    for (uint8_t p = 0; p < MAX_PROFILES; p++) {
        rem = sizeof(export_buf) - used;
        if (p) {
            ret = snprintf(export_buf + used, rem, ",");
            if (ret < 0 || (size_t)ret >= rem) return 0;
            used += (size_t)ret;
        }
        rem = sizeof(export_buf) - used;
        ret = snprintf(export_buf + used, rem, "{\"name\":\"%s\",\"matrix\":[", profiles[p].name);
        if (ret < 0 || (size_t)ret >= rem) return 0;
        used += (size_t)ret;

        for (int r = 0; r < NUM_ROWS; r++) {
            rem = sizeof(export_buf) - used;
            if (r) {
                ret = snprintf(export_buf + used, rem, ",");
                if (ret < 0 || (size_t)ret >= rem) return 0;
                used += (size_t)ret;
            }
            rem = sizeof(export_buf) - used;
            ret = snprintf(export_buf + used, rem, "[");
            if (ret < 0 || (size_t)ret >= rem) return 0;
            used += (size_t)ret;

            for (int c = 0; c < NUM_COLS; c++) {
                rem = sizeof(export_buf) - used;
                if (c) {
                    ret = snprintf(export_buf + used, rem, ",");
                    if (ret < 0 || (size_t)ret >= rem) return 0;
                    used += (size_t)ret;
                }
                Action_t a = profiles[p].matrix[r][c];
                rem = sizeof(export_buf) - used;
                ret = snprintf(export_buf + used, rem, "[%u,%u,%u]", a.type, a.mod, a.code);
                if (ret < 0 || (size_t)ret >= rem) return 0;
                used += (size_t)ret;
            }
            rem = sizeof(export_buf) - used;
            ret = snprintf(export_buf + used, rem, "]");
            if (ret < 0 || (size_t)ret >= rem) return 0;
            used += (size_t)ret;
        }
        rem = sizeof(export_buf) - used;
        ret = snprintf(export_buf + used, rem, ",\"enc_btn\":[%u,%u,%u],\"enc_cw\":[%u,%u,%u],\"enc_ccw\":[%u,%u,%u]}",
                       profiles[p].enc_btn.type, profiles[p].enc_btn.mod, profiles[p].enc_btn.code,
                       profiles[p].enc_cw.type, profiles[p].enc_cw.mod, profiles[p].enc_cw.code,
                       profiles[p].enc_ccw.type, profiles[p].enc_ccw.mod, profiles[p].enc_ccw.code);
        if (ret < 0 || (size_t)ret >= rem) return 0;
        used += (size_t)ret;
    }

    rem = sizeof(export_buf) - used;
    ret = snprintf(export_buf + used, rem, "],\"sequences\":[");
    if (ret < 0 || (size_t)ret >= rem) return 0;
    used += (size_t)ret;

    for (uint8_t sidx = 0; sidx < MAX_SEQUENCES; sidx++) {
        rem = sizeof(export_buf) - used;
        if (sidx) {
            ret = snprintf(export_buf + used, rem, ",");
            if (ret < 0 || (size_t)ret >= rem) return 0;
            used += (size_t)ret;
        }
        KeySequence_t *sq = &sequences[sidx];
        rem = sizeof(export_buf) - used;
        ret = snprintf(export_buf + used, rem, "{\"name\":\"%s\",\"len\":%u,\"steps\":[", sq->name, sq->len);
        if (ret < 0 || (size_t)ret >= rem) return 0;
        used += (size_t)ret;

        for (uint8_t si = 0; si < sq->len; si++) {
            rem = sizeof(export_buf) - used;
            if (si) {
                ret = snprintf(export_buf + used, rem, ",");
                if (ret < 0 || (size_t)ret >= rem) return 0;
                used += (size_t)ret;
            }
            SequenceStep_t *st = &sq->steps[si];
            rem = sizeof(export_buf) - used;
            ret = snprintf(export_buf + used, rem, "[%u,%u,%u]", st->mod, st->code, st->delay_ms);
            if (ret < 0 || (size_t)ret >= rem) return 0;
            used += (size_t)ret;
        }
        rem = sizeof(export_buf) - used;
        ret = snprintf(export_buf + used, rem, "]}");
        if (ret < 0 || (size_t)ret >= rem) return 0;
        used += (size_t)ret;
    }

    rem = sizeof(export_buf) - used;
    ret = snprintf(export_buf + used, rem, "]}");
    if (ret < 0 || (size_t)ret >= rem) return 0;
    used += (size_t)ret;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, export_buf, used);
}

static ssize_t ble_flutter_write_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    const uint8_t *data = buf;
    printk("BLE: Received write packet, length: %d bytes\n", len);

    if (len == 1) {
        if (data[0] < MAX_PROFILES) {
            app_post_u8(APP_EVT_PROFILE_CHANGED, data[0]);
        }
        return len;
    }

    /* Short commands (2 bytes) */
    if (len == 2) {
        if (data[0] == 0xAA) {
            app_post_u8(APP_EVT_BACKLIGHT_CHANGED, data[1]);
            return len;
        } else if (data[0] == 0xAB) {
            app_post_u8(APP_EVT_SLEEP_TIMEOUT_CHANGED, data[1]);
            return len;
        } else if (data[0] == 0xB0) {
            uint8_t seq_idx = data[1] & 0x7F;
            if (seq_idx < MAX_SEQUENCES) {
                app_post_u8(APP_EVT_SEQUENCE_RUN_REQUESTED, seq_idx);
            }
            return len;
        }
    }

    /* Sequence write (0xB1) -> total length 147 bytes: [0xB1, seq_idx, KeySequence_t]
       Clear sequence (0xB2, seq_idx, 1) */
    if (len >= 2) {
        if (data[0] == 0xB1 && len == 147) {
            uint8_t seq_idx = data[1];
            if (seq_idx < MAX_SEQUENCES) {
                memcpy(&sequences[seq_idx], data + 2, sizeof(KeySequence_t));
                ssize_t w_rc = nvs_write(&fs, NVS_SEQUENCE_BASE_ID + seq_idx, &sequences[seq_idx], sizeof(KeySequence_t));
                if (w_rc < 0) printk("BLE ERROR: Failed to write sequence %d (err %d)\n", seq_idx, (int)w_rc);
                else printk("BLE: Sequence %d written\n", seq_idx);
            }
            return len;
        } else if (data[0] == 0xB2 && len >= 3) {
            uint8_t seq_idx = data[1];
            uint8_t flag = data[2];
            if (seq_idx < MAX_SEQUENCES && flag == 1) {
                memset(&sequences[seq_idx], 0, sizeof(KeySequence_t));
                nvs_write(&fs, NVS_SEQUENCE_BASE_ID + seq_idx, &sequences[seq_idx], sizeof(KeySequence_t));
                printk("BLE: Sequence %d cleared\n", seq_idx);
            }
            return len;
        }
    }

    /* Profile write: [profile_idx, Profile_t] */
    if (len >= sizeof(Profile_t) + 1) {
        uint8_t target_idx = data[0];
        if (target_idx < MAX_PROFILES) {
            memcpy(&profiles[target_idx], data + 1, sizeof(Profile_t));
            ssize_t w_rc = nvs_write(&fs, NVS_PROFILE_BASE_ID + target_idx, &profiles[target_idx], sizeof(Profile_t));
            if (w_rc < 0) {
                printk("BLE ERROR: Failed to write to Flash! Code: %d\n", (int)w_rc);
            } else {
                printk("BLE: Profile %d updated and saved (%d bytes written)\n", target_idx, (int)w_rc);
            }
        }
        return len;
    }

    printk("BLE ERROR: Invalid packet length or command. Received %d bytes\n", len);
    return len;
}

BT_GATT_SERVICE_DEFINE(macropad_config_svc,
    BT_GATT_PRIMARY_SERVICE(&config_svc_uuid),
    BT_GATT_CHARACTERISTIC(&info_char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, ble_flutter_read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(&dump_char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, ble_flutter_read_dump, NULL, NULL),
    BT_GATT_CHARACTERISTIC(&sequences_char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, ble_flutter_read_sequences, NULL, NULL),
    BT_GATT_CHARACTERISTIC(&export_char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, ble_flutter_read_export, NULL, NULL),
    BT_GATT_CHARACTERISTIC(&ctrl_char_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE_ENCRYPT, NULL, ble_flutter_write_ctrl, NULL)
);

/* ========================================================================== *
 * 4. МОДУЛЬ HARDWARE
 * Ініціалізація та сканування фізичних кнопок та енкодера
 * Interrupt-based wake-up support
 * ========================================================================== */

/* GPIO Interrupt callbacks */
static struct gpio_callback button_cb_data;

static void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    printk("Hardware: Wakeup interrupt triggered on pins 0x%x\n", pins);
}

void input_disable_runtime_interrupts(void)
{
    if (device_is_ready(enc_btn.port)) {
        gpio_pin_interrupt_configure_dt(&enc_btn, GPIO_INT_DISABLE);
    }
}

static void hardware_init(void) {
    if (device_is_ready(enc_btn.port)) { 
        gpio_pin_configure_dt(&enc_btn, GPIO_INPUT | GPIO_PULL_UP);
        /* Setup interrupt for encoder button */
        gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(enc_btn.pin));
        gpio_add_callback(enc_btn.port, &button_cb_data);
        gpio_pin_interrupt_configure_dt(&enc_btn, GPIO_INT_EDGE_TO_ACTIVE);
    }
    
    for (int i = 0; i < NUM_COLS; i++) { gpio_pin_configure_dt(&cols[i], GPIO_OUTPUT_INACTIVE); }
    for (int i = 0; i < NUM_ROWS; i++) { gpio_pin_configure_dt(&rows[i], GPIO_INPUT); }

    /* ДОДАНО: Запуск підсвітки */
    if (pwm_is_ready_dt(&backlight)) {
        printk("Hardware: PWM Backlight is ready!\n");
        set_backlight(current_brightness, backlight_on); 
    }
}

static void prepare_wakeup_pins(void) {
#if SYSTEM_OFF_WAKE_ON_MATRIX
    /* Matrix wake from SYSTEM OFF:
     * keep every column driven HIGH and arm SENSE on every row. Pressing any
     * key connects a column to a row, so any matrix key becomes a wake source.
     */
    for (int c = 0; c < NUM_COLS; c++) {
        uint32_t pin = nrf_pin_from_dt(&cols[c]);

        nrf_gpio_cfg_output(pin);
        nrf_gpio_pin_set(pin);
        printk("Power save: wake col %d configured pin %u output HIGH\n", c, pin);
    }

    for (int r = 0; r < NUM_ROWS; r++) {
        uint32_t pin = nrf_pin_from_dt(&rows[r]);

        nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
        printk("Power save: wake row %d configured pin %u sense HIGH\n", r, pin);
    }
#else
    for (int c = 0; c < NUM_COLS; c++) {
        nrf_gpio_cfg_default(nrf_pin_from_dt(&cols[c]));
    }

    for (int r = 0; r < NUM_ROWS; r++) {
        nrf_gpio_cfg_default(nrf_pin_from_dt(&rows[r]));
    }

    printk("Power save: matrix wake disabled for leakage test\n");
#endif

    /* Encoder button wake. The devicetree flag decides the active level. */
    if (device_is_ready(enc_btn.port)) {
        bool active_low = (enc_btn.dt_flags & GPIO_ACTIVE_LOW) != 0;
        uint32_t pin = nrf_pin_from_dt(&enc_btn);

        nrf_gpio_cfg_sense_input(
            pin,
            active_low ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_PULLDOWN,
            active_low ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH
        );
        printk("Power save: wake encoder button configured pin %u sense %s\n",
               pin,
               active_low ? "LOW" : "HIGH");
    }

    printk("Power save: all buttons armed as SYSTEM OFF wake sources\n");
}

/* ДОДАНО: Спільна функція для локальних дій (які виконуються самою платою, а не йдуть на ПК) */
static void handle_local_action(Action_t act) {
    if (act.type == ACTION_TYPE_LIGHT) {
        if (act.code == 1) { /* Більше */
            if (current_brightness <= 99) set_backlight(current_brightness + 1, true);
        } 
        else if (act.code == 2) { /* Менше */
            if (current_brightness >= 1) set_backlight(current_brightness - 1, true);
        }
        else if (act.code == 3) { /* Toggle ON/OFF */
            set_backlight(current_brightness, !backlight_on);
            uint8_t state_to_save = (uint8_t)backlight_on;
            nvs_write(&fs, NVS_LED_STATE_ID, &state_to_save, sizeof(state_to_save));
            printk("Backlight Toggle: %s\n", backlight_on ? "ON" : "OFF");
        }
    } else if (act.type == ACTION_TYPE_SEQUENCE) {
        input_execute_sequence(act.code);
    }
}

static bool hardware_scan_matrix(void) {
    uint32_t now = k_uptime_get_32(); 
    bool state_changed = false;

    for (int c = 0; c < NUM_COLS; c++) {
        gpio_pin_set_dt(&cols[c], 1);
        /* Small delay for capacitive settling - non-blocking optimized timing */
        k_busy_wait(30); 
        
        for (int r = 0; r < NUM_ROWS; r++) {
            bool is_pressed = (gpio_pin_get_dt(&rows[r]) == 1);
            if (is_pressed != key_state[r][c] && (now - last_time[r][c] > DEBOUNCE_MS)) {
                key_state[r][c] = is_pressed;
                last_time[r][c] = now; 
                state_changed = true;
                
                /* Post event to app event queue, state machine will handle it */
                if (is_pressed) {
                    printk("Input: Key pressed [%d,%d]\n", r, c);
                    app_post_key(APP_EVT_KEY_PRESSED, r, c);
                    
                } else {
                    printk("Input: Key released [%d,%d]\n", r, c);
                    app_post_key(APP_EVT_KEY_RELEASED, r, c);
                }
            }
        }
        
        gpio_pin_set_dt(&cols[c], 0);
    }
    
    if (device_is_ready(enc_btn.port)) {
        bool is_pressed = (gpio_pin_get_dt(&enc_btn) > 0);
        if (is_pressed != enc_btn_state && (now - enc_btn_last_time > DEBOUNCE_MS)) {
            enc_btn_state = is_pressed;
            enc_btn_last_time = now;
            state_changed = true;

            /* Post events for encoder button */
            if (is_pressed) {
                printk("Input: Encoder button pressed\n");
                app_post_simple(APP_EVT_ENCODER_BUTTON_PRESSED);
            } else {
                printk("Input: Encoder button released\n");
                app_post_simple(APP_EVT_ENCODER_BUTTON_RELEASED);
            }
        }
    }

    return state_changed;
}

static void hardware_scan_encoder(void)
{
    if (!device_is_ready(qdec_dev)) {
        return;
    }

    struct sensor_value val;

    if (sensor_sample_fetch(qdec_dev) != 0) {
        return;
    }

    if (sensor_channel_get(qdec_dev, SENSOR_CHAN_ROTATION, &val) != 0) {
        return;
    }

    if (val.val1 > 0) {
#if DEBUG_ENCODER_LOG
        printk("Input: Encoder CW\n");
#endif
        app_post_simple(APP_EVT_ENCODER_CW);
    } else if (val.val1 < 0) {
#if DEBUG_ENCODER_LOG
        printk("Input: Encoder CCW\n");
#endif
        app_post_simple(APP_EVT_ENCODER_CCW);
    }
}
/* ========================================================================== *
 * 5. ГОЛОВНИЙ ЦИКЛ ПРОГРАМИ
 * Об'єднує всі модулі разом
 * Sleep/Wake implementation with low power consumption
 * ========================================================================== */
K_THREAD_STACK_DEFINE(button_stack, 2048);
static struct k_thread button_thread;

void input_scan_init(void)
{
    /* Initialize input scanning thread */
    printk("Input: Initializing input scanning\n");
}

static void button_thread_fn(void *p1, void *p2, void *p3) {
    (void)p1; (void)p2; (void)p3;
    
    printk("\n--- Input Thread: Starting Hardware Scanning ---\n");
    
    /* Initialize storage and hardware */
    storage_init();
    hardware_init();
    battery_init();
    battery_update_ble();
    printk("Input: Hardware and storage initialized\n");

    /* Boot-time check for clear bonds combo (encoder button + 2+ matrix keys) */
    {
        int scan_count = 0;
        bool combo_active_at_boot = false;
        
        /* Do a few quick scans to detect if combo is pressed at boot */
        for (scan_count = 0; scan_count < 5; scan_count++) {
            hardware_scan_matrix();
            hardware_scan_encoder();
            
            if (input_combo_unpair_condition()) {
                combo_active_at_boot = true;
                printk("Input: Boot combo detected (encoder button + 2+ keys)\n");
                break;
            }
            
            k_msleep(50);
        }
        
        /* If combo active at boot and bonds not already cleared, clear them now */
        if (combo_active_at_boot) {
            printk("Input: Clear bonds requested during boot\n");
            app_post_simple(APP_EVT_CLEAR_BONDS_REQUESTED);
            
            /* Wait for button release before continuing */
            int release_wait = 0;
            while (input_combo_unpair_condition() && release_wait < 100) {
                hardware_scan_matrix();
                hardware_scan_encoder();
                k_msleep(50);
                release_wait++;
            }
            
            if (release_wait >= 100) {
                printk("Input: Boot combo timeout waiting for release\n");
            } else {
                printk("Input: Boot combo released, continuing boot\n");
            }
        }
    }

    /* Run button scanning in this dedicated thread */
    while (1) {
        
        if (input_scan_enabled) {
            if (hardware_scan_matrix()) {
                /* Events already posted by hardware_scan_matrix */
            }
            hardware_scan_encoder();

            battery_update_periodic();
            k_msleep(1);
        } else {
            k_msleep(100);
        }
    }
}

void hog_init(void) {}

void hog_button_loop(void) {
    static bool input_thread_started;

    if (input_thread_started) {
        return;
    }

    input_thread_started = true;

    /* Spawn button scanning as a separate thread so main thread can handle events */
    k_thread_create(&button_thread, button_stack, K_THREAD_STACK_SIZEOF(button_stack),
                    button_thread_fn, NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
    
    printk("Input: Button scanning thread started\n");
}
