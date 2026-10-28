# Nous-Girl firmware toolchain (recorded Oct 27, 2026)

All builds, v3.5 through v3.6.7-dj, used ESP-IDF 6.1.0 (CONFIG_IDF_INIT_VERSION="6.1.0"). The checkout had no git metadata, so there is no commit hash. Install with: git clone -b v6.1 --recursive https://github.com/espressif/esp-idf && ./install.sh esp32s3

Python env: idf6.1_py3.13_env (Python 3.13.5)

| Tool | Version |
|---|---|
| xtensa-esp-elf (compiler) | esp-15.2.0_20251204 |
| riscv32-esp-elf | esp-15.2.0_20251204 |
| xtensa-esp-elf-gdb | 17.1_20260402 |
| esp32ulp-elf | 2.38_20240113 |
| esp-rom-elfs | 20241011 |
| esp-clangd | esp-21.1.3_20260408 |
| openocd-esp32 | v0.12.0-esp32-20260703 |
| cmake | 4.0.3 |
| ninja | 1.12.1 |

The build caches (~/.espressif, build/ folders) were left off the Mac on purpose. They regenerate with idf.py build.
