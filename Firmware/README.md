# Macropad nRF52840 Firmware

This directory contains the embedded software for the Macropad nRF52840, developed using the Zephyr Real-Time Operating System (RTOS). The firmware manages all hardware interactions, Bluetooth Low Energy (BLE) communication, and persistent storage of user configurations.

## Hardware Overview

*   **Microcontroller**: Nordic nRF52840 (specifically targeting the `nrf52840dk_nrf52840` development kit, but adaptable to custom boards).
*   **Input**:
    *   3x4 Key Matrix: Scans 12 physical keys.
    *   Rotary Encoder: Provides rotational input (clockwise/counter-clockwise) and a push-button.
*   **Output**:
    *   PWM-controlled Backlight: Adjustable brightness for visual feedback.
*   **Connectivity**: Bluetooth Low Energy (BLE) for HID (Human Interface Device) and custom configuration services.
*   **Power Management**: Battery voltage monitoring via ADC and various low-power modes.

## Key Technologies

*   **Zephyr RTOS**: Provides a robust and scalable foundation for embedded development, including drivers, BLE stack, and power management features.
*   **Bluetooth LE HID Service**: Implements standard keyboard and mouse profiles for seamless integration with host devices.
*   **Custom BLE GATT Service**: A proprietary service for the Flutter configuration application to read/write device settings (profiles, keymaps, sequences).
*   **NVS (Non-Volatile Storage)**: Utilizes Zephyr's NVS module to store user-defined profiles, keymaps, sequences, and other settings persistently in flash memory.
*   **Hardware Drivers**: GPIO for key matrix and encoder button, PWM for backlight, ADC for battery monitoring, QDEC for rotary encoder.

## Features

*   **Debounced Input Scanning**: Reliable detection of key presses and encoder events.
*   **BLE HID Reports**: Sends keyboard, mouse (including scroll wheel), and media key reports to connected hosts.
*   **Multi-Profile Support**: Stores and allows switching between `MAX_PROFILES` (currently 3) distinct configuration profiles.
*   **Programmable Sequences**: Executes complex macros with multiple key presses and customizable delays, including optional random delays.
*   **Backlight Control**: Manages backlight state (on/off) and brightness levels.
*   **Battery Monitoring**: Reports current battery percentage to connected BLE hosts.
*   **Power Management**:
    *   Inactivity Timer: Automatically enters low-power connected mode after a configurable timeout.
    *   System-Off Mode: Enters ultra-low-power mode after extended inactivity, waking up on any key press.
*   **Boot-time Bond Clearing**: A special key combination at boot allows clearing all stored BLE bonding information.

## Compilation and Flashing

### Prerequisites

1.  **Zephyr SDK**: Install the Zephyr SDK. Based on the project's `CMakeCache.txt`, version `0.16.5` was used.
    ```bash
    # Download the SDK (adjust version if needed)
    wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.5/zephyr-sdk-0.16.5_linux-x86_64.tar.xz
    # Extract
    tar xvf zephyr-sdk-0.16.5_linux-x86_64.tar.xz
    # Install (follow prompts)
    ./zephyr-sdk-0.16.5/setup.sh
    ```
2.  **West Tool**: Install the Zephyr `west` meta-tool.
    ```bash
    pip3 install west
    ```
3.  **Zephyr Source Code**: Initialize and update your Zephyr workspace.
    ```bash
    # Initialize a new Zephyr workspace (adjust --mr to the Zephyr version used, e.g., v3.5.0)
    west init -m https://github.com/zephyrproject-rtos/zephyr --mr v3.5.0 zephyrproject
    cd zephyrproject
    west update
    west zephyr-export
    ```
    *Note: If you already have a Zephyr workspace, ensure it's updated and configured correctly.*

### Building the Firmware

1.  Navigate to the `Firmware` directory of this project:
    ```bash
    cd c:\Users\Vitalii\Desktop\Macropad nrf52840\Firmware
    ```
2.  Build the application for the `nrf52840dk_nrf52840` board. The `prj.conf` and `app.overlay` files define the specific configuration for the macropad.
    ```bash
    west build -b nrf52840dk_nrf52840 --pristine auto -- -DCONF_FILE="prj.conf;app.overlay"
    ```
    *   `west build`: The command to build Zephyr applications.
    *   `-b nrf52840dk_nrf52840`: Specifies the target board.
    *   `--pristine auto`: Ensures a clean build environment.
    *   `-- -DCONF_FILE="prj.conf;app.overlay"`: Passes additional CMake arguments to specify the configuration files.

    Upon successful compilation, the firmware image (`zephyr.hex`, `zephyr.bin`, `zephyr.elf`) will be located in the `build/zephyr/` directory.

### Flashing the Firmware

1.  Ensure your nRF52840 development kit (or custom board with a debugger like J-Link) is connected to your computer.
2.  From the `Firmware` directory, use `west flash`:
    ```bash
    west flash
    ```
    This command will automatically detect your debugger and flash the compiled firmware.

## Configuration Files

*   **`prj.conf`**: Contains Kconfig options that enable and configure various Zephyr modules and drivers used by the macropad (e.g., BLE, NVS, GPIO, PWM, ADC).
*   **`app.overlay`**: Defines hardware-specific settings and pin assignments using Zephyr's Device Tree Overlay system. This is where the key matrix, encoder, and backlight pins are mapped.