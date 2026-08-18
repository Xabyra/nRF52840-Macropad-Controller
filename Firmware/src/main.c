#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

/* Підключаємо заголовочні файли модулів */
#include "hog.h"
#include "app_events.h"
#include "app_sm.h"

#define DIAG_IMMEDIATE_SYSTEM_OFF 0

/* Лічильник активних підключень. Потрібен для підтримки мульти-підключення 
   (коли макропад підключений до кількох пристроїв одночасно) */
uint8_t active_connections = 0;
static bool advertising_active = false;
static bool ble_hid_ready = false;
static bool bt_stack_ready = false;
static uint8_t security_fail_count = 0;

#define BLE_STORAGE_MIGRATION_ID    0x7f00
#define BLE_STORAGE_MIGRATION_MAGIC 0x42544d31u
#define APP_NVS_PARTITION           app_storage_partition
#define BT_SETTINGS_PARTITION       storage_partition

static void ble_settings_save_work_handler(struct k_work *work);
static K_WORK_DEFINE(ble_settings_save_work, ble_settings_save_work_handler);

static void ble_settings_save_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        int save_err = settings_save();
        printk("BLE: settings_save after security err=%d\n", save_err);
    }
}

#if DIAG_IMMEDIATE_SYSTEM_OFF
static void diag_immediate_system_off(void)
{
    printk("DIAG: immediate GPIO default + SYSTEM OFF\n");

    for (uint8_t pin = 0; pin < 32; pin++) {
        nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(0, pin));
    }

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
    for (uint8_t pin = 0; pin < 16; pin++) {
        nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(1, pin));
    }
#endif

    nrf_power_system_off(NRF_POWER);

    printk("DIAG ERROR: nrf_power_system_off returned\n");
    sys_reboot(SYS_REBOOT_COLD);
}
#endif

static void ble_storage_migration_once(void)
{
    struct nvs_fs fs = {0};
    struct flash_pages_info info;
    const struct flash_area *bt_area;
    uint32_t marker = 0;
    bool app_storage_erased = false;
    int err;

    fs.flash_device = FIXED_PARTITION_DEVICE(APP_NVS_PARTITION);
    if (!device_is_ready(fs.flash_device)) {
        printk("BLE: app storage flash not ready, skip migration\n");
        return;
    }

    fs.offset = FIXED_PARTITION_OFFSET(APP_NVS_PARTITION);
    err = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (err) {
        printk("BLE: app storage page info failed err=%d\n", err);
        return;
    }

    fs.sector_size = info.size;
    fs.sector_count = FIXED_PARTITION_SIZE(APP_NVS_PARTITION) / info.size;

    err = nvs_mount(&fs);
    if (err) {
        const struct flash_area *app_area;

        printk("BLE: app storage migration mount failed err=%d, erasing app storage\n", err);
        err = flash_area_open(FIXED_PARTITION_ID(APP_NVS_PARTITION), &app_area);
        if (err) {
            printk("BLE: app storage open failed err=%d\n", err);
            return;
        }

        err = flash_area_erase(app_area, 0, app_area->fa_size);
        flash_area_close(app_area);
        if (err) {
            printk("BLE: app storage erase failed err=%d\n", err);
            return;
        }
        app_storage_erased = true;

        err = nvs_mount(&fs);
        if (err) {
            printk("BLE: app storage migration remount failed err=%d\n", err);
            return;
        }
    }

    err = nvs_read(&fs, BLE_STORAGE_MIGRATION_ID, &marker, sizeof(marker));
    if (err == sizeof(marker) && marker == BLE_STORAGE_MIGRATION_MAGIC) {
        return;
    }

    if (!app_storage_erased) {
        const struct flash_area *app_area;

        err = flash_area_open(FIXED_PARTITION_ID(APP_NVS_PARTITION), &app_area);
        if (err) {
            printk("BLE: app storage open failed err=%d\n", err);
            return;
        }

        err = flash_area_erase(app_area, 0, app_area->fa_size);
        flash_area_close(app_area);
        if (err) {
            printk("BLE: app storage erase failed err=%d\n", err);
            return;
        }

        err = nvs_mount(&fs);
        if (err) {
            printk("BLE: app storage migration remount failed err=%d\n", err);
            return;
        }
    }

    err = flash_area_open(FIXED_PARTITION_ID(BT_SETTINGS_PARTITION), &bt_area);
    if (err) {
        printk("BLE: settings partition open failed err=%d\n", err);
        return;
    }

    err = flash_area_erase(bt_area, 0, bt_area->fa_size);
    flash_area_close(bt_area);
    if (err) {
        printk("BLE: settings partition erase failed err=%d\n", err);
        return;
    }

    marker = BLE_STORAGE_MIGRATION_MAGIC;
    err = nvs_write(&fs, BLE_STORAGE_MIGRATION_ID, &marker, sizeof(marker));
    printk("BLE: split app storage from BT settings, erased old settings err=%d\n", err < 0 ? err : 0);
}
/* ========================================================================== *
 * 1. НАЛАШТУВАННЯ РЕКЛАМИ (ADVERTISING)
 * Це те, що бачить телефон або комп'ютер під час пошуку Bluetooth пристроїв
 * ========================================================================== */
static const struct bt_data ad[] = {
    /* Вказуємо, що це звичайний BLE пристрій (без підтримки старого Bluetooth Classic) */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    
    /* Транслюємо UUID сервісів, щоб ОС відразу зрозуміла, що це за пристрій:
       - HIDS_VAL (0x1812): HID-пристрій (клавіатура/миша)
       - BAS_VAL  (0x180F): Сервіс батареї */
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
              BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

/* Функція для запуску трансляції (щоб макропад став видимим для пошуку) */
int ble_app_start_advertising(void)
{
    static bool first_advertising_after_boot = true;
    if (first_advertising_after_boot) {
        first_advertising_after_boot = false;
        printk("BLE: first advertising after boot, waiting for host/security stability\n");
        k_msleep(1000);
    }

    int err = bt_le_adv_stop();
    if (err && err != -EALREADY) {
        printk("BLE: Advertising stop before restart failed (err %d)\n", err);
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        if (err == -EALREADY) {
            printk("BLE: Advertising already started\n");
            advertising_active = true;
        } else {
            printk("BLE: Advertising failed to start (err %d), retrying\n", err);
            k_msleep(50);
            err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
            if (!err) {
                printk("BLE: Advertising successfully started on retry\n");
                advertising_active = true;
            } else {
                printk("BLE: Advertising retry failed (err %d)\n", err);
                return err;
            }
        }
    } else {
        printk("BLE: Advertising successfully started\n");
        advertising_active = true;
    }

    return 0;
}

/* Stop advertising */
int ble_app_stop_advertising(void)
{
    int err = bt_le_adv_stop();
    if (err && err != -EALREADY && err != -EINVAL) {
        printk("BLE: Advertising stop failed (err %d)\n", err);
        return err;
    }

    advertising_active = false;
    printk("BLE: Advertising stopped\n");
    return 0;
}

/* Get active connection count */
uint8_t ble_app_active_connections(void)
{
    return active_connections;
}

/* Check if we have free connection slots */
bool ble_app_has_free_connection_slots(void)
{
    return active_connections < CONFIG_BT_MAX_CONN;
}

bool ble_app_can_send_hid(void)
{
    return active_connections > 0 && ble_hid_ready;
}

static void disconnect_one_conn_for_clear_bonds(struct bt_conn *conn, void *data)
{
    ARG_UNUSED(data);

    int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    printk("BT: disconnect for clear bonds err=%d\n", err);
}

static void disconnect_one_conn_for_sleep(struct bt_conn *conn, void *data)
{
    ARG_UNUSED(data);

    int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        printk("BLE: sleep disconnect failed err=%d\n", err);
    }
}

/* Low-level BLE shutdown requested by app_sm before SYSTEM OFF. */
void ble_app_disconnect_all_for_sleep(void)
{
    ble_app_stop_advertising();
    bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_one_conn_for_sleep, NULL);
    printk("BLE: forced off for sleep\n");
}

/* Clear all bonding records */
int ble_app_clear_bonds(void)
{
    printk("BT: clear bonds requested\n");

    if (!bt_stack_ready) {
        printk("BT ERROR: clear bonds requested before Bluetooth ready\n");
        return -EAGAIN;
    }

    /* Stop advertising before disconnecting. This is not the sleep flow. */
    ble_app_stop_advertising();

    /* Disconnect all active LE connections without using sleep helper */
    bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_one_conn_for_clear_bonds, NULL);

    /* Let the controller finish the disconnect procedure */
    k_msleep(500);

    int err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
    printk("BT: bt_unpair returned %d\n", err);

    if (err == 0) {
        printk("BT: all local bonds cleared\n");
    } else {
        printk("BT ERROR: bt_unpair failed err=%d\n", err);
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        int save_err = settings_save();
        printk("BT: settings_save returned %d\n", save_err);
    }

    printk("BT: local bonds clear flow complete. Remove/Forgot device on host too.\n");
    printk("BT: rebooting after clear bonds\n");

    k_msleep(500);
#ifdef CONFIG_REBOOT
    sys_reboot(SYS_REBOOT_COLD);
#else
    NVIC_SystemReset();
#endif
    return err;
}

/* Deprecated: use ble_app_start_advertising() instead */
__attribute__((unused))
static void start_advertising(void)
{
    ble_app_start_advertising();
}

/* ========================================================================== *
 * 2. КОЛБЕКИ (ОБРОБНИКИ ПОДІЙ) BLUETOOTH З'ЄДНАННЯ
 * Спрацьовують автоматично, коли хтось підключається або відключається
 * ========================================================================== */

/* Викликається, коли пристрій (ПК або телефон) успішно підключився */
static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        printk("Failed to connect to %s (%u)\n", addr, err);
        return;
    }

    active_connections++;
    ble_hid_ready = false;
    printk("BLE: Connected %s. Total connections: %d\n", addr, active_connections);

    int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (sec_err && sec_err != -EALREADY) {
        printk("BLE: security request failed err=%d\n", sec_err);
    }

    /* Post event to state machine (do not make strategic decisions here) */
    app_post_simple(APP_EVT_BLE_CONNECTED);
}

/* Викликається, коли пристрій відключається (наприклад, вимкнули Bluetooth на ПК) */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (active_connections > 0) {
        active_connections--;
    }

    ble_hid_ready = false;

    printk("BLE: Disconnected from %s (reason 0x%02x). Total connections: %d\n", addr, reason, active_connections);

    /* Post disconnect event to state machine (state machine will decide about advertising) */
    struct app_event evt = {
        .type = APP_EVT_BLE_DISCONNECTED,
        .data = {
            .ble = {
                .reason = reason,
            }
        }
    };
    app_post_event(&evt);
}

/* Викликається при зміні статусу безпеки (успішне або неуспішне створення пари) */
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        printk("BLE: Security changed: %s level %u\n", addr, level);
        if (level >= BT_SECURITY_L2) {
            ble_hid_ready = true;
            security_fail_count = 0;
            k_work_submit(&ble_settings_save_work);
            app_post_simple(APP_EVT_BLE_HID_READY);
        }
    } else {
        ble_hid_ready = false;
        security_fail_count++;
        printk("BLE: Security failed: %s level %u err %d\n", addr, level, err);
        app_post_security_failed(err);
    }

    /* Post event to state machine for tracking */
    app_post_simple(APP_EVT_BLE_SECURITY_CHANGED);
}

/* Реєструємо наші обробники подій в системі Zephyr */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

/* ========================================================================== *
 * 3. АВТОРИЗАЦІЯ ТА СТВОРЕННЯ ПАРИ (PAIRING)
 * ========================================================================== */

/* Якщо для створення пари потрібен пін-код (зазвичай для клавіатур без екрану це не використовується, 
   але залишено для сумісності з `CONFIG_SAMPLE_BT_USE_AUTHENTICATION`) */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,
    .cancel = auth_cancel,
};

/* ========================================================================== *
 * 4. ІНІЦІАЛІЗАЦІЯ ТА ГОЛОВНИЙ ЦИКЛ
 * ========================================================================== */

/* Ця функція викликається автоматично, коли підсистема Bluetooth готова до роботи */
static void bt_ready(int err)
{
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        app_post_error(err);
        return;
    }
    printk("BLE: Bluetooth initialized\n");
    bt_stack_ready = true;

    /* ВАЖЛИВО: Завантажуємо збережені налаштування Bluetooth з Flash пам'яті (NVS).
       Це потрібно, щоб макропад "пам'ятав" спарені комп'ютери після перезавантаження */
    if (IS_ENABLED(CONFIG_SETTINGS)) {
        int settings_err = settings_load();
        printk("BLE: settings_load err=%d\n", settings_err);
    }

    /* Post event to state machine */
    app_post_simple(APP_EVT_BT_READY);
}

/* Точка входу в програму */
int main(void)
{
    int err;

    printk("\n=== MACROPAD V10 with Event-Driven Architecture ===\n");

#if DIAG_IMMEDIATE_SYSTEM_OFF
    diag_immediate_system_off();
#endif

    ble_storage_migration_once();

    /* Initialize and start the state machine after storage migration */
    app_sm_init();

    /* Start state machine thread */
    app_sm_start();

    /* Реєструємо колбеки для створення пари, якщо автентифікація увімкнена в конфігах */
    if (IS_ENABLED(CONFIG_SAMPLE_BT_USE_AUTHENTICATION)) {
        bt_conn_auth_cb_register(&auth_cb_display);
    }

    /* Вмикаємо Bluetooth. Коли він увімкнеться, викличеться колбек bt_ready */
    err = bt_enable(bt_ready);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    /* Main thread waits forever, events are processed by app_sm and handlers */
    k_sleep(K_FOREVER);

    return 0;
}
