# Human Stress Control

This repository contains the firmware for an STM32-based heart-rate and stress monitoring system. The main project lives in `Human_stress_control-HeatbeatSP/` and targets an `STM32L432KCUx` MCU. It reads a `MAX30102` pulse oximeter sensor, computes heart-rate variability metrics, drives a small OLED display, and uses a buzzer for feedback.

A separate Flutter app also exists under `stess app/hrv_monitor/`, but the primary embedded project is the STM32 firmware.

## What the firmware does

- Samples raw PPG data from the `MAX30102` over I2C
- Detects beats and computes HRV statistics such as `RMSSD`, `SDNN`, and a stress index
- Displays status and waveform information on an OLED using `U8g2`
- Sends summary data over UART
- Plays a calm melody and shows a breathing animation when stress is elevated

## Repository layout

- `Human_stress_control-HeatbeatSP/` - main STM32 firmware project
- `Human_stress_control-HeatbeatSP/Core/Inc` - application headers
- `Human_stress_control-HeatbeatSP/Core/Src` - application source code
- `Human_stress_control-HeatbeatSP/Drivers` - STM32 HAL, CMSIS, and `U8g2`
- `Human_stress_control-HeatbeatSP/Middlewares` - FreeRTOS and third-party middleware
- `Human_stress_control-HeatbeatSP/MDK-ARM/Phase1Project.uvprojx` - Keil uVision project file
- `Human_stress_control-HeatbeatSP/Phase1Project.ioc` - STM32CubeMX configuration

## Requirements

- Keil MDK-ARM / uVision with ARM Compiler 6 support
- STM32CubeMX if you want to regenerate code from the `.ioc` file
- An `STM32L432KCUx` board wired for this project
- A debugger/programmer such as ST-LINK

## Build

1. Open `Human_stress_control-HeatbeatSP/MDK-ARM/Phase1Project.uvprojx` in Keil uVision.
2. Make sure the device pack for `STM32L4xx` is installed.
3. Select the `Phase1Project` target.
4. Build the project from the IDE.


## Run

1. Connect the STM32 board through ST-LINK or another supported programmer.
2. Flash the built image from Keil using the project’s debug or download flow.
3. Reset the board and open a serial terminal on the UART debug port to watch boot and HRV logs.
4. Place a finger on the `MAX30102` sensor to start live measurement.
