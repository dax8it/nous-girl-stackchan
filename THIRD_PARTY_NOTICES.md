# Third-party notices

This repo's own code and docs are under the [MIT License](LICENSE). The firmware builds published in this repo's releases also contain the third-party software listed below. Each part keeps its own license. Full license texts are in [licenses/](licenses/). Where an upstream component is Apache-2.0, the full text is [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt); none of the Apache-2.0 components in the build ship a NOTICE file.

How this list was made: every license below was read from the LICENSE file or `idf_component.yml` in the actual dj7 source tree used for the release builds (xiaozhi-esp32 `5d54beb7` plus the fairy-stackchan layer, ESP-IDF 6.1.0, component versions from `dependencies.lock`), or from the upstream repository. Items we could not confirm are marked **unconfirmed**. Email addresses inside upstream license texts were replaced with "email omitted, see upstream"; the upstream files are unchanged.

Art is separate: `faces/starter/` is [CC0](faces/starter/LICENSE). `faces/tribute/` is **not** covered by any open license, see [faces/tribute/nous-girl/README.md](faces/tribute/nous-girl/README.md).

## 1. Firmware base

| Project | Link | License | Copyright | Text |
|---|---|---|---|---|
| xiaozhi-esp32 (firmware this build is based on) | https://github.com/78/xiaozhi-esp32 | MIT | Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.; Copyright (c) 2025 Project Contributors | [xiaozhi-esp32.txt](licenses/xiaozhi-esp32.txt) |
| fairy-stackchan (CoreS3 StackChan layer, avatar decorators; commit 6ddd844). Its game-character art is NOT used or included. | https://github.com/Michae1o/fairy-stackchan | MIT | Copyright (c) 2026 Michae1o | [fairy-stackchan.txt](licenses/fairy-stackchan.txt) |
| StackChan avatar code (`stackchan_avatar/`: eyes, mouth, decorators), from M5Stack's official StackChan firmware via fairy-stackchan | https://github.com/m5stack/StackChan | MIT (from the SPDX header in every source file and fairy-stackchan's notices; the upstream repo shows no LICENSE file, so **unconfirmed** at repo level) | SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD | standard MIT text, same as [m5stack__m5pm1.txt](licenses/m5stack__m5pm1.txt) |
| m5unified-avatar-mic (vowel detector ported into `hg_vowel.h`, commit 8b6f79f) | https://github.com/mongonta0716/m5unified-avatar-mic | MIT | Copyright (c) 2026 Takao Akaki | [m5unified-avatar-mic.txt](licenses/m5unified-avatar-mic.txt) |
| Bosch BMI270 Sensor API (IMU driver in the CoreS3 board) | https://github.com/boschsensortec/BMI270_SensorAPI | BSD-3-Clause | Copyright (c) 2023 Bosch Sensortec GmbH | [bosch-bmi270-sensorapi.txt](licenses/bosch-bmi270-sensorapi.txt) |
| ESP-IDF 6.1.0 (framework, drivers, Wi-Fi and BT libraries) | https://github.com/espressif/esp-idf | Apache-2.0 | Copyright Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |

### Third-party code inside ESP-IDF that is linked into the firmware

ESP-IDF's full list is in its [COPYRIGHT page](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/COPYRIGHT.html). The main ones, read from the ESP-IDF 6.1.0 tree:

| Part | License | Copyright | Text |
|---|---|---|---|
| FreeRTOS kernel | MIT | Copyright (C) 2021 Amazon.com, Inc. or its affiliates | [esp-idf__freertos-kernel.txt](licenses/esp-idf__freertos-kernel.txt) |
| lwIP (TCP/IP stack) | BSD-3-Clause | Copyright (c) 2001, 2002 Swedish Institute of Computer Science | [esp-idf__lwip.txt](licenses/esp-idf__lwip.txt) |
| Mbed TLS | Apache-2.0 OR GPL-2.0-or-later (used under Apache-2.0) | The Mbed TLS Contributors | [esp-idf__mbedtls.txt](licenses/esp-idf__mbedtls.txt) |
| wpa_supplicant | BSD-3-Clause | Copyright (c) 2002-2019, Jouni Malinen and contributors | [esp-idf__wpa_supplicant.txt](licenses/esp-idf__wpa_supplicant.txt) |
| SPIFFS | MIT | Copyright (c) 2013-2017 Peter Andersson | [esp-idf__spiffs.txt](licenses/esp-idf__spiffs.txt) |
| FatFs | FatFs license (BSD-style, 1 clause) | Copyright (C) 2025, ChaN | [esp-idf__fatfs.txt](licenses/esp-idf__fatfs.txt) |
| http_parser | MIT | Igor Sysoev; Joyent, Inc. and other Node contributors | [esp-idf__http_parser.txt](licenses/esp-idf__http_parser.txt) |

## 2. ESP-IDF components in the build

All of these are listed as build components in the dj7 `project_description.json` (many are display or board drivers that the CoreS3 build compiles but does not use).

| Component | Version | Link | License | Copyright | Text |
|---|---|---|---|---|---|
| 78/esp-ml307 | 3.7.4 | https://github.com/78/esp-ml307 | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| 78/esp_lcd_nv3023 | 1.0.1 | https://github.com/MakerM0/esp_lcd_nv3023 | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| 78/uart-eth-modem | 0.6.5 | https://github.com/78/uart-eth-modem | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| 78/uart-uhci | 0.3.3 | https://github.com/78/uart-uhci | Apache-2.0 (declared in the component manifest; no license file in the package) | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| 78/xiaozhi-fonts | 2.0.0 | https://github.com/78/noto-fonts | Apache-2.0 (declared in the component manifest; no license file in the package) | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| cube32esp/xpowerslib | 0.3.3 | https://github.com/cube32esp/XPowersLib | MIT | Copyright (c) 2022 lewis he | [cube32esp__xpowerslib.txt](licenses/cube32esp__xpowerslib.txt) |
| esphome/esp-hub75 | 0.3.6 | https://github.com/esphome-libs/esp-hub75 | MIT | Copyright (c) 2025 Stuart Parmenter | [esphome__esp-hub75.txt](licenses/esphome__esp-hub75.txt) |
| espressif2022/esp_emote_assets | 0.1.2 | https://github.com/espressif2022/esp_emote_assets | Apache-2.0 (declared in the component manifest; no license file in the package) | Espressif Systems (Shanghai) Co., Ltd. (per source headers) | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif2022/esp_emote_expression | 1.0.2 | https://github.com/espressif2022/esp_emote_expression | Apache-2.0 (declared in the component manifest; no license file in the package) | Espressif Systems (Shanghai) Co., Ltd. (per source headers) | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif2022/esp_emote_gfx | 3.0.5 | https://github.com/espressif2022/esp_emote_gfx | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. (per source headers) | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif2022/image_player | 1.1.1 | https://github.com/espressif2022/image_player | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. (per source headers) | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/adc_battery_estimation | 0.2.2 | https://github.com/espressif/esp-iot-solution/tree/master/components/sensors/battery_fuel_gauge/adc_battery_estimation | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/bmi270_sensor | 0.1.2 | https://github.com/espressif/esp-iot-solution/ | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/bq27220 | 0.1.3 | https://github.com/espressif/esp-iot-solution/tree/master/components/sensors/battery_fuel_gauge/bq27220 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/button | 4.2.2 | https://github.com/espressif/esp-iot-solution/tree/master/components/button | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/cjson | 1.7.19~2 | https://github.com/espressif/idf-extra-components/tree/master/cjson | MIT | Copyright (c) 2009-2017 Dave Gamble and cJSON contributors | [espressif__cjson.txt](licenses/espressif__cjson.txt) |
| espressif/cmake_utilities | 0.5.0 | https://github.com/espressif/esp-iot-solution/tree/master/tools/cmake_utilities | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/dl_fft | 0.8.0 | https://github.com/espressif/esp-dl/tree/master/esp-dl/tools/dl_fft | MIT (declared in the component manifest; no license file in the package) | see upstream | none shipped upstream |
| espressif/esp-dsp | 1.8.0 | https://github.com/espressif/esp-dsp | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp-sr | 2.4.7 | https://github.com/espressif/esp-sr | Espressif MIT (modified MIT) | Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD | [espressif__esp-sr.txt](licenses/espressif__esp-sr.txt) |
| espressif/esp32-camera | 2.1.8 | https://github.com/espressif/esp32-camera | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_audio_codec | 2.5.0 | https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_codec | Espressif MIT (modified MIT) | Copyright (c) 2025 Espressif Systems (Shanghai) CO., LTD | [espressif__esp_audio_codec.txt](licenses/espressif__esp_audio_codec.txt) |
| espressif/esp_audio_effects | 1.3.0~1 | https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_effects | Espressif MIT (modified MIT) | Copyright (c) 2025 Espressif Systems (Shanghai) CO., LTD | [espressif__esp_audio_effects.txt](licenses/espressif__esp_audio_effects.txt) |
| espressif/esp_cam_sensor | 2.6.0 | https://github.com/espressif/esp-video-components/tree/master/esp_cam_sensor | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_codec_dev | 1.6.2 | https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_image_effects | 1.2.0 | https://github.com/espressif/esp-adf-libs/tree/master/esp_image_effects | Espressif MIT (modified MIT) | Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO.，LTD> | [espressif__esp_image_effects.txt](licenses/espressif__esp_image_effects.txt) |
| espressif/esp_io_expander | 1.2.1 | https://github.com/espressif/esp-bsp/tree/master/components/io_expander/esp_io_expander | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_io_expander_tca9554 | 2.0.3 | https://github.com/espressif/esp-bsp/tree/master/components/io_expander/esp_io_expander_tca9554 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_io_expander_tca95xx_16bit | 2.0.2 | https://github.com/espressif/esp-bsp/tree/master/components/io_expander/esp_io_expander_tca95xx_16bit | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_jpeg | 1.3.2 | https://github.com/espressif/idf-extra-components/tree/master/esp_jpeg | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_axs15231b | 2.1.1 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_axs15231b | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_co5300 | 2.2.0 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_co5300 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_gc9a01 | 2.0.4 | https://github.com/espressif/esp-bsp/tree/master/components/lcd/esp_lcd_gc9a01 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_ili9341 | 2.1.0 | https://github.com/espressif/esp-bsp/tree/master/components/lcd/esp_lcd_ili9341 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_panel_io_additions | 1.0.1~1 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_panel_io_additions | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_spd2010 | 2.0.0~1 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_spd2010 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_st7701 | 2.0.3 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_st7701 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_st77916 | 2.0.2 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_st77916 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_st77922 | 2.1.1 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_st77922 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_st7796 | 1.4.0 | https://github.com/espressif/esp-bsp/tree/master/components/lcd/esp_lcd_st7796 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch | 1.2.1 | https://github.com/espressif/esp-bsp/tree/master/components/lcd_touch/esp_lcd_touch | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch_cst816s | 1.1.2 | https://github.com/espressif/esp-bsp/tree/master/components/lcd_touch/esp_lcd_touch_cst816s | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch_ft5x06 | 1.1.1 | https://github.com/espressif/esp-bsp/tree/master/components/lcd_touch/esp_lcd_touch_ft5x06 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch_gt1151 | 1.1.1 | https://github.com/espressif/esp-bsp/tree/master/components/lcd_touch/esp_lcd_touch_gt1151 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch_gt911 | 1.2.1 | https://github.com/espressif/esp-bsp/tree/master/components/lcd_touch/esp_lcd_touch_gt911 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lcd_touch_st7123 | 1.0.2 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd_touch/esp_lcd_touch_st7123 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_lvgl_port | 2.9.0 | https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_mmap_assets | 1.4.0 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/tools/esp_mmap_assets | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_new_jpeg | 1.0.2 | https://github.com/espressif/esp-adf-libs/tree/master/esp_new_jpeg | Espressif MIT (modified MIT) | Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO.，LTD> | [espressif__esp_new_jpeg.txt](licenses/espressif__esp_new_jpeg.txt) |
| espressif/esp_sccb_intf | 1.0.0 | https://github.com/espressif/esp-video-components/tree/master/esp_sccb_intf | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/esp_video | 2.5.0 | https://github.com/espressif/esp-video-components/tree/master/esp_video | Espressif MIT (modified MIT) | Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD> | [espressif__esp_video.txt](licenses/espressif__esp_video.txt) |
| espressif/freetype | 2.14.3~1 | https://github.com/espressif/idf-extra-components/tree/master/freetype | FreeType License (FTL), dual-licensed with GPL-2.0; used here under the FTL | The FreeType Project (David Turner, Robert Wilhelm, Werner Lemberg) | [espressif__freetype.txt](licenses/espressif__freetype.txt) |
| espressif/gmf_fft | 1.0.0 | https://github.com/espressif/esp-gmf/tree/main/packages/gmf_fft | Espressif MIT (modified MIT) | Copyright (c) 2026 Espressif Systems (Shanghai) CO., LTD | [espressif__gmf_fft.txt](licenses/espressif__gmf_fft.txt) |
| espressif/human_face_detect | 0.5.2 | https://github.com/espressif/esp-dl/tree/master/models/human_face_detect | MIT | Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd. | [espressif__human_face_detect.txt](licenses/espressif__human_face_detect.txt) |
| espressif/i2c_bus | 1.5.2 | https://github.com/espressif/esp-iot-solution/tree/master/components/i2c_bus | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/iot_eth | 1.1.0 | https://github.com/espressif/esp-iot-solution/tree/master/components/iot_eth | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/iot_usbh_cdc | 3.1.0 | https://github.com/espressif/esp-iot-solution/tree/master/components/usb/iot_usbh_cdc | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/iot_usbh_rndis | 0.3.1 | https://github.com/espressif/esp-iot-solution | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/knob | 1.1.0 | https://github.com/espressif/esp-iot-solution/tree/master/components/knob | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/led_strip | 3.0.3 | https://github.com/espressif/idf-extra-components/tree/master/led_strip | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/mqtt | 1.1.0 | https://github.com/espressif/esp-mqtt | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/usb | 1.6.0 | https://github.com/espressif/esp-usb/tree/master/host/usb | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| espressif/usb_host_uvc | 2.5.2 | https://github.com/espressif/esp-usb/tree/master/host/class/uvc/usb_host_uvc | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| laride/heatshrink | 0.4.1 | https://github.com/laride/heatshrink | ISC | Copyright (c) 2013-2015, Scott Vokes; Copyright (c) 2026, Espressif Systems (Shanghai) Co., Ltd. | [laride__heatshrink.txt](licenses/laride__heatshrink.txt) |
| lvgl/lvgl | 9.5.0 | https://lvgl.io/ | MIT | Copyright (c) 2025 LVGL Kft | [lvgl__lvgl.txt](licenses/lvgl__lvgl.txt) |
| m5stack/m5ioe1 | 1.0.9 | https://github.com/m5stack/M5IOE1 | MIT | Copyright (c) 2026 M5Stack Technology CO LTD | [m5stack__m5ioe1.txt](licenses/m5stack__m5ioe1.txt) |
| m5stack/m5pm1 | 1.0.7 | https://github.com/m5stack/M5PM1 | MIT | Copyright (c) 2025 M5Stack Technology CO LTD | [m5stack__m5pm1.txt](licenses/m5stack__m5pm1.txt) |
| tny-robotics/sh1106-esp-idf | 1.0.2 | https://github.com/TNY-Robotics/sh1106-esp-idf | MIT | Copyright (c) 2025 TNY Robotics | [tny-robotics__sh1106-esp-idf.txt](licenses/tny-robotics__sh1106-esp-idf.txt) |
| txp666/otto-emoji-gif-component | 1.4.0 | https://github.com/txp666/otto-emoji-gif-component | MIT | Copyright (c) 2025 小鹏 | [txp666__otto-emoji-gif-component.txt](licenses/txp666__otto-emoji-gif-component.txt) |
| waveshare/custom_io_expander_ch32v003 | 2.0.0 | https://github.com/waveshareteam/Waveshare-ESP32-components | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| waveshare/esp_lcd_sh8601 | 1.0.2 | https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/display/lcd/esp_lcd_sh8601 | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| waveshare/esp_lcd_touch_cst9217 | 1.0.4 | https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/display/touch/esp_lcd_touch_cst9217 | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| wvirgil123/sscma_client | 1.0.4 | https://github.com/Seeed-Studio/SenseCAP-Watcher/tree/main/components/sscma_client | Apache-2.0 | see upstream | [Apache-2.0.txt](licenses/Apache-2.0.txt) |
| 78/esp-wifi-connect | 3.3.1 | https://github.com/78/esp-wifi-connect | MIT | Copyright (c) 2024 Terrence | [78__esp-wifi-connect.txt](licenses/78__esp-wifi-connect.txt) |
| espressif/esp-dl | 3.3.14 | https://github.com/espressif/esp-dl/tree/master/esp-dl | MIT | Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd. | [espressif__esp-dl.txt](licenses/espressif__esp-dl.txt) |
| mooncake | - | bundled in xiaozhi-esp32 components/ | MIT | Copyright (c) 2023 Forairaaaaa | [mooncake.txt](licenses/mooncake.txt) |
| mooncake_log | - | bundled in xiaozhi-esp32 components/ | MIT | Copyright (c) 2024 海底撩 | [mooncake_log.txt](licenses/mooncake_log.txt) |
| smooth_ui_toolkit | - | bundled in xiaozhi-esp32 components/ | MIT | Copyright (c) 2023 Forairaaaaa | [smooth_ui_toolkit.txt](licenses/smooth_ui_toolkit.txt) |

Notes:

- `78/xiaozhi-fonts` packs Noto Sans, Noto Material Symbols and Noto Emoji. The component declares Apache-2.0, but no font license file ships in the package and its repo (github.com/78/noto-fonts) was not reachable, so the license of the font files themselves is **unconfirmed**. Noto fonts are Google's; check their upstream license before reusing the font files.
- `espressif/dl_fft` declares MIT but ships no license file; it comes from esp-dl, whose MIT text is [espressif__esp-dl.txt](licenses/espressif__esp-dl.txt).
- FreeType credit, required by the FTL: Portions of this software are copyright (c) 2026 The FreeType Project (www.freetype.org). All rights reserved. Full FTL text: [espressif__freetype-FTL.txt](licenses/espressif__freetype-FTL.txt).
- "Espressif MIT" is MIT, granted for use on Espressif products. See each file for the exact wording.

## 3. Used alongside, not included in this repo

These run on your own computer or server. Nothing from them is copied into this repo or the firmware. You download them yourself under their own terms.

| Project | Link | License | Checked |
|---|---|---|---|
| xiaozhi-esp32-server (voice server) | https://github.com/xinnan-tech/xiaozhi-esp32-server | MIT | GitHub license file |
| Hermes Agent (Nous Research) | https://github.com/NousResearch/hermes-agent | MIT | GitHub license file |
| gradium/phonon-40m-multilingual (voice) | https://huggingface.co/gradium/phonon-40m-multilingual | CC-BY-NC-4.0, gated, non-commercial | model card |
| gradium/phonon-31m-en | https://huggingface.co/gradium/phonon-31m-en | Apache-2.0 | model card |
| gradium/phonon-voices | https://huggingface.co/gradium/phonon-voices | Apache-2.0 | model card |
| xn-ptts (Phonon TTS server) | https://github.com/gradium-ai/xn-ptts | Apache-2.0 | GitHub license file |
| parakeet-tdt-0.6b-v3 (speech-to-text, by NVIDIA; MLX port) | https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v3 | CC-BY-4.0 | model card |
| whisper-large-v3-turbo (MLX port) | https://huggingface.co/mlx-community/whisper-large-v3-turbo-asr-fp16 | MIT per OpenAI Whisper; the MLX card has no license tag, **unconfirmed** | - |
| Silero VAD | https://github.com/snakers4/silero-vad | MIT | GitHub license file |
| Ollama gemma4 | https://ollama.com/library/gemma4 | see the model page, **unconfirmed** here | - |
| esptool (flashing tool) | https://github.com/espressif/esptool | GPL-2.0 (tool only, not linked into the firmware) | GitHub license file |
| Stack-chan (original project that inspired StackChan) | https://github.com/stack-chan/stack-chan | Apache-2.0 (no code from it is included) | GitHub license file |

## 4. Trademarks

Nous Research, Nous-Girl and Hermes are names of Nous Research. M5Stack and StackChan belong to M5Stack. Espressif and ESP32 belong to Espressif Systems. This project is not affiliated with or endorsed by any of them.
