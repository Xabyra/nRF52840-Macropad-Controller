# Macropad nRF52840 Project

This repository contains the complete project for a custom, programmable macropad built around the nRF52840 microcontroller. It features a Bluetooth Low Energy (BLE) HID interface and a cross-platform desktop application (developed with Flutter) for intuitive configuration. This project showcases a full-stack approach, integrating embedded systems development with modern UI/UX design.

## Project Overview

The Macropad nRF52840 is designed to enhance productivity by providing a highly customizable input device. Users can define complex key sequences, assign functions to a 3x4 key matrix and a rotary encoder, and manage multiple profiles for different applications.

## Key Features

*   **Bluetooth Low Energy (BLE) HID**: Connects wirelessly as a standard keyboard and mouse device to computers and other compatible devices.
*   **Customizable Keymaps**: Assign individual keys and encoder actions (press, clockwise, counter-clockwise) to standard keyboard keys, mouse clicks/scrolls, or custom sequences.
*   **Multiple Profiles**: Store and switch between different configuration profiles, allowing for tailored setups for various tasks or applications (e.g., "Photoshop Profile", "Coding Profile").
*   **Sequence/Macro Playback**: Program complex sequences of key presses with customizable delays, including random delays for more human-like input.
*   **Backlight Control**: Integrated PWM-controlled backlight with adjustable brightness and on/off toggle.
*   **Battery Monitoring**: Monitors and reports battery level via BLE.
*   **Low-Power Modes**: Efficient power management with inactivity-based sleep and system-off modes to maximize battery life.
*   **Desktop Configuration Application**: A user-friendly Flutter application for Windows and Linux to manage all macropad settings over BLE.
*   **Zephyr RTOS Firmware**: Robust and efficient embedded software built on the Zephyr Real-Time Operating System.

## Project Structure

This repository is divided into two main components:

*   **`Firmware/`**: Contains the embedded software for the nRF52840 microcontroller. This includes the Zephyr RTOS application, BLE services, hardware drivers, and logic for input processing and power management.
*   **`Software/`**: Contains the cross-platform desktop application developed with Flutter. This application provides the graphical user interface for configuring the macropad's profiles, keymaps, and sequences via BLE.

## Getting Started

To get started with building and exploring this project, please refer to the detailed `README.md` files within each sub-directory:

*   **Firmware Readme**
*   **Software Readme**

## Portfolio Relevance

This project demonstrates expertise in:

*   **Embedded Systems Development**: C programming, RTOS (Zephyr), hardware interaction (GPIO, PWM, ADC), low-power design, device tree configuration.
*   **Bluetooth Low Energy (BLE)**: Implementing custom GATT services, HID profiles, advertising, and bonding.
*   **Cross-Platform Application Development**: Flutter framework, Dart programming, desktop application architecture, BLE communication from desktop.
*   **Full-Stack Integration**: Seamless communication and interaction between embedded hardware and a desktop application.
*   **UI/UX Design**: Creating an intuitive and functional user interface for device configuration.
*   **Version Control & Build Systems**: CMake, `west` (Zephyr), `flutter` CLI.

This project is a comprehensive example of bringing a hardware product to life with both robust embedded software and a polished user-facing application.