/*
 * sc_hid_desc.c — TinyUSB HID composite descriptor tables
 *
 * Composite HID device:
 *   Report 1 — Boot Keyboard (8 bytes: modifier + reserved + 6 keycodes)
 *   Report 2 — Consumer Control (2 bytes: usage code)
 *
 * VID / PID — change to match your USB Implementers Forum allocation.
 * During development 0x303A (Espressif) / 0x4002 is used.
 */

#include "sc_hid_desc.h"
#include "tusb.h"
#include <string.h>

/* ── USB descriptor constants ────────────────────────────────────────────── */
#define SC_HID_VID            (0x303A)
#define SC_HID_PID            (0x4002)
#define SC_HID_BCD_DEVICE     (0x0100)   /* v1.00 */

#define SC_HID_EP_HID         (0x81)     /* IN endpoint 1 */
#define SC_HID_EP_HID_SZ      (64)
#define SC_HID_POLL_MS        (1)        /* 1 ms polling = 1000 Hz */

/* ── HID Report Descriptor ───────────────────────────────────────────────── */
static const uint8_t s_hid_report_desc[] = {
    /* --- Report 1: Gamepad A (Buttons 1-128) ---------------------- */
    HID_USAGE_PAGE (HID_USAGE_PAGE_DESKTOP),
    HID_USAGE      (HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION (HID_COLLECTION_APPLICATION),
        HID_REPORT_ID (1)

        /* 128 Buttons (16 bytes) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_BUTTON),
        HID_USAGE_MIN    (1),
        HID_USAGE_MAX    (128),
        HID_LOGICAL_MIN  (0),
        HID_LOGICAL_MAX  (1),
        HID_REPORT_COUNT (128),
        HID_REPORT_SIZE  (1),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        /* 8 Analog Axes (8 bytes: X, Y, Z, Rz, Rx, Ry, Slider, Dial) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_DESKTOP),
        HID_USAGE        (HID_USAGE_DESKTOP_X),
        HID_USAGE        (HID_USAGE_DESKTOP_Y),
        HID_USAGE        (HID_USAGE_DESKTOP_Z),
        HID_USAGE        (HID_USAGE_DESKTOP_RX),
        HID_USAGE        (HID_USAGE_DESKTOP_RY),
        HID_USAGE        (HID_USAGE_DESKTOP_RZ),
        HID_USAGE        (HID_USAGE_DESKTOP_SLIDER),
        HID_USAGE        (HID_USAGE_DESKTOP_DIAL),
        HID_LOGICAL_MIN  (0),
        HID_LOGICAL_MAX_N(0xFF, 1),
        HID_REPORT_COUNT (8),
        HID_REPORT_SIZE  (8),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        /* 1 POV Hat (1 byte: 4 bits value + 4 bits padding) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_DESKTOP),
        HID_USAGE        (HID_USAGE_DESKTOP_HAT_SWITCH),
        HID_LOGICAL_MIN  (1),
        HID_LOGICAL_MAX  (8),
        HID_PHYSICAL_MIN (0),
        HID_PHYSICAL_MAX_N(315, 2),
        HID_REPORT_SIZE  (4),
        HID_REPORT_COUNT (1),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NULL_STATE),
        /* Padding */
        HID_REPORT_SIZE  (1),
        HID_REPORT_COUNT (4),
        HID_INPUT        (HID_CONSTANT),
    HID_COLLECTION_END,

    /* --- Report 2: Gamepad B (Buttons 129-256) -------------------- */
    HID_USAGE_PAGE (HID_USAGE_PAGE_DESKTOP),
    HID_USAGE      (HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION (HID_COLLECTION_APPLICATION),
        HID_REPORT_ID (2)

        /* 128 Buttons (16 bytes) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_BUTTON),
        HID_USAGE_MIN    (1),
        HID_USAGE_MAX    (128),
        HID_LOGICAL_MIN  (0),
        HID_LOGICAL_MAX  (1),
        HID_REPORT_COUNT (128),
        HID_REPORT_SIZE  (1),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        /* 8 Analog Axes (8 bytes: X, Y, Z, Rz, Rx, Ry, Slider, Dial) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_DESKTOP),
        HID_USAGE        (HID_USAGE_DESKTOP_X),
        HID_USAGE        (HID_USAGE_DESKTOP_Y),
        HID_USAGE        (HID_USAGE_DESKTOP_Z),
        HID_USAGE        (HID_USAGE_DESKTOP_RX),
        HID_USAGE        (HID_USAGE_DESKTOP_RY),
        HID_USAGE        (HID_USAGE_DESKTOP_RZ),
        HID_USAGE        (HID_USAGE_DESKTOP_SLIDER),
        HID_USAGE        (HID_USAGE_DESKTOP_DIAL),
        HID_LOGICAL_MIN  (0),
        HID_LOGICAL_MAX_N(0xFF, 1),
        HID_REPORT_COUNT (8),
        HID_REPORT_SIZE  (8),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        /* 1 POV Hat (1 byte: 4 bits value + 4 bits padding) */
        HID_USAGE_PAGE   (HID_USAGE_PAGE_DESKTOP),
        HID_USAGE        (HID_USAGE_DESKTOP_HAT_SWITCH),
        HID_LOGICAL_MIN  (1),
        HID_LOGICAL_MAX  (8),
        HID_PHYSICAL_MIN (0),
        HID_PHYSICAL_MAX_N(315, 2),
        HID_REPORT_SIZE  (4),
        HID_REPORT_COUNT (1),
        HID_INPUT        (HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NULL_STATE),
        /* Padding */
        HID_REPORT_SIZE  (1),
        HID_REPORT_COUNT (4),
        HID_INPUT        (HID_CONSTANT),
    HID_COLLECTION_END,

    /* --- Report 3: Consumer Control ------------------------------------- */
    HID_USAGE_PAGE (HID_USAGE_PAGE_CONSUMER),
    HID_USAGE      (HID_USAGE_CONSUMER_CONTROL),
    HID_COLLECTION (HID_COLLECTION_APPLICATION),
        HID_REPORT_ID (3)
        HID_USAGE_MIN_N (0x0000, 2),
        HID_USAGE_MAX_N (0x03FF, 2),
        HID_LOGICAL_MIN (0x00),
        HID_LOGICAL_MAX_N(0x03FF, 2),
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE (16),
        HID_INPUT       (HID_DATA | HID_ARRAY | HID_ABSOLUTE),
    HID_COLLECTION_END,

    /* --- Report 4: Keyboard --------------------------------------------- */
    HID_USAGE_PAGE (HID_USAGE_PAGE_DESKTOP),
    HID_USAGE      (HID_USAGE_DESKTOP_KEYBOARD),
    HID_COLLECTION (HID_COLLECTION_APPLICATION),
        HID_REPORT_ID (4)
        /* Modifiers (1 byte) */
        HID_USAGE_PAGE (HID_USAGE_PAGE_KEYBOARD),
        HID_USAGE_MIN  (224), /* Left Control */
        HID_USAGE_MAX  (231), /* Right GUI */
        HID_LOGICAL_MIN (0),
        HID_LOGICAL_MAX (1),
        HID_REPORT_SIZE (1),
        HID_REPORT_COUNT(8),
        HID_INPUT       (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        /* Reserved (1 byte) */
        HID_REPORT_SIZE (8),
        HID_REPORT_COUNT(1),
        HID_INPUT       (HID_CONSTANT),
        /* Keycodes (6 bytes) */
        HID_USAGE_MIN   (0),
        HID_USAGE_MAX   (101),
        HID_LOGICAL_MIN (0),
        HID_LOGICAL_MAX (101),
        HID_REPORT_SIZE (8),
        HID_REPORT_COUNT(6),
        HID_INPUT       (HID_DATA | HID_ARRAY | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

/* ── Device Descriptor ───────────────────────────────────────────────────── */
static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = SC_HID_VID,
    .idProduct          = SC_HID_PID,
    .bcdDevice          = SC_HID_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static const tusb_desc_device_qualifier_t s_device_qualifier_desc = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};

/* ── Configuration Descriptor ────────────────────────────────────────────── */
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc),
                       SC_HID_EP_HID, SC_HID_EP_HID_SZ, SC_HID_POLL_MS),
};

static const uint8_t s_hs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc),
                       SC_HID_EP_HID, SC_HID_EP_HID_SZ, SC_HID_POLL_MS),
};

/* ── String Descriptors ──────────────────────────────────────────────────── */
static const char *s_string_desc[] = {
    "\x09\x04",            /* 0: Language = English (0x0409) */
    "DOTM - HotBox",       /* 1: Manufacturer */
    "HotBox HID",          /* 2: Product */
    "HB-001",              /* 3: Serial */
};

/* ── Accessors for esp_tinyusb config ───────────────────────────────────── */

const tusb_desc_device_t *sc_hid_get_device_descriptor(void)
{
    return &s_device_desc;
}

const uint8_t *sc_hid_get_configuration_descriptor(void)
{
    return s_config_desc;
}

const uint8_t *sc_hid_get_hs_configuration_descriptor(void)
{
    return s_hs_config_desc;
}

const tusb_desc_device_qualifier_t *sc_hid_get_qualifier_descriptor(void)
{
    return &s_device_qualifier_desc;
}

const char **sc_hid_get_string_descriptors(void)
{
    return (const char **)s_string_desc;
}

int sc_hid_get_string_descriptor_count(void)
{
    return (int)(sizeof(s_string_desc) / sizeof(s_string_desc[0]));
}

/* ── TinyUSB callbacks ───────────────────────────────────────────────────── */

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&s_device_desc;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_config_desc;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            const uint8_t *buffer, uint16_t bufsize)
{
    /* This terminal is output-only; ignore SET_REPORT (LED state etc.) */
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc_str[32];
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&desc_str[1], s_string_desc[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(s_string_desc) / sizeof(s_string_desc[0])) {
            return NULL;
        }
        const char *str = s_string_desc[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = str[i];
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
