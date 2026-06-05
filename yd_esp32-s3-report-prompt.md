# Hardware & Integration Report: VCC-GND YD-ESP32-S3 (N16R8)

This technical reference document contains specifications, pinout restrictions, and firmware configurations for the **YD-ESP32-S3** development board, designed by **VCC-GND Studio (源地工作室)**. It is tailored for developers integrating custom ESP-IDF or MicroPython applications on this hardware.

---

## 📐 Core Specifications

| Attribute | Specification | Details |
| :--- | :--- | :--- |
| **SoC** | ESP32-S3 (QFN56) | Revision `v0.2` (Silicon Rev 2), Dual-core Xtensa 32-bit LX7 |
| **Max CPU Frequency** | 240 MHz | Stable range: 80 / 160 / 240 MHz. |
| **Flash Memory** | 16 MB SPI Flash | Connected via Quad SPI (80 MHz, DIO mode) |
| **PSRAM** | 8 MB Octal PSRAM | Connected via Octal SPI (80 MHz, OPI mode) |
| **LDO Regulator** | 5V to 3.3V LDO | Dedicated 1A low-dropout regulator with high current overhead |

---

## 🔌 USB Interfaces
The board features **two USB-C ports** with different hardware layouts:

1. **Left USB-C Port (Native USB):**
   * **Controller:** Connected directly to the ESP32-S3's internal hardware `USB_SERIAL_JTAG` unit (**GPIO19 for D-** and **GPIO20 for D+**).
   * **Usage:** Recommended for hardware JTAG debugging, flashing (stub-based), serial console routing (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), and USB-OTG/CDC.
   * *Note:* Opening a connection to this port does **not** assert DTR/RTS to hard-reset the board. Active polling protocols are needed to handshake with booted firmware.

2. **Right USB-C Port (USB-to-UART):**
   * **Controller:** Connected to the onboard **WCH CH343P** USB-to-UART bridge.
   * **Pins:** Wired to **UART0 (GPIO43 for TXD0** and **GPIO44 for RXD0**).
   * **Usage:** Standard UART console, flashing, and boot monitoring. Supports automatic DTR/RTS bootloader state transitions.

---

## 📌 GPIO Mappings & Critical Pin Constraints

> [!WARNING]
> **Octal Bus Memory Reservation (Pins 26–32 and 35–37)**
> The ESP32-S3-WROOM-1 variant on this board uses 8-line Octal SPI to access the 8 MB PSRAM. 
> * **GPIOs 26, 27, 28, 29, 30, 31, and 32** are dedicated to the Flash.
> * **GPIOs 35, 36, and 37** are dedicated to the PSRAM.
> **NEVER** attempt to configure or pull these pins in code. Doing so disrupts the memory bus and causes instant CPU panics.

### Onboard Hardware Pins
* **WS2812 Addressable RGB LED:** Driven by **GPIO48** (GRB color order, requires NeoPixel/RMT timing protocol).
* **BOOT Button:** Mapped to **GPIO0**. Can be used as a general active-low button in user code after the system boots.
* **TX/RX Status LEDs:** Mounted directly on the UART0 TX/RX lines (**GPIO43** and **GPIO44**). If UART0 is disabled, these can be toggled manually as programmable LEDs.
* **RST Button:** Triggers a hardware reset of the ESP32-S3 (EN line).

---

## ⚡ Power Supply & Configuration Jumpers
* **Powering Options:** Can be powered via either USB-C port (or both simultaneously), or via the `5V` / `3V3` header pins.
* **USB-OTG Jumper:** Located on the bottom of the PCB. Bridge this solder jumper if you need the native USB port to act as a USB Host and supply VBUS power to connected external devices.

---

## 📐 Mechanical Layout & PCB Stackup (Probed from Altium PcbDoc)
The physical dimensions and layout parameters have been extracted directly from the board's design database (`ESP32-S3-size-pcb-ad.PcbDoc`):

### 1. Board Dimensions & Mounting
* **Outer Bounding Box:** `1.1 inches` Width × `2.25 inches` Length (`1100 mil` × `2250 mil` / `27.94 mm` × `57.15 mm`).
* **Header Spacing:** The dual 22-pin headers (J1 and J2) are spaced exactly `1.0 inch` (`1000 mil` / `25.4 mm`) apart, making the board fully breadboard-compatible.
* **Headers Placement:** Centered horizontally (Board center X is `4845.5 mil` relative to layout origin, with J1 at `4345.5 mil` and J2 at `5345.5 mil`).

### 2. PCB Layer Stackup (2-Layer Design)
* **Layer 1 (Top Overlay):** Silkscreen, annotations, component outlines.
* **Layer 2 (Top Solder):** Solder mask (0.4 mil thick, Resist material, Dielectric constant 3.50).
* **Layer 3 (Top Copper):** Component pads and routing (1.4 mil thick copper).
* **Layer 4 (Core Dielectric):** FR-4 core material (12.6 mil thick, Dielectric constant 4.80).
* **Layer 5 (Bottom Copper):** Return paths and routing (1.4 mil thick copper).
* **Layer 6 (Bottom Solder):** Solder mask (0.4 mil thick, Resist material).
* **Layer 7 (Bottom Overlay):** Silkscreen labels.

### 3. Main Board Component Placements
* **Left Pin Header (J1):** `HDR1X22` footprint (2.54mm pitch) located at `(4345.5 mil, 4079.2 mil)`.
* **Right Pin Header (J2):** `HDR1X22` footprint (2.54mm pitch) located at `(5345.5 mil, 4079.2 mil)`.
* **ESP32-S3 Module (U4):** `WIFIM-SMD_39P-L25.5-W18.0-P1.27` footprint located at `(4845.5 mil, 3754.2 mil)` (Centered near the top edge).
* **Left USB-C Connector (USB2 - Native USB):** `USB-C-SMD_TYPEC-304-BCP16` footprint located at `(4610.5 mil, 2104.2 mil)`.
* **Right USB-C Connector (USB1 - CH343P UART):** `USB-C-SMD_TYPEC-304-BCP16` footprint located at `(5080.5 mil, 2104.2 mil)`.

---

## ⚙️ ESP-IDF Configuration Reference (`sdkconfig`)

To compile firmware matching this board's memory and console configuration, use the following `sdkconfig` parameters:

```ini
# Console routing to Native USB JTAG
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y

# 16MB Flash Configuration
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# 8MB Octal PSRAM Configuration
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_TIME_CLEANUP=y
```

---

## 💻 Sample Code: Controlling the Onboard WS2812 RGB LED (ESP-IDF v6)

The onboard LED is addressable and must be driven via the ESP-IDF RMT (Remote Control) peripheral or LED Strip driver:

```c
#include "led_strip.h"
#include "esp_log.h"

#define LED_STRIP_BLINK_GPIO  48

static led_strip_handle_t led_strip;

void init_rgb_led(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI("LED", "RGB LED initialized on GPIO %d", LED_STRIP_BLINK_GPIO);
}

void set_rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_set_pixel(led_strip, 0, red, green, blue);
    led_strip_refresh(led_strip);
}
```