Local copy of espressif/esp-dl 3.3.x for headphone-girl v3.6.1 (overrides managed_components).
1. dl/tool/src/dl_tool.cpp: malloc_aligned/calloc_aligned try PSRAM first unless MALLOC_CAP_INTERNAL is asked.
   Stock MALLOC_CAP_DEFAULT|SIMD picks internal SRAM first on the S3 and starved Wi-Fi/TLS.
2. dl/module/src/dl_module_base.cpp: dual-core worker runtime disabled (runs both halves on the calling task).
   Saves about 4.6 KB static SRAM and keeps esp-dl off the audio core.
3. All .S kernels: ".section .iram1" -> ".section .text.hgdl" (flash). Saves about 10 KB IRAM (shared SRAM on S3).
4. v3.6.3: conv scratch (dl_c_conv_common.hpp conv_scratch_alloc_hg, used by dl_c_conv.cpp,
   dl_c_dwconv.cpp, dl_xtensa_conv.cpp) takes internal SRAM only if 48 KB stays free there; else PSRAM.
