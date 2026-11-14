#include "wifi_board.h"
#include "cores3_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "cores3_py32_led.h"   // 12 颗 RGB（PY32 IO 扩展）
#include "cores3_servo.h"      // 两个飞特总线舵机（UART1）
#include "cores3_espnow_remote.h"  // ★ 官方 K151-R 遥控器（ESP-NOW 接收）
#include "stackchan_sensor.h"
#include "stackchan_geometry_display.h"
#include "hg_agent_tools.h"
#include "hg_settings.h"   // v3 Settings screen  // ★ 官方几何脸（第二套皮肤）
#include "skin_manager.h"      // skin state (server skin); v3.6 power key no longer switches it
#include "mcp_server.h"        // ★ 待机扭头开关（MCP 工具）
#include "settings.h"          // ★ NVS 持久化（开关存这里）

#include <lvgl.h>
#include <esp_log.h>
#include <atomic>
#include <algorithm>
#include <cmath>          // ★ fabsf（遥控器角度变化判定）
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>   // v3.6.5 esp_rom_delay_us
#include "esp_video.h"
#include "hg_lip.h"    // v3.5 vowel lip sync worker
#include "hg_eq_task.h"   // v3.6.7-dj2 DJ equalizer worker
#include "hg_gaze.h"   // v3.5 follow faces
#include "hg_face_dl.h"   // v3.6 esp-dl face detector
#include "hg_cam_gate.h"   // v3.6.2 photos win over follow faces
#include "hg_follow_tool.h"   // v3.6.3 voice switch for follow faces
#include "hg_dj_logic.h"   // v3.6.7-dj tap vs swipe, DJ buttons
#include "hg_meme_overlay.h"   // v3.6.7-dj6 full-screen meme (self.screen.show_image)
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#define TAG "M5StackCoreS3Board"

class Pmic : public Axp2101 {
public:
    // Power Init
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        uint8_t data = ReadReg(0x90);
        data |= 0b10110100;
        WriteReg(0x90, data);
        WriteReg(0x99, (0b11110 - 5));
        WriteReg(0x97, (0b11110 - 2));
        WriteReg(0x69, 0b00110101);
        WriteReg(0x30, 0b111111);
        WriteReg(0x90, 0xBF);
        WriteReg(0x94, 33 - 5);
        WriteReg(0x95, 33 - 5);
    }

    void SetBrightness(uint8_t brightness) {
        brightness = ((brightness + 641) >> 5);
        WriteReg(0x99, brightness);
    }

    // ★ 电源键（用户要求「改成电源键按一下」）
    //
    //   AXP2101 中断寄存器（数据手册 §5.7）：
    //     0x40 = IRQ Enable1   0x41 = IRQ Enable2
    //     0x48 = IRQ Status1   0x49 = IRQ Status2
    //     0x4A = IRQ Status3
    //   其中 PWR_KEY_SHORT_IRQ 在 Status2(0x49) 的 bit3
    //        PWR_KEY_LONG_IRQ  在 Status2(0x49) 的 bit2
    //   ★ 保守做法：两个都读，只认【短按】，且读完清标志
    //
    //   返回 true = 检测到一次短按
    bool ConsumeShortPress() {
        // 使能短按中断（0x42 = IRQ Enable2 的 bit3）
        uint8_t en2 = ReadReg(0x42);
        if ((en2 & 0b00001000) == 0) {
            WriteReg(0x42, (uint8_t)(en2 | 0b00001000));
        }
        // 清长按（避免长按关机时被当成短按）
        uint8_t st2 = ReadReg(0x49);
        if (st2 & 0b00000100) {            // bit2 = 长按
            WriteReg(0x49, st2);           // 写回清
            return false;                  // 长按让 AXP 自己处理（关机）
        }
        if (st2 & 0b00001000) {            // bit3 = 短按
            WriteReg(0x49, st2);           // 写回清
            return true;
        }
        return false;
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Pmic *pmic) : pmic_(pmic) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        pmic_->SetBrightness(target_brightness_);
        brightness_ = target_brightness_;
    }

private:
    Pmic *pmic_;
};

class Aw9523 : public I2cDevice {
public:
    // Exanpd IO Init
    Aw9523(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        // v3.6.5 battery power-on fix: on a cold battery start, switching the
        // SY7088 boost (BOOST_EN = P1_7) fully on at once draws an inrush that
        // browns out the boot. If the boost is not already running, bring the
        // expander up with BOOST_EN low and soft-start it (see BoostSoftStart).
        // Every other bit is exactly what v3.6.4 wrote.
        uint8_t p1_before = 0;
        const bool boost_was_on = TryReadReg(0x03, &p1_before) == ESP_OK &&
                                  (p1_before & kBoostEn) != 0;
        WriteReg(0x02, 0b00000111);  // P0
        WriteReg(0x03, boost_was_on ? kP1On : kP1Off);  // P1 (BOOST_EN low unless already on)
        WriteReg(0x04, 0b00011000);  // CONFIG_P0
        WriteReg(0x05, 0b00001100);  // CONFIG_P1
        WriteReg(0x11, 0b00010000);  // GCR P0 port is Push-Pull mode.
        WriteReg(0x12, 0b11111111);  // LEDMODE_P0
        WriteReg(0x13, 0b11111111);  // LEDMODE_P1
        if (boost_was_on) {
            // Warm restart (esp_restart / OTA): the expander kept power, so the
            // boost is already up. Don't drop it; leave BOOST_EN high as before.
            ESP_LOGI("AW9523", "BOOST_EN already on (warm restart), soft-start skipped, P1=0x%02X", kP1On);
        } else {
            BoostSoftStart();
        }
    }

    static constexpr uint8_t kBoostEn = 0b10000000;   // P1_7
    static constexpr uint8_t kP1On = 0b10001111;      // v3.6.4 P1 value
    static constexpr uint8_t kP1Off = kP1On & ~kBoostEn;

    // 20 short on-pulses (150 us) with shrinking off-gaps, ~150 ms of gaps plus
    // I2C time, then BOOST_EN stays high. Pattern from
    // https://zenn.dev/maruhana/articles/stackchan-battery-boot-debug
    void BoostSoftStart() {
        static constexpr uint32_t kOffUs[20] = {30000, 25000, 20000, 16000, 13000, 10000, 8000,
                                                6000,  5000,  4000,  3000,  2500,  2000,  1500,
                                                1200,  1000,  800,   600,   400,   300};
        const int64_t t0 = esp_timer_get_time();
        for (uint32_t off_us : kOffUs) {
            WriteReg(0x03, kP1On);
            esp_rom_delay_us(150);
            WriteReg(0x03, kP1Off);
            esp_rom_delay_us(off_us);
        }
        WriteReg(0x03, kP1On);
        const int ramp_ms = (int)((esp_timer_get_time() - t0) / 1000);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI("AW9523", "BOOST_EN soft-start done: 20 pulses in %d ms, then on (P1=0x%02X)",
                 ramp_ms, kP1On);
    }

    void ResetAw88298() {
        ESP_LOGI(TAG, "Reset AW88298");
        WriteReg(0x02, 0b00000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void ResetIli9342() {
        ESP_LOGI(TAG, "Reset IlI9342");
        WriteReg(0x03, 0b10000001);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        // ★ 用 Try（探测失败不该 abort；原 ReadReg 会 abort）
        uint8_t chip_id = 0;
        if (TryReadReg(0xA3, &chip_id) == ESP_OK) {
            ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        } else {
            ESP_LOGW(TAG, "FT6336 探测失败（触摸可能不可用，但不影响启动）");
        }
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        // ★★★ 修「点屏幕就重启」（addr2line 定位到本函数 L143）
        //   崩溃链：UpdateTouchPoint → I2cDevice::ReadRegs
        //           → _esp_error_check_failed → abort
        //   真因：原实现用 ReadRegs（内含 ESP_ERROR_CHECK），
        //         而 I2C 读【会偶发失败】（Fairy 皮肤持续解 GIF 时总线繁忙）
        //         ⇒ 一次超时就把可恢复错误当致命 ⇒ abort ⇒ 重启
        //
        //   修法（健壮化，不是"少读几次"）：
        //     ① 用不 abort 的 TryReadRegs
        //     ② 失败：限频打日志 + 连续 5 次复位 I2C 总线
        //     ③ ★ 清空触摸点 —— 当作"没按"
        //        不能让旧坐标"粘住"，否则用户以为还在按
        esp_err_t err = TryReadRegs(0x02, read_buffer_, 6);
        if (err != ESP_OK) {
            fail_count_++;
            if (fail_count_ <= 3 || (fail_count_ % 100) == 0) {
                ESP_LOGW(TAG, "FT6336 读取失败(%s)，累计 %d 次",
                         esp_err_to_name(err), fail_count_);
            }
            if (fail_count_ >= 5) {
                ResetBus("FT6336 连续读取失败");
                fail_count_ = 0;
            }
            tp_.num = 0;      // ★ 当作没触摸，避免坐标粘住
            tp_.x = -1;
            tp_.y = -1;
            last_ok_ = false;   // v3.6.7-dj5: the gesture tracker treats this as "no news"
            ++read_fails_;
            return;
        }
        last_ok_ = true;
        fail_count_ = 0;
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    inline const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }
    bool LastReadOk() const { return last_ok_; }      // v3.6.7-dj5
    uint32_t ReadFails() const { return read_fails_; }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
    // ★ 连续 I2C 读失败计数（≥5 次触发总线复位）
    int fail_count_ = 0;
    bool last_ok_ = true;
    uint32_t read_fails_ = 0;
};

class M5StackCoreS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Aw9523* aw9523_;
    Ft6336* ft6336_;
    LcdDisplay* display_;
    EspVideo* camera_;
    esp_timer_handle_t touchpad_timer_;
    PowerSaveTimer* power_save_timer_;
    CoreS3Py32Led* fairy_led_ = nullptr;   // 12 颗 RGB（PY32 IO 扩展）
    CoreS3Servo* servo_ = nullptr;         // yaw/pitch 舵机
    // ★ 官方 K151-R 遥控器（ESP-NOW 广播收包）
    //   遥控器 = M5StickC-Plus + 摇杆帽，30ms 一包 8 字节
    //   ⛔ 设备侧不改 WiFi 信道（peer.channel = 0 跟随）⇒ 与服务器链路无关
    CoreS3EspNowRemote* espnow_ = nullptr;
    TaskHandle_t espnow_task_ = nullptr;
    // ★ 几何脸显示（display_ 的实际类型，用于切皮肤）
    //   display_ 保持 LcdDisplay* 以复用框架接口，这里存一份具体类型
    // ★ 传感器（摸头 + 摇晃）与待机小动作任务
    StackChanSensor* sensor_ = nullptr;
    TaskHandle_t sensor_task_ = nullptr;
    TaskHandle_t idle_motion_task_ = nullptr;
    // ★ 舵机跟随 / 视线跟踪（用户要求：「跟踪我的脸，舵机跟随我的脸移动，默认关闭」）
    //   实现思路：摄像头出帧 → 人脸检测 → 人脸中心相对画面的偏移 → 舵机 yaw/pitch 跟随
    //   ⚠️ 代价：摄像头常开 + 每帧检测要跑模型（CoreS3 跑得动，但会占 CPU/发热）
    //   ⇒ 默认 false（用户明确要求），菜单里开关，存 NVS（namespace "fairy_gaze"）
    std::atomic<bool> gaze_follow_{false};
    std::atomic<bool> gaze_debug_{false};   // v3.6.2 Settings > Follow faces debug
    // v3.6.1 follow faces memory budget
    static constexpr size_t kGazeFrameCap = 320 * 240 * 2;
    static constexpr size_t kGazeMinSram = 40 * 1024;       // skip a detect below this
    static constexpr int64_t kGazeStartDelayMs = 4000;       // v3.6.3: let AFE finish after idle
    static constexpr size_t kGazeSpeakMinSram = 56 * 1024;  // follow while speaking only above this
    uint8_t* gaze_frame_ = nullptr;
    std::atomic<int> gaze_sx_{1}, gaze_sy_{1};
    TaskHandle_t gaze_task_ = nullptr;
    std::atomic<int64_t> gaze_last_move_ms_{0};   // v3.5: idle motion yields to tracking
    int64_t face_restore_at_ = 0;                 // v3.5: tap faces go back after 3 s
    uint32_t gaze_detect_cnt_ = 0;     // 限频用（不是每帧都检测）
    bool last_speaking_ = false;

    // ★★★ LVGL 输入设备（indev）—— 修「菜单点不动」
    //   真因：CoreS3 从没把 FT6336 注册成 LVGL 输入设备
    //        ⇒ LVGL 收不到点击 ⇒ 任何 LVGL 按钮都点不动
    //   read_cb 在 LVGL 任务里跑，绝不能做 I2C ⇒ 只读下面 3 个缓存
    lv_indev_t* lvgl_indev_ = nullptr;
    std::atomic<bool> lvgl_touch_pressed_{false};
    std::atomic<int> lvgl_touch_x_{-1};
    std::atomic<int> lvgl_touch_y_{-1};

    // ★ 本地菜单（不依赖服务器）
    std::atomic<bool> menu_open_{false};

    // ★ 菜单创建是否已派发（lv_async_call 只派发一次）
    std::atomic<bool> menu_build_dispatched_{false};
    std::atomic<int> menu_sel_{0};      // 0=切皮肤 1=转头 2=关闭
    std::atomic<int64_t> menu_open_ms_{0};
    std::atomic<uint32_t> pwr_poll_cnt_{0};
    // ★ 本地菜单的 LVGL 对象（真·可点菜单）
    lv_obj_t* menu_root_ = nullptr;      // 半透明遮罩
    lv_obj_t* menu_card_ = nullptr;      // 中央卡片
    lv_obj_t* menu_btns_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* menu_txts_[3] = {nullptr, nullptr, nullptr};
    // ★ 待机扭头开关（NVS 持久化，默认开）
    // ★ 用户要求（2026-09-19）：「待机转头请默认调成关闭」
    //   原因：默认开着会一直自己转头，用户觉得吵/费舵机
    //   ⇒ 默认 false，需要的人在菜单/控制台里打开（存 NVS）
    std::atomic<bool> auto_motion_{false};

    // ★★ 反应动作的「基准角度 + 恢复计时」
    //   病根（用户反馈三连）：摸头/甩晕/待机 都写死绝对角度（pitch 45°/55°），
    //   而不是相对【当前角度】做温和增量 ⇒ 只要开机默认姿势不是 45°，
    //   这些动作就会把俯仰猛拽到 45/55，看着就像「第一下就顶到最高」；
    //   而且摸头做完【不还原】，头一直抬着。
    //   现在照官方 modifiers/head_pet.h：
    //     记下基准 → 加随机小增量 → 平滑过去 → 3 秒后平滑还原。
    float pet_base_yaw_ = 0.0f;
    float pet_base_pitch_ = kLevelPitchDeg;
    int64_t pet_restore_at_ = 0;            // 0 = 无需恢复
    std::atomic<bool> motion_busy_{false};  // 反应动作进行中（待机小动作让路）
    // v3.4: shake/dizzy lifecycle. The servo moves themselves jolt the IMU, so
    // shake detection is ignored while the head moves and for 600 ms after;
    // the dizzy face ends once the body has been still for 1.5 s.
    std::atomic<int64_t> servo_quiet_at_ms_{0};
    std::atomic<bool> dizzy_active_{false};
    int64_t dizzy_since_ms_ = 0;
    static constexpr int64_t kServoSettleMs = 600;
    static constexpr int64_t kDizzyStillMs = 1500;
    static constexpr int64_t kDizzyMaxMs = 15000;
    void EndMotion() {
        servo_quiet_at_ms_.store(esp_timer_get_time() / 1000 + kServoSettleMs);
        motion_busy_.store(false);
    }
    // Back to the face for the current state (called from the sensor task).
    void EndDizzy(const char* why) {
        if (!dizzy_active_.exchange(false)) return;
        auto st = Application::GetInstance().GetDeviceState();
        const char* face = st == kDeviceStateSpeaking ? "speaking"
                         : st == kDeviceStateListening ? "listening" : "neutral";
        ESP_LOGI(TAG, "dizzy over (%s) -> %s", why, face);
        if (auto* d = Board::GetInstance().GetDisplay()) d->SetEmotion(face);
    }
    StackChanGeometryDisplay* geometry_display_ = nullptr;

    void InitializePowerSaveTimer() {
        // 熄屏 300 秒（保留暗屏省电），但【永不自动关机】
        // seconds_to_shutdown = -1 表示禁用关机回调（框架语义，见 power_save_timer.h）
        power_save_timer_ = new PowerSaveTimer(-1, 300, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(10);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void I2cDetect() {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeAw9523() {
        ESP_LOGI(TAG, "Init AW9523");
        aw9523_ = new Aw9523(i2c_bus_, 0x58);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ══════════════════════════════════════════════════════════════
    // ★ 本地菜单（用户需求：「不依赖服务器的设备上的UI菜单」）
    //
    //   理由：在外面连不上自建服务器时，控制台和语音都用不了
    //        ⇒ 设备本地必须有个入口
    //
    //   入口：① 屏幕长按 ≥500ms  ② 电源键短按
    //   选择：电源键短按 → 轮转
    //   确认：屏幕短按
    // ══════════════════════════════════════════════════════════════
    static constexpr int kMenuItems = 3;
    static constexpr int64_t kMenuTimeoutMs = 8000;   // 8 秒无操作自动关

    bool MenuCanOpen() {
        // 只在空闲时弹（说话/聆听时语音优先，避免打架）
        auto st = Application::GetInstance().GetDeviceState();
        return st == kDeviceStateIdle || st == kDeviceStateStarting;
    }

    // ══════════════════════════════════════════════════════════════
    // ★ 本地菜单 —— 真·可点 UI（用户要求：「浮现出一个手指能选择的菜单，
    //   直接用手点击相应位置即可」）
    //
    //   为什么不用 ShowNotification：
    //     那是文字提示条，不能点，还得靠电源键换项 —— 用户说"太简陋"
    //
    //   现在：屏幕中央弹一个卡片，三个大按钮竖排，手指直接点目标项
    // ══════════════════════════════════════════════════════════════

    // 按钮回调：统一入口，靠 user_data 区分第几项
    static void MenuBtnCb(lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        // ★ 用 Board::GetInstance()（实测存在，board.h L62）
        //   ⛔ 别用 Application::GetBoard() —— 那个 API 不存在（查过了）
        auto& brd = (M5StackCoreS3Board&)Board::GetInstance();
        brd.MenuActivate(idx);
    }

    // 执行第 idx 项
    void MenuActivate(int idx) {
        ESP_LOGI(TAG, "菜单点击第 %d 项", idx);
        if (idx == 0) {
            // ① 待机转头 开/关（本地 NVS）
            bool now = auto_motion_.load();
            auto_motion_.store(!now);
            SaveAutoMotionToNvs(!now);
            ESP_LOGI(TAG, "菜单：待机转头 → %s", now ? "关" : "开");
            MenuRefreshLabels();
        } else if (idx == 1) {
            // ② 舵机跟随（视线跟踪）开/关（本地 NVS）
            //    ★ 用户要求「默认关闭」—— 见 gaze_follow_ 初值 false
            bool now = gaze_follow_.load();
            SetFollowFaces(!now);   // v3.6.3: same path as Settings and the voice tool
            ESP_LOGI(TAG, "菜单：舵机跟随 → %s", now ? "关" : "开");
            MenuRefreshLabels();
        } else {
            // ③ 关闭菜单
            MenuClose();
        }
    }

    void MenuSetText(int i, const char* s) {
        // ★ 注意：本函数必须由【已持锁】的调用者使用（避免嵌套加锁死锁）
        if (i >= 0 && i < 3 && menu_txts_[i] != nullptr) {
            lv_label_set_text(menu_txts_[i], s);
        }
    }

    // 无锁版（调用者必须已持有 LVGL 锁）
    void MenuRefreshLabelsUnlocked() {
        char b0[64], b1[64];
        snprintf(b0, sizeof(b0), "待机转头\n(%s)",
                 auto_motion_.load() ? "开" : "关");
        snprintf(b1, sizeof(b1), "舵机跟随\n(%s)",
                 gaze_follow_.load() ? "开" : "关");
        if (menu_txts_[0]) lv_label_set_text(menu_txts_[0], b0);
        if (menu_txts_[1]) lv_label_set_text(menu_txts_[1], b1);
        if (menu_txts_[2]) lv_label_set_text(menu_txts_[2], "关闭菜单");
    }

    // 带锁版（给非 LVGL 上下文的调用者）
    void MenuRefreshLabels() {
        if (display_ == nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        MenuRefreshLabelsUnlocked();
    }

    // ★★★ 让菜单 UI 在【LVGL 任务】里创建（★ 真正修「点屏重启」）
    //
    //   ★ 我上一版判断错了方向（虽然方向对但放错了位置）：
    //     · LVGL 对象必须在【LVGL 任务上下文】创建 —— 这点对
    //     · 但 ApplyBootSkin() 是在【板卡初始化流程】（main 任务）里调的
    //     · 板卡初始化 ≠ LVGL 任务 ⇒ BuildMenuAtInit() 仍在错误上下文
    //     ⇒ LVGL 内部断言失败 ⇒ abort() ⇒ 反复重启
    //       （抓到的崩溃：abort() at PC 0x403866fa，日志停在
    //         "初始化菜单 UI（在正确的 LVGL 上下文）" 之后）
    //
    //   ★ 正解：lv_async_call() 把创建操作【派发到 LVGL 任务】
    //     —— 这是 LVGL 官方推荐的跨任务操作方式。
    //
    //   流程：
    //     ApplyBootSkin（main 任务）→ 派发 → LVGL 任务执行 → 建菜单
    void BuildMenuAtInit() {
        if (display_ == nullptr) {
            ESP_LOGW(TAG, "display_ 为空，菜单不建");
            return;
        }
        if (menu_build_dispatched_.exchange(true)) {
            return;   // 只派发一次
        }
        ESP_LOGI(TAG, "派发菜单创建到 LVGL 任务（lv_async_call）");
        // ★ user_data 传 this —— 回调里再拿回来
        lv_async_call(&M5StackCoreS3Board::MenuBuildAsync, this);
    }

    // ★ lv_async_call 的回调：运行在【LVGL 任务】里，可安全创建对象
    static void MenuBuildAsync(void* user_data) {
        auto* self = static_cast<M5StackCoreS3Board*>(user_data);
        if (self == nullptr) {
            return;
        }
        ESP_LOGI(TAG, "在 LVGL 任务里创建菜单 UI");
        self->MenuBuild();        // 内部自带 DisplayLockGuard
        ESP_LOGI(TAG, "菜单 UI 初始化完成 root=%p", (void*)self->menu_root_);
    }

    void MenuBuild() {
        if (menu_root_ != nullptr) {
            return;                  // ★ 幂等：已建过就不重复建
        }
        if (display_ == nullptr) {
            ESP_LOGW(TAG, "MenuBuild: display_ 为空");
            return;
        }
        // ★★★ 必须加锁！LVGL 非线程安全，本函数在 esp_timer 线程里被调用
        //     （用户实测：不加锁 ⇒ 菜单点不动、关不掉）
        DisplayLockGuard lock(display_);
        lv_obj_t* scr = lv_screen_active();
        if (scr == nullptr) {
            return;
        }
        // 半透明遮罩（吃掉下层点击，防误触）
        // ★ 用板卡的 DISPLAY_WIDTH/HEIGHT 宏（config.h 里定义）
        //   ⛔ 别用 width_/height_ —— 那是 Display 类的成员，板卡里没有
        menu_root_ = lv_obj_create(scr);
        lv_obj_set_size(menu_root_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_center(menu_root_);
        // ★ 遮罩：极淡的黑（用户要"黑底白字"但别把脸全挡住）
        lv_obj_set_style_bg_color(menu_root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_root_, LV_OPA_30, 0);
        lv_obj_set_style_border_width(menu_root_, 0, 0);
        lv_obj_set_style_radius(menu_root_, 0, 0);
        lv_obj_remove_flag(menu_root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_CLICKABLE);   // 挡住下层
        // ★★★ 关键：菜单【建好就隐藏】！
        //   否则它是一个【全屏 + CLICKABLE】的遮罩，
        //   会把状态栏的 ☰ 按钮整个盖住 ⇒ 用户「☰ 可见但点不动」
        //   （半透明 40% ⇒ 视觉上还能看到 ☰，但点击全被它吃掉）
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);

        // ★ 抽屉（用户要求：「点开菜单，就从侧边滑出菜单选项」）
        //   贴右边缘、竖长条，宽 150
        menu_card_ = lv_obj_create(menu_root_);
        lv_obj_set_size(menu_card_, 160, DISPLAY_HEIGHT - 20);
        lv_obj_align(menu_card_, LV_ALIGN_RIGHT_MID, -6, 0);
        // ★★ 用户要求：「直接给我做成黑底白字的样式」
        //   ⇒ 纯黑底（不透明）+ 白字 + 平面按钮（无圆角无边框无阴影）
        lv_obj_set_style_bg_color(menu_card_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_card_, LV_OPA_COVER, 0);   // 不透明纯黑
        lv_obj_set_style_border_width(menu_card_, 1, 0);
        lv_obj_set_style_border_color(menu_card_, lv_color_hex(0x444444), 0);
        lv_obj_set_style_radius(menu_card_, 0, 0);              // 直角，更"平面"
        lv_obj_set_style_pad_all(menu_card_, 10, 0);
        lv_obj_remove_flag(menu_card_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(menu_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(menu_card_, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 三个大按钮（高 52，手指好点）
        for (int i = 0; i < 3; i++) {
            lv_obj_t* btn = lv_button_create(menu_card_);
            lv_obj_set_size(btn, 140, 56);   // 大一点，手指好点
            // ★★ 用户要求：黑底白字、平面
            //   底 = 黑，字 = 白，按下 = 反色（白底黑字）作为反馈
            lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 0, 0);              // 直角
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 4, 0);
            lv_obj_add_event_cb(btn, MenuBtnCb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)i);
            lv_obj_t* lb = lv_label_create(btn);
            lv_obj_set_style_text_color(lb, lv_color_white(), 0);
            // 按下时反色（白底 ⇒ 黑字）
            lv_obj_set_style_text_color(lb, lv_color_black(),
                                        LV_STATE_PRESSED);
            lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(lb);
            menu_btns_[i] = btn;
            menu_txts_[i] = lb;
        }
        MenuRefreshLabels();
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);   // 默认隐藏
        ESP_LOGI(TAG, "菜单 UI 已建好");
    }

    void MenuOpen() {
        // ★ 用户决定：菜单功能停用（2026-09-19）
        //   ⇒ 任何路径都开不了菜单（语音/按钮/热区）
        ESP_LOGI(TAG, "菜单已停用，忽略打开请求");
        return;
        if (menu_open_.load()) {
            return;                  // 已开，忽略（点按钮才算）
        }
        if (!MenuCanOpen()) {
            ESP_LOGI(TAG, "菜单：非空闲状态，忽略");
            return;
        }
        // ★★★ 修「点屏幕就重启」：
        //   本函数在 esp_timer 线程被调用，【绝不能在这里创建 LVGL 对象】。
        //   菜单 UI 已在初始化时建好（见 BuildMenuAtInit）。
        //   若还没建（初始化失败），直接拒绝 —— 不要再建，否则设备崩。
        if (menu_root_ == nullptr) {
            ESP_LOGW(TAG, "菜单对象未初始化，忽略（避免在错误上下文建 LVGL 对象）");
            return;
        }
        {
            // ★ 运行时只做「显示」—— 加锁即可（不涉及对象创建）
            DisplayLockGuard lock(display_);
            MenuRefreshLabelsUnlocked();
            lv_obj_clear_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(menu_root_);
            // ★★★ 菜单遮罩是【全屏 CLICKABLE】，把 ☰ 一起提到最前，
            //   否则菜单打开后 ☰ 被盖住 ⇒ 关不掉菜单
            if (display_ != nullptr) {
                display_->RaiseMenuButton();
            }
        }
        menu_open_.store(true);
        menu_open_ms_.store(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "本地菜单已打开");
    }

    void MenuClose() {
        if (!menu_open_.load()) {
            return;
        }
        menu_open_.store(false);
        if (menu_root_ != nullptr && display_ != nullptr) {
            DisplayLockGuard lock(display_);   // ★ 加锁
            lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "本地菜单已关闭");
    }

    // 菜单超时检查（由轮询定时器调）
    void MenuTick() {
        if (!menu_open_.load()) {
            return;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - menu_open_ms_.load() > kMenuTimeoutMs) {
            ESP_LOGI(TAG, "菜单超时自动关闭");
            MenuClose();
        }
    }

    // ★★★ 驱动 avatar 的每帧更新（装饰器动画 + 自动销毁）
    //   官方：stackchan.h L136 `_avatar->update();`
    //   ⛔ 不调 ⇒ 爱心/晕眩静止 + 永不过期盖住表情（用户实测）
    void PollAvatarTick() {
        if (geometry_display_ == nullptr) {
            return;
        }
        geometry_display_->AvatarTick();
    }

    // ★ 说话嘴动画驱动（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    //   读设备状态 → 告诉几何脸层"在说话/说完了"
    //   TickMouth 内部自己限频到 180ms（与官方 SpeakingModifier 一致）
    void PollSpeaking() {
        if (geometry_display_ == nullptr) {
            return;
        }
        auto st = Application::GetInstance().GetDeviceState();
        bool speaking = (st == kDeviceStateSpeaking);
        if (speaking != last_speaking_) {
            last_speaking_ = speaking;
            geometry_display_->SetSpeaking(speaking);
        }
        geometry_display_->TickMouth();
    }

    // 电源键轮询（挂在触摸轮询定时器里，20ms 一次 ⇒ 每 5 次查一遍 = 100ms）
    void PollPowerKey() {
        uint32_t c = pwr_poll_cnt_.load() + 1;
        pwr_poll_cnt_.store(c);
        if (c % 5 != 0) {
            return;              // 100ms 查一次足够（避免 I2C 太频繁）
        }
        if (pmic_ == nullptr) {
            return;
        }
        if (pmic_->ConsumeShortPress()) {
            // v3.6: short press = switch only the face (girl <-> classic),
            // the same face-only path as self.face.style. Never touches the
            // server, the OTA URL or the skin, and never reboots.
            if (menu_open_.load()) {
                MenuClose();
            }
            // run on the main loop, not in this timer callback (NVS + LVGL)
            Application::GetInstance().Schedule([this]() {
                bool classic = !FaceStyleClassic();
                std::string err;
                if (ApplyFaceStyle(classic, &err)) {
                    ESP_LOGI(TAG, "power key short press -> face %s", classic ? "classic" : "girl");
                } else {
                    ESP_LOGW(TAG, "power key short press: %s", err.c_str());
                    if (auto* d = Board::GetInstance().GetDisplay()) {
                        d->ShowNotification("Classic face only on this server", 2500);
                    }
                }
            });
        }
    }

    // v3.6.7-dj5: every screen gesture goes through hg_gesture::Tracker
    // (hg_dj_logic.h). Nothing happens on touch-down; the gesture is decided
    // after the finger has gone, so a swipe can never start as an interrupt.
    // A failed I2C read or a brief "no finger" frame no longer ends the touch.
    // One log line per gesture says what was seen and what was done.
    void PollTouchpad() {
        static hg_gesture::Tracker tracker;
        ft6336_->UpdateTouchPoint();
        auto& touch_point = ft6336_->GetTouchPoint();
        const bool ok = ft6336_->LastReadOk();
        const int64_t now_ms = esp_timer_get_time() / 1000;

        // ★ 同步给 LVGL 输入设备（缓存坐标，read_cb 只读缓存）
        //   v3.6.7-dj5: a failed read keeps the last state (no fake release)
        if (ok) {
            bool pressed = (touch_point.num > 0);
            lvgl_touch_pressed_.store(pressed);
            if (pressed) {
                lvgl_touch_x_.store(touch_point.x);
                lvgl_touch_y_.store(touch_point.y);
            }
        }
        // v3.1: any finger on the glass keeps Settings open (scrolls too)
        if (ok && touch_point.num > 0) hg_settings::NoteActivity();

        const bool dj = geometry_display_ != nullptr && geometry_display_->IsDjVisible();
        hg_gesture::Gesture g;
        if (!tracker.Poll(now_ms, ok, touch_point.num, touch_point.x, touch_point.y, dj, &g)) return;

        const char* what = "";
        // Settings / menu handle their own taps through LVGL.
        // v3.6.7-dj6: while a meme is up, a tap (or any gesture) only hides it.
        if (hg_meme::IsShown() && g.kind != hg_gesture::kNone) {
            hg_meme::Dismiss("tap");
            what = "hide meme";
        }
        else if (hg_settings::IsOpen()) what = "settings open (LVGL)";
        else if (menu_open_.load()) what = "menu open (LVGL)";
        else switch (g.kind) {
            case hg_gesture::kHold:            // hold 1.5 s still = Settings (unchanged)
                hg_settings::RequestOpen();
                what = "open settings";
                break;
            case hg_gesture::kSwipeNext:
            case hg_gesture::kSwipePrev: {
                // songs: while music plays, or on the DJ screen
                bool music = hg_agent::MusicPlaying();
                if (music || dj) {
                    if (g.kind == hg_gesture::kSwipeNext) { hg_agent::SwipeNext(); what = "next song"; }
                    else { hg_agent::SwipePrev(); what = "previous song"; }
                } else {
                    what = "ignored, no music";
                }
                break;
            }
            case hg_gesture::kSwipeUp:
            case hg_gesture::kSwipeDown:
                // v3.6.7-dj5: up / down do nothing (rave mode is voice only:
                // self.dj.mode rave_on / rave_off). They are still recognized
                // so a vertical swipe is never taken for a tap.
                what = "ignored (rave is voice only)";
                break;
            case hg_gesture::kTap: {
                // 短触（菜单没开）⇒ 原行为：状态栏 + 聊天 (tap-to-interrupt)
                if (display_ != nullptr) display_->ShowTopBarTemporarily();
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                    what = "wifi config";
                } else {
                    app.ToggleChatState();
                    what = "interrupt / chat";
                }
                break;
            }
            default: what = "ignored"; break;
        }
        ESP_LOGI(TAG, "touch: start %d,%d end %d,%d dt %lld ms max %d px, %d samples, %d drops, %d joins, "
                      "i2c fails %lu%s -> %s: %s",
                 g.x0, g.y0, g.x1, g.y1, (long long)g.dur_ms, g.max_dist, g.samples, g.drops, g.joins,
                 (unsigned long)ft6336_->ReadFails(), g.dj ? " [DJ]" : "",
                 hg_gesture::KindName(g.kind), what);
    }

    // ══════════════════════════════════════════════════════════════
    // ★★★ LVGL 输入设备（indev）—— 菜单「点不动」的真因
    //
    //   问题：CoreS3 板卡从没把 FT6336 注册成 LVGL 输入设备。
    //         FT6336 只被 PollTouchpad() 读（供小智自身逻辑），
    //         而 LVGL 完全不知道屏幕被点过
    //         ⇒ 任何 LVGL 按钮（我的菜单、以后加的 UI）都点不动。
    //
    //   对比其他板卡：都有 lvgl_port_add_touch() 或手写 lv_indev_create()
    //
    //   修法：建一个 LVGL 指针设备，read_cb 只用【缓存坐标】
    //     ⚠️ read_cb 在 LVGL 任务上下文里跑，绝不能做 I2C 读
    //        ⇒ 缓存由 PollTouchpad()（20ms）刷新
    // ══════════════════════════════════════════════════════════════
    // ★ read_cb 在 LVGL 任务上下文里被调用 ⇒ 必须加 LVGL 锁
    //   （官方 hal.cpp L286/300 用 hal_bridge::lock()/unlock() 包住）
    static void LvglTouchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
        M5StackCoreS3Board* self =
            (M5StackCoreS3Board*)lv_indev_get_user_data(indev);
        if (self == nullptr) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        // ★ 只读缓存（无 I2C —— I2C 会阻塞 LVGL 任务）
        if (self->lvgl_touch_pressed_.load()) {
            data->state   = LV_INDEV_STATE_PRESSED;
            data->point.x = self->lvgl_touch_x_.load();
            data->point.y = self->lvgl_touch_y_.load();
            // ★ 诊断日志：确认 LVGL 真在收触摸（限频 1 秒，防刷屏）
            //   与 PollTouchpad 的日志对比：
            //     · 两条都有 ⇒ 坐标正常，问题在 LVGL 命中判定
            //     · 只有 PollTouchpad 有 ⇒ indev 没接进 LVGL 输入循环
            static int64_t s_last_touchdbg = 0;
            int64_t now_dbg = esp_timer_get_time() / 1000;
            if (now_dbg - s_last_touchdbg > 1000) {
                s_last_touchdbg = now_dbg;
                ESP_LOGI("TOUCHDBG", "LVGL read_cb PRESSED (%d,%d)",
                         (int)data->point.x, (int)data->point.y);
            }
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    void InitializeLvglInput() {
        if (lvgl_indev_ != nullptr) {
            return;
        }
        // ★ 对齐官方 hal.cpp L303-317：
        //   · 注册过程包在 LVGL 锁内（disply_lvgl_lock/unlock）
        //   · set_group / set_display 两句不能少
        //     （缺 set_group ⇒ 键盘/编码器类事件收不到；
        //       缺 set_display ⇒ 多显示器时路由不到正确的屏）
        if (display_ != nullptr) {
            DisplayLockGuard lock(display_);
            lvgl_indev_ = lv_indev_create();
            lv_indev_set_type(lvgl_indev_, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(lvgl_indev_, LvglTouchReadCb);
            lv_indev_set_user_data(lvgl_indev_, this);
            // ★ 官方同款（hal.cpp L313-314）
            lv_indev_set_group(lvgl_indev_, lv_group_get_default());
            lv_indev_set_display(lvgl_indev_, lv_display_get_default());
        } else {
            lvgl_indev_ = lv_indev_create();
            lv_indev_set_type(lvgl_indev_, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(lvgl_indev_, LvglTouchReadCb);
            lv_indev_set_user_data(lvgl_indev_, this);
        }
        ESP_LOGI(TAG, "LVGL 输入设备已注册（触摸可用）");
    }

    void InitializeFt6336TouchPad() {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, 0x38);
        
        // 创建定时器，20ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                M5StackCoreS3Board* board = (M5StackCoreS3Board*)arg;
                board->PollTouchpad();
                board->PollPowerKey();   // ★ 电源键轮询（内部 100ms 一次）
                board->MenuTick();       // ★ 菜单超时自动关闭
                board->PollSpeaking();   // ★ 说话时驱动嘴开合
                board->PollAvatarTick(); // ★★★ 驱动 avatar 每帧更新（装饰器动画+销毁）
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_37;
        // headphone-girl patch: GPIO35 doubles as SD MISO (and LCD D/C).
        // Routing it as MISO here lets hg_agent mount the microSD later;
        // the LCD driver re-claims the pin as an output right after.
        buscfg.miso_io_num = GPIO_NUM_35;
        buscfg.sclk_io_num = GPIO_NUM_36;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
        hg_agent::ParkSdChipSelect();   // keep the SD card quiet on the shared bus
    }

    void InitializeIli9342Display() {
        ESP_LOGI(TAG, "Init IlI9342");

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_3;
        io_config.dc_gpio_num = GPIO_NUM_35;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        aw9523_->ResetIli9342();

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        // ★ 用自定义显示类（含官方几何脸，第二套皮肤）
        //   它继承 SpiLcdDisplay，多的能力是 SetGeometryVisible()
        auto* geom = new StackChanGeometryDisplay(
            panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = geom;
        geometry_display_ = geom;
        // ★ 注入开机皮肤钩子：SetupUI 建好 UI 后回调 ApplyBootSkin()
        geom->SetBootSkinHook([this]() { ApplyBootSkin(); });
        ESP_LOGI(TAG, "using StackChanGeometryDisplay");
    }

     void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new EspVideo(video_config);
    }

public:
    M5StackCoreS3Board() {
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeAxp2101();
        InitializeAw9523();
        I2cDetect();
        InitializeSpi();
        InitializeIli9342Display();
        InitializeCamera();
        InitializeFt6336TouchPad();
        InitializeLvglInput();   // ★ LVGL 输入设备（否则按钮点不动）
        InitializeFairyRgb();
        InitializeServo();
        InitializeSensors();   // ★ 传感器（摸头/摇晃）+ 待机小动作
        // headphone-girl patch: SD / profile / face-cache / dance / face tools
        hg_agent::RegisterTools(GetDisplay(), camera_, servo_);
        // v3.6.7-dj7: dj6 full-screen self.screen.show_image is NOT registered any more. It reserved
        // 600 KB + 1 of PSRAM when the reply had no Content-Length (largest free block was ~240 KB),
        // and the bridge is back on the stock preview. Code kept in hg_meme_overlay.cc, unused.
        // hg_meme::RegisterTool(GetDisplay());
        // headphone-girl v3: Settings screen (hold the screen 1.5 s, or say "open settings")
        {
            hg_settings::Hooks h;
            h.get_home_pitch = [this]() { return servo_ ? servo_->GetHomePitch() : kLevelPitchDeg; };
            h.set_home_pitch = [this](float p) {
                if (servo_ == nullptr || !servo_->IsReady()) return;
                servo_->SaveHomeToNvs(servo_->GetHomeYaw(), p);
                servo_->Center(300);
            };
            h.go_home = [this]() { if (servo_ && servo_->IsReady()) servo_->Center(300); };
            h.get_idle_motion = [this]() { return auto_motion_.load(); };
            h.set_idle_motion = [this](bool on) { auto_motion_.store(on); SaveAutoMotionToNvs(on); };
            h.get_follow = [this]() { return gaze_follow_.load(); };
            h.set_follow = [this](bool on) { SetFollowFaces(on); };
            h.get_follow_debug = [this]() { return gaze_debug_.load(); };
            h.set_follow_debug = [this](bool on) { gaze_debug_.store(on); SaveGazeDebugToNvs(on); };
            h.enter_wifi_setup = [this]() {
                Application::GetInstance().Schedule([this]() { EnterWifiConfigMode(); });
            };
            hg_settings::Init(GetDisplay(), std::move(h));
            hg_settings::RegisterTools();
        }
        // headphone-girl v3.5: vowel lip sync, talking lights, party face
        hg_lip::Start();
        hg_eq_task::Start();   // v3.6.7-dj2: idle until the DJ screen shows
        if (geometry_display_ != nullptr) {
            geometry_display_->SetVoiceLevelHook([this](float level) {
                if (fairy_led_ != nullptr) fairy_led_->VoiceLevel(level);
            });
            hg_agent::SetPartyHook([this](bool on) {
                if (geometry_display_ != nullptr) geometry_display_->SetPartyMode(on);
            });
            // v3.6.7-dj: DJ screen while a song plays with DJ mode on
            geometry_display_->SetDjTitleProvider([]() { return hg_agent::DjTitle(); });
            hg_agent::SetDjHook([this](bool on) {
                if (geometry_display_ != nullptr) geometry_display_->SetDjMode(on);
            });
            hg_agent::SetDjVuHook([this](const std::string& mode, int ms) {
                return geometry_display_ != nullptr && geometry_display_->SetDjVu(mode, ms);
            });
            hg_agent::SetDjRaveHook([this](bool on) {   // v3.6.7-dj4
                if (geometry_display_ != nullptr) geometry_display_->SetRave(on);
            });
        }
        StartGazeTask();
        // ★★ 遥控器（ESP-NOW）【不能在这里初始化】——
        //   此刻 WiFi 还没 esp_wifi_init()，而 esp_now_init() 在这种状态下
        //   【不会】优雅返回错误，而是直接崩（LoadProhibited）。
        //   实测后果（2026-10-06）：开机到这就重启，无限循环 ⇒ 白屏。
        //   ⇒ 挪到下面的 StartNetwork() 里（那里 WiFi 模块一定已就绪）。
        // ★ 皮肤：读 NVS 决定默认显示哪套（几何脸要在 UI 建好后才能挂）
        //   这里只注册 MCP 工具 + 记下皮肤；实际切换在 SetupUI 之后
        stackchan_skin::RegisterMcpTools();
        RegisterFaceStyleTool();   // v3.5: face-only switch, no server change
        RegisterFollowFacesTool();   // v3.6.3: "follow me" / "stop following"
        ESP_LOGW(TAG, "skin at boot = %s",
                 stackchan_skin::SkinName(stackchan_skin::LoadSkin()));
        GetBacklight()->RestoreBrightness();
    }

    // ★ 应用启动皮肤：必须等 UI 建好（SetupUI）后调用
    void ApplyBootSkin() {
        if (geometry_display_ == nullptr) {
            return;
        }
        auto skin = stackchan_skin::LoadSkin();
        bool geom = (skin == stackchan_skin::Skin::Geometry) || FaceStyleClassic();
        geometry_display_->SetGeometryVisible(geom);
        ESP_LOGW(TAG, "boot skin applied: %s",
                 stackchan_skin::SkinName(skin));
        // ★ 顺便接上状态栏菜单按钮（此时 UI 已建好）
        HookMenuButton();
        // ★★★ 在这里把菜单 UI 建好（★ 修「点屏重启」）
        //   必须在 LVGL 上下文创建对象；运行时只显示/隐藏
        BuildMenuAtInit();
    }

    // ★ 接线：状态栏最右的 ☰ 按钮 → 打开/关闭侧边抽屉菜单
    //   （用户要求：「状态栏最右边加入菜单选项的图标，
    //     然后点开菜单，就从侧边滑出菜单选项」）
    void HookMenuButton() {
        // ★★★ 用户决定（2026-09-19）：「算了你tm这个功能别做了」
        //   ⇒ 菜单功能整体停用：
        //     · 不再给 ☰ 接线（点了也不做事）
        //     · 菜单 UI 不显示
        //     · 待机转头/舵机跟随改用【语音控制】（MCP 工具）
        //   注意：菜单 UI 对象仍在（初始化时建过），但不显示、不可达 ——
        //        保留对象而不删，避免动到初始化时序（更稳）。
        ESP_LOGI(TAG, "菜单功能已按用户要求停用（改用语音控制）");
    }

    virtual Led* GetLed() override {
        return fairy_led_;
    }

    // ★★★ 在 OTA 上报里带上【当前皮肤】（2026-09-19）
    //   修用户反馈：「之前按电源键切换皮肤，服务器也能切，不知为何不行了」
    //
    //   机制：设备每次连服务器都走 OTA，携带 GetSystemInfoJson()，
    //         其中 "board" 段是本函数的返回值。服务器 ota_handler
    //         会解析 data_json["board"] ⇒ 只要这里带上 skin，
    //         服务器就能在设备上线时自动对齐配置。
    //
    //   为什么必须这样：按电源键切皮肤只写设备自己的 NVS，
    //   不通知服务器；OTA 是【设备 → 服务器】的现成通道。
    std::string GetBoardJson() override {
        std::string json = WifiBoard::GetBoardJson();
        std::string skin =
            stackchan_skin::SkinName(stackchan_skin::LoadSkin());
        if (!skin.empty() && !json.empty() && json.back() == '}') {
            json.pop_back();                       // 去掉末尾 }
            if (!json.empty() && json.back() != '{') {
                json += ",";                       // 非空对象要加逗号
            }
            json += "\"skin\":\"" + skin + "\"}";   // v3.6.1: dead hg_voice field removed
        }
        return json;
    }

    // StackChan 的 12 颗 RGB 挂在 PY32 IO 扩展上（不是直连 GPIO），
    // 所以需要先起 I2C，再让 Led 自己初始化 IO 扩展。
    void InitializeFairyRgb() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGW(TAG, "i2c bus not ready, skip RGB init");
            return;
        }
        fairy_led_ = new CoreS3Py32Led(i2c_bus_);
        if (!fairy_led_->Init()) {
            ESP_LOGW(TAG, "Fairy RGB init failed (PY32 not found?)");
        } else {
            fairy_led_->RegisterMcpTools();  // 让 AI 能控制灯（关灯/换色）
            // v3.6.7-dj3: DJ screen drives her LEDs from the equalizer palette
            hg_eq_task::LedSink sink;
            sink.ctx = fairy_led_;
            sink.frame = [](void* c, const uint32_t* rgb) { static_cast<CoreS3Py32Led*>(c)->DjFrame(rgb); };
            sink.sync = [](void* c, bool on) { static_cast<CoreS3Py32Led*>(c)->SetDjSync(on); };
            hg_eq_task::SetLedSink(sink);
            ESP_LOGI(TAG, "RGB ready, MCP tools registered");
        }
    }

    // StackChan 的两个飞特总线舵机走 UART1（1Mbps, TX=GPIO6, RX=GPIO7）。
    // 舵机供电由 PY32 IO 扩展 pin0(VM EN) 控制 —— RGB 的 Init() 里已使能，
    // 所以本函数必须【在 InitializeFairyRgb() 之后】调用。
    // ★ 传感器：SI12T 触摸 + BMI270 摇晃，并起两个后台任务
    //   ① 轮询任务：摸头 → 开心；摇晃 → 晕眩
    //   ② 待机小动作：idle 时随机轻微转头（还原出厂固件那个"没事动一动"）
    void InitializeSensors() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGW(TAG, "i2c bus not ready, skip sensors");
            return;
        }
        sensor_ = new StackChanSensor(i2c_bus_);
        if (!sensor_->Init()) {
            ESP_LOGW(TAG, "传感器初始化失败（触摸/IMU 都不可用）");
        } else {
            sensor_->RegisterMcpTools();
        }

        // ① 轮询任务（10ms 一次：触摸要快速响应）
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                while (true) {
                    if (self->sensor_ != nullptr) {
                        self->sensor_->Poll();
                        if (self->sensor_->TakePetEvent() && !hg_agent::IsParty()) {
                            self->ReactPet();
                        }
                        self->HandleHeadGesture(self->sensor_->TakeGesture());
                        // ★★ 2026-10-06 用户反馈：「用遥控器控头会触发甩晕，左右扭头」
                        //   根因：甩晕靠 IMU 加速度突变判断（stackchan_sensor.cc：
                        //   diff > shake_threshold_，1 秒窗口内累计 3 次才触发），
                        //   但遥控器驱动舵机快速转动时，机身的反作用力同样会让 IMU
                        //   出现这种突变 ⇒ 被误判成「有人在甩」。
                        //   ⛔ 调大阈值解决不了：真甩的加速度一样大，调大等于废掉甩晕。
                        //   这是【控制来源冲突】，按来源抑制才对。
                        //   修法：遥控器活跃（1.5 秒内收到过包）时不判定甩晕。
                        //   ⇒ 放下遥控器（等 1.5 秒）照样能甩晕，功能没丢。
                        bool shake = self->sensor_->TakeShakeEvent();
                        int64_t now_ms = esp_timer_get_time() / 1000;
                        // v3.4: our own head moves are not "being shaken"
                        bool servo_moving = self->motion_busy_.load() ||
                                            now_ms < self->servo_quiet_at_ms_.load();
                        if (shake && servo_moving) {
                            shake = false;
                        }
                        // v3.4: end the dizzy face 1.5 s after the last real jolt
                        // (jolts during/just after our own moves don't count)
                        if (self->dizzy_active_.load()) {
                            int64_t jolt = self->sensor_->LastJoltMs();
                            int64_t quiet_from = self->servo_quiet_at_ms_.load();
                            int64_t ref = jolt > quiet_from ? jolt : quiet_from;
                            if (!servo_moving && now_ms - ref > kDizzyStillMs) {
                                self->EndDizzy("still");
                            } else if (now_ms - self->dizzy_since_ms_ > kDizzyMaxMs) {
                                self->EndDizzy("timeout");
                            }
                        }
                        if (shake) {
                            bool remote_active = false;
                            if (self->espnow_ != nullptr) {
                                int64_t last = self->espnow_->LastPacketMs();
                                remote_active =
                                    (last != 0) &&
                                    (esp_timer_get_time() / 1000 - last) < 1500;
                            }
                            if (!remote_active) {
                                self->ReactShake();
                            } else {
                                ESP_LOGD(TAG, "遥控器活跃中，跳过甩晕判定");
                            }
                        }
                    }
                    // ★ 摸头满 3 秒后，平滑还原到「摸之前」的姿势
                    //   （照官方 HeadPetModifier 的 restoreDelayMs = 3000）
                    //   时间没到就跳过，不阻塞轮询；摸头期间会被 ReactPet
                    //   不断推迟，等于「手还在摸就不还原」。
                    // v3.5: tap faces (giggle, peek, pet) go back after a moment
                    if (self->face_restore_at_ != 0 &&
                        esp_timer_get_time() / 1000 >= self->face_restore_at_) {
                        self->face_restore_at_ = 0;
                        auto st = Application::GetInstance().GetDeviceState();
                        if (st == kDeviceStateIdle && !hg_agent::IsParty() &&
                            !self->dizzy_active_.load()) {
                            if (auto* d = Board::GetInstance().GetDisplay()) d->SetEmotion("neutral");
                        }
                    }
                    if (self->pet_restore_at_ != 0 &&
                        !self->motion_busy_.load() &&
                        (esp_timer_get_time() / 1000) >= self->pet_restore_at_) {
                        self->pet_restore_at_ = 0;
                        if (self->servo_ != nullptr && self->servo_->IsReady()) {
                            self->motion_busy_.store(true);
                            self->servo_->MoveSmooth(self->pet_base_yaw_,
                                                     self->pet_base_pitch_,
                                                     250, 400);
                            self->EndMotion();
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            },
            "sens_poll", 3072, this, 3, &sensor_task_);

        // ② 待机小动作任务 —— 照官方 modifiers/idle_motion.h 的四种动作
        //
        // ★ 用户反馈：「待机自动转头，只能左右，不能上下，这很局限」
        //   旧实现把 pitch 写死 45°（MoveTo(delta, 45.0f)）⇒ 只可能左右摆。
        //   官方是【四种随机动作】（环视 / 小幅张望 / 快速一瞥 / Yaw 回正），
        //   每一种都同时动 yaw 和 pitch。
        //   这里照搬，并把基准从「固定 45°」改成「当前角度」，
        //   动作全部走 MoveSmooth 平滑下发（不猛甩）。
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                while (true) {
                    // 随机间隔 6~14 秒
                    uint32_t wait_ms = 6000 + (esp_random() % 8000);
                    vTaskDelay(pdMS_TO_TICKS(wait_ms));

                    if (self->servo_ == nullptr || !self->servo_->IsReady()) {
                        continue;
                    }
                    // ★ 开关关了就跳过（用户可在控制台关掉）
                    if (!self->auto_motion_.load()) {
                        continue;
                    }
                    // 正在做摸头/甩晕反应时别插一脚
                    if (self->motion_busy_.load()) {
                        continue;
                    }
                    // ★ 用户正在用遥控器控头时别插一脚
                    //   （3 秒内收到过遥控器包 ⇒ 让路，免得抢舵机）
                    if (self->espnow_ != nullptr) {
                        int64_t last = self->espnow_->LastPacketMs();
                        if (last != 0 &&
                            (esp_timer_get_time() / 1000 - last) < 3000) {
                            continue;
                        }
                    }
                    if (hg_agent::IsDancing()) {
                        continue;   // v3: a dance owns the head
                    }
                    if (esp_timer_get_time() / 1000 - self->gaze_last_move_ms_.load() < 10000) {
                        continue;   // v3.5: she is following a face
                    }
                    if (Application::GetInstance().GetDeviceState() !=
                        kDeviceStateIdle) {
                        continue;   // 说话/听的时候别乱动
                    }

                    // 以【当前角度】为基准（官方 idle_motion 也是基于当前角度）
                    float cy = self->servo_->GetYawAngle();
                    // headphone-girl v3: tilt is always based on the saved
                    // home (level) so idle glances never ratchet the face up.
                    float cp = self->servo_->GetHomePitch();
                    if (cy <= -900.0f) cy = self->servo_->GetHomeYaw();

                    float ty = cy;
                    float tp = cp;
                    uint16_t spd = 250;
                    uint32_t action = esp_random() % 100;
                    const char* name = "?";

                    if (action < 40) {
                        // ① 环视：朝一边偏 20~45°，同时抬头或低头 5~15°
                        float side = (esp_random() % 2) ? 1.0f : -1.0f;
                        ty = cy + side * (20.0f + (esp_random() % 26));
                        tp = cp + (esp_random() % 8);   // v3: stay near level
                        spd = 250 + (esp_random() % 60);
                        name = "环视";
                    } else if (action < 70) {
                        // ② 小幅张望：在当前位置上做小抖动
                        ty = cy + static_cast<int>(esp_random() % 31) - 15;
                        tp = self->servo_->GetHomePitch() + (esp_random() % 5);   // v3.6.1: near home, no drift
                        spd = 180 + (esp_random() % 60);
                        name = "张望";
                    } else if (action < 85) {
                        // ③ 快速一瞥：偏得多、动得快、带点抬头
                        float side = (esp_random() % 2) ? 1.0f : -1.0f;
                        ty = cy + side * (30.0f + (esp_random() % 21));
                        tp = self->servo_->GetHomePitch() + 4.0f + (esp_random() % 6);   // v3.6.1
                        spd = 450;
                        name = "一瞥";
                    } else {
                        // ④ 回正：yaw 归零，pitch 回开机默认角度
                        ty = self->servo_->GetHomeYaw();
                        tp = self->servo_->GetHomePitch();
                        spd = 220;
                        name = "回正";
                    }

                    ty = M5StackCoreS3Board::ClampSoftYaw(ty);
                    tp = M5StackCoreS3Board::ClampSoftPitch(tp);

                    self->motion_busy_.store(true);
                    self->servo_->MoveSmooth(ty, tp, spd, 420);
                    self->EndMotion();
                    ESP_LOGI(TAG, "idle motion(%s): yaw=%.1f pitch=%.1f",
                             name, ty, tp);
                }
            },
            "idle_mot", 3072, this, 2, &idle_motion_task_);

        // ★ 待机扭头开关：从 NVS 读（没存过 = 默认开）并注册 MCP 工具
        LoadAutoMotionFromNvs();
        RegisterMotionMcpTools();
        // ★ 视线跟踪开关：从 NVS 读（用户要求默认关）
        LoadGazeFollowFromNvs();
        ESP_LOGI(TAG, "传感器任务与待机小动作任务已启动");
    }

    // ★ 待机扭头开关：从 NVS 读（没存过 = 默认开）
    void LoadAutoMotionFromNvs() {
        Settings settings("fairy_motion", false);
        // 哨兵值：没存过返回 -1 ⇒ 保持默认（开）
        int32_t v = settings.GetInt("auto_motion", -1);
        if (v >= 0) {
            auto_motion_.store(v != 0);
        }
        ESP_LOGI(TAG, "auto_motion NVS=%d → %s", (int)v,
                 auto_motion_.load() ? "开" : "关");
    }

    // ★ 视线跟踪开关的 NVS 读写（namespace "fairy_gaze"，与灯光/舵机隔离）
    void LoadGazeFollowFromNvs() {
        Settings settings("fairy_gaze", false);
        int32_t v = settings.GetInt("follow", -1);
        if (v >= 0) {
            gaze_follow_.store(v != 0);
        }
        ESP_LOGI(TAG, "gaze_follow NVS=%d → %s（默认关）", (int)v,
                 gaze_follow_.load() ? "开" : "关");
        LoadGazeDebugFromNvs();
    }

    // v3.6.2: "Follow faces debug" (off by default), same namespace
    void LoadGazeDebugFromNvs() {
        Settings settings("fairy_gaze", false);
        gaze_debug_.store(settings.GetInt("debug", 0) != 0);
    }
    void SaveGazeDebugToNvs(bool on) {
        Settings settings("fairy_gaze", true);
        settings.SetInt("debug", on ? 1 : 0);
    }

    void SaveGazeFollowToNvs(bool on) {
        Settings settings("fairy_gaze", true);
        settings.SetInt("follow", on ? 1 : 0);
    }

    void SaveAutoMotionToNvs(bool on) {
        Settings settings("fairy_motion", true);
        settings.SetInt("auto_motion", on ? 1 : 0);
    }

    // ★ 注册 MCP 工具（让 AI/控制台都能开关待机扭头）
    void RegisterMotionMcpTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.motion.set_auto",
            "开启或关闭「待机时自己随机轻微转头」这个动作，并永久保存。"
            "enabled 传 true=开启 / false=关闭。"
            "用户说「别乱动」「待机别转头」时传 false。",
            PropertyList({
                Property("enabled", kPropertyTypeBoolean, true),
            }),
            [this](const PropertyList& p) -> ReturnValue {
                bool on = p["enabled"].value<bool>();
                auto_motion_.store(on);
                SaveAutoMotionToNvs(on);
                return std::string(std::string("待机自动转头已") +
                                   (on ? "开启" : "关闭") + "并保存");
            });

        mcp.AddTool("self.motion.get_auto",
            "查询「待机时自动转头」当前是开还是关。",
            PropertyList(),
            [this](const PropertyList& p) -> ReturnValue {
                (void)p;
                return std::string(auto_motion_.load() ? "开" : "关");
            });
    }

    // ── 自动动作的「软限位」────────────────────────────────
    // ★ 别让表情动作把舵机顶到机械极限（pitch 硬限位 3~87°）。
    //   自动动作只在中间这段活动，给物理行程留余量。
    static float ClampSoftPitch(float d) {
        if (d < kLevelPitchDeg) return kLevelPitchDeg;   // v3.6.1: level 3 is the floor
        if (d > 65.0f) return 65.0f;
        return d;
    }
    static float ClampSoftYaw(float d) {
        if (d < -70.0f) return -70.0f;
        if (d > 70.0f) return 70.0f;
        return d;
    }

    // ★ 记下当前姿势（读失败时用开机默认角度兜底）
    void SnapshotPose(float* yaw, float* pitch) {
        *yaw = servo_->GetYawAngle();
        *pitch = servo_->GetPitchAngle();
        if (*yaw <= -900.0f) *yaw = servo_->GetHomeYaw();
        if (*pitch <= -900.0f) *pitch = servo_->GetHomePitch();
    }

    // ★ 摸头反应：开心 + 冒爱心 + 【相对当前角度】的温和动作
    //
    // ★★ 旧实现的两个问题（用户反馈：「摸摸头的动作也会导致他俯仰角一下变最高」）：
    //   ① 角度写死绝对值（MoveTo(0, 55)）⇒ 只要开机默认姿势不是 45°，
    //      这一下就是把头【猛拽到 55°】，看着就像「第一下就顶到最高」
    //   ② 动作做完【不还原】⇒ 头一直抬着
    //   现在照官方 modifiers/head_pet.h：
    //     · 记下摸之前的 yaw/pitch 作基准，在此基础上加【随机小增量】
    //     · 慢速平滑过去（MoveSmooth）
    //     · 3 秒后平滑还原（由 sens_poll 任务里的 pet_restore_at_ 计时）
    // ── v3.5 face style (face only: same server, no reboot) ──
    static bool FaceStyleClassic() {
        Settings s("hg", false);
        return s.GetInt("face_style", 0) == 1;
    }

    // Face only: saves face_style and redraws. Same server, no reboot. Used by
    // self.face.style and the power key.
    bool ApplyFaceStyle(bool classic, std::string* err) {
        if (stackchan_skin::LoadSkin() == stackchan_skin::Skin::Geometry) {
            if (err) *err = "the official-server skin always uses the classic face";
            return false;
        }
        {
            Settings s("hg", true);
            s.SetInt("face_style", classic ? 1 : 0);
        }
        if (geometry_display_ != nullptr) {
            geometry_display_->SetGeometryVisible(classic);
            if (!classic) geometry_display_->SetEmotion("happy");
        }
        return true;
    }

    // ── v3.6.3 follow faces by voice ──
    // One place that turns Follow faces on/off: the Settings switch, the old
    // menu and the MCP tool all land here. Same atomic, same NVS key
    // (fairy_gaze/follow), starts the tracker task when turned on.
    void SetFollowFaces(bool on) {
        gaze_follow_.store(on);
        SaveGazeFollowToNvs(on);
        if (on) StartGazeTask();
    }

    void RegisterFollowFacesTool() {
        McpServer::GetInstance().AddTool(hg_follow_tool::kToolName,
            hg_follow_tool::kToolDescription,
            PropertyList({Property("enabled", kPropertyTypeBoolean)}),
            [this](const PropertyList& p) -> ReturnValue {
                bool on = p["enabled"].value<bool>();
                hg_follow_tool::Hooks h;
                h.get_follow = [this]() { return gaze_follow_.load(); };
                h.set_follow = [this](bool v) { SetFollowFaces(v); };
                h.camera_enabled = []() { return HgCameraEnabled().load(); };
                h.tracker_running = [this]() { return gaze_task_ != nullptr; };
                h.refresh_ui = [this]() {
                    hg_settings::RefreshIfOpen();   // the Follow faces switch shows the new state
                    MenuRefreshLabels();            // old long-press menu, if it was built
                };
                std::string r = hg_follow_tool::Apply(h, on);
                ESP_LOGI(TAG, "voice: follow faces -> %s", r.c_str());
                return r;
            });
    }

    void RegisterFaceStyleTool() {
        McpServer::GetInstance().AddTool("self.face.style",
            "Change only how her face looks on the screen. style: girl (the headphone-girl "
            "drawings, default) or classic (the simple StackChan line face). Same server, "
            "same voice, no restart. Leave style empty to just ask which one is on.",
            PropertyList({Property("style", kPropertyTypeString, std::string(""))}),
            [this](const PropertyList& p) -> ReturnValue {
                std::string st = p["style"].value<std::string>();
                for (auto& c : st) c = (char)tolower((unsigned char)c);
                bool official = stackchan_skin::LoadSkin() == stackchan_skin::Skin::Geometry;
                if (st.empty()) {
                    bool classic = FaceStyleClassic() || official;
                    return std::string(classic ? "{\"style\":\"classic\"}" : "{\"style\":\"girl\"}");
                }
                bool classic;
                if (st == "classic" || st == "line" || st == "simple" || st == "stackchan") {
                    classic = true;
                } else if (st == "girl" || st == "headphone" || st == "headphone-girl" ||
                           st == "anime" || st == "drawn") {
                    classic = false;
                } else {
                    return std::string("{\"ok\":false,\"error\":\"style must be girl or classic\"}");
                }
                std::string err;
                if (!ApplyFaceStyle(classic, &err)) {
                    return std::string("{\"ok\":false,\"error\":\"") + err + "\"}";
                }
                return std::string(classic ? "{\"ok\":true,\"style\":\"classic\"}"
                                           : "{\"ok\":true,\"style\":\"girl\"}");
            });
    }

    // ── v3.5 head gestures ──
    static bool SwipeSwapped() {
        Settings s("hg", false);
        return s.GetInt("swipeswap", 0) != 0;
    }

    void HandleHeadGesture(int g) {
        if (g == StackChanSensor::kGestureNone) return;
        if (g == StackChanSensor::kSwipeForward || g == StackChanSensor::kSwipeBackward) {
            bool fwd = (g == StackChanSensor::kSwipeForward) != SwipeSwapped();
            if (fwd) hg_agent::SwipeNext(); else hg_agent::SwipeStop();
            return;
        }
        if (hg_agent::IsParty() || dizzy_active_.load()) return;   // taps wait for the song
        switch (g) {
            case StackChanSensor::kTapPad0: ReactGiggle(); break;
            case StackChanSensor::kTapPad1: ReactPet(); break;
            case StackChanSensor::kTapPad2: ReactPeek(); break;
            default: break;   // kHold already raised a pet event
        }
    }

    // Tap on the first pad: a happy laugh and two quick nods.
    void ReactGiggle() {
        ESP_LOGI(TAG, "head tap -> giggle");
        if (auto* d = Board::GetInstance().GetDisplay()) d->SetEmotion("laughing");
        face_restore_at_ = esp_timer_get_time() / 1000 + 3000;
        if (servo_ == nullptr || !servo_->IsReady() || motion_busy_.load()) return;
        float by = 0.0f, bp = kLevelPitchDeg;
        SnapshotPose(&by, &bp);
        bp = servo_->GetHomePitch();
        pet_base_yaw_ = by;
        pet_base_pitch_ = bp;
        motion_busy_.store(true);
        for (int i = 0; i < 2; ++i) {
            servo_->MoveSmooth(by, ClampSoftPitch(bp + 9.0f), 350, 160);
            servo_->MoveSmooth(by, ClampSoftPitch(bp + 1.0f), 350, 160);
        }
        EndMotion();
        pet_restore_at_ = esp_timer_get_time() / 1000 + 1200;
    }

    // Tap on the last pad: a curious peek to one side.
    void ReactPeek() {
        bool left = (esp_random() % 2) == 0;
        ESP_LOGI(TAG, "head tap -> peek %s", left ? "left" : "right");
        if (auto* d = Board::GetInstance().GetDisplay()) d->SetEmotion(left ? "look_left" : "look_right");
        face_restore_at_ = esp_timer_get_time() / 1000 + 2500;
        if (servo_ == nullptr || !servo_->IsReady() || motion_busy_.load()) return;
        float by = 0.0f, bp = kLevelPitchDeg;
        SnapshotPose(&by, &bp);
        bp = servo_->GetHomePitch();
        pet_base_yaw_ = by;
        pet_base_pitch_ = bp;
        motion_busy_.store(true);
        servo_->MoveSmooth(ClampSoftYaw(by + (left ? -18.0f : 18.0f)), ClampSoftPitch(bp + 4.0f), 260, 380);
        EndMotion();
        pet_restore_at_ = esp_timer_get_time() / 1000 + 2000;
    }

    // ── v3.6.1 follow faces (Settings > Head > Follow faces) ──
    // The on-device esp-dl face detector looks at a camera frame 4 times a
    // second and steers the head in small smooth steps around home (level). Only when the switch and the camera are on
    // and nothing else owns the head. Which way is "right" in the image is
    // learned (and saved) if the first guess turns out backwards.
    // v3.6.1: the task starts only when Follow faces is first turned on. Its
    // 12 KB stack and the frame buffer live in PSRAM, so internal SRAM stays
    // free for Wi-Fi and TLS. A PSRAM stack must never run flash operations,
    // so NVS is read here (caller task) and written via Application::Schedule.
    void StartGazeTask() {
        if (gaze_task_ != nullptr || camera_ == nullptr || servo_ == nullptr) return;
        if (!gaze_follow_.load()) return;
        // v3.6.2: a photo may free the face model once follow has paused
        hg_cam_gate::SetFreeHook([]() { return hg_face_dl::Release(); });
        if (gaze_frame_ == nullptr) {
            gaze_frame_ = (uint8_t*)heap_caps_malloc(kGazeFrameCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (gaze_frame_ == nullptr) {
                ESP_LOGE(TAG, "follow faces: no PSRAM for a frame, tracking off");
                return;
            }
        }
        {
            Settings s("hg", false);
            gaze_sx_.store(s.GetInt("gz_sx", 1) < 0 ? -1 : 1);
            gaze_sy_.store(s.GetInt("gz_sy", 1) < 0 ? -1 : 1);
        }
        if (xTaskCreatePinnedToCoreWithCaps(
                [](void* arg) { static_cast<M5StackCoreS3Board*>(arg)->GazeLoop(); },
                "hg_gaze", 12288, this, 2, &gaze_task_, 1,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            gaze_task_ = nullptr;
            ESP_LOGE(TAG, "follow faces: task create failed");
            return;
        }
        ESP_LOGI(TAG, "follow faces: task started (stack in PSRAM), internal free %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    void SaveGazeSign(const char* key, int sign) {
        std::string k = key;
        Application::GetInstance().Schedule([k, sign]() {
            Settings s("hg", true);
            s.SetInt(k.c_str(), sign);
        });
    }

    // v3.6.3: same rules as v3.6.2, but says WHY it is "no" (the old debug line
    // lumped everything into "busy/state", including the speaking RAM guard).
    bool GazeMayMove(const char** why = nullptr) {
        const char* w = nullptr;
        if (!gaze_follow_.load()) w = "follow off";
        else if (!HgCameraEnabled().load()) w = "camera off in Settings";
        else if (servo_ == nullptr || !servo_->IsReady()) w = "servo not ready";
        else if (motion_busy_.load() || pet_restore_at_ != 0 || dizzy_active_.load()) w = "head busy (pet/dizzy/move)";
        else if (hg_agent::IsDancing() || hg_agent::IsParty() || hg_agent::IsRecording()) w = "dance/party/record";
        else if (hg_settings::IsOpen()) w = "settings open";
        else if (espnow_ != nullptr && espnow_->LastPacketMs() != 0 &&
                 esp_timer_get_time() / 1000 - espnow_->LastPacketMs() < 3000) w = "remote in use";
        else {
            auto st = Application::GetInstance().GetDeviceState();
            // v3.6.1: never while connecting / starting / upgrading (TLS needs SRAM).
            if (st == kDeviceStateIdle || st == kDeviceStateListening) {
                w = nullptr;
            } else if (st == kDeviceStateSpeaking) {
                if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < kGazeSpeakMinSram) w = "speaking, RAM < 56K";
            } else if (st == kDeviceStateConnecting) {
                w = "state connecting";
            } else if (st == kDeviceStateStarting || st == kDeviceStateActivating ||
                       st == kDeviceStateWifiConfiguring) {
                w = "state starting/activating";
            } else {
                w = "state other";
            }
        }
        if (why != nullptr) *why = w;
        return w == nullptr;
    }

    // v3.6.2 follow faces loop. Faster (100 ms between checks, moves of
    // 180..420 ms, 200 ms settle), first move after one confident sighting,
    // "face lost right after a move" counts as a wrong-direction strike.
    // Photos win: grab + detect only inside hg_cam_gate::TryEnterFollow().
    // RAM guards stay and log clearly (pause + resume lines).
    void GazeLoop() {
        uint8_t* frame = gaze_frame_;
        const size_t kFrameCap = kGazeFrameCap;
        int64_t last_info = 0, last_dbg = 0;
        int64_t off_since = 0;
        hg_gaze::SignLearner lx, ly;
        lx.sign = gaze_sx_.load();
        ly.sign = gaze_sy_.load();
        int streak = 0;
        bool moved = false;
        bool away_from_home = false;
        int64_t last_seen = 0;
        float before_x = 0, before_y = 0;
        int64_t settle_until = 0;
        const char* paused_for = nullptr;    // current RAM pause reason (logged once)
        int64_t first_ok_at = 0;             // v3.6.3: first time GazeMayMove() said yes
        // v3.6.3: load the face model now, at boot, before Wi-Fi / TLS / the
        // wake-word engine start, while PSRAM is one big block. It stays loaded
        // (a photo frees it; the next detect reloads it the same way).
        if (gaze_follow_.load() && hg_cam_gate::Get().TryEnterFollow()) {
            hg_face_dl::Load();
            hg_cam_gate::Get().LeaveFollow();
        }
        // per-second debug counters
        int dbg_checks = 0, dbg_found = 0, dbg_moves = 0;
        const char* dbg_skip = "-";
        hg_gaze::Face dbg_face;
        auto now_ms = []() { return esp_timer_get_time() / 1000; };
        auto debug_line = [&](int64_t now) {
            if (!gaze_debug_.load() || now - last_dbg < 1000) return;
            last_dbg = now;
            ESP_LOGI(TAG,
                     "follow dbg: checks %d found %d moves %d | boxes %d best %.2f box [%d,%d,%d,%d] "
                     "at (%.2f,%.2f) | detect %d ms | skip %s | sign %d/%d | sram %u min %u | psram big %u",
                     dbg_checks, dbg_found, dbg_moves, hg_face_dl::LastCount(), dbg_face.score,
                     dbg_face.box.x0, dbg_face.box.y0, dbg_face.box.x1, dbg_face.box.y1, dbg_face.cx,
                     dbg_face.cy, hg_face_dl::LastMs(), dbg_skip, lx.sign, ly.sign,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
            dbg_checks = dbg_found = dbg_moves = 0;
            dbg_skip = "-";
            dbg_face = hg_gaze::Face{};
        };
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(hg_gaze::kLoopMs));
            int64_t now = now_ms();
            const char* why = nullptr;
            if (!GazeMayMove(&why) || hg_face_dl::Failed()) {
                // follow switched off for 20 s: give the detector memory back
                if (!gaze_follow_.load()) {
                    if (off_since == 0) off_since = now;
                    if (now - off_since > 20000 && hg_face_dl::Loaded() &&
                        hg_cam_gate::Get().TryEnterFollow()) {
                        if (hg_face_dl::Release()) ESP_LOGI(TAG, "follow faces: off, detector released");
                        hg_cam_gate::Get().LeaveFollow();
                    }
                } else {
                    off_since = 0;
                    dbg_skip = hg_face_dl::Failed() ? "model failed" : (why ? why : "-");
                    debug_line(now);
                }
                streak = 0;
                moved = false;
                away_from_home = false;   // someone else owns the head now
                last_seen = 0;
                vTaskDelay(pdMS_TO_TICKS(400));
                continue;
            }
            off_since = 0;
            // v3.6.3: right after boot/activation the wake-word engine (AFE,
            // MultiNet) is still building and briefly needs ~15 KB of SRAM.
            // v3.6.2 ran its first detect inside that window (free hit 16 B).
            if (first_ok_at == 0) first_ok_at = now;
            if (now - first_ok_at < kGazeStartDelayMs) {
                dbg_skip = "warming up after boot";
                debug_line(now);
                continue;
            }
            if (now < settle_until) continue;
            // SRAM guard: skip a check rather than squeeze Wi-Fi / TLS
            size_t sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (sram < kGazeMinSram) {
                if (paused_for == nullptr || now - last_info > 10000) {
                    last_info = now;
                    ESP_LOGW(TAG, "follow faces: PAUSED, internal RAM low (free %u < %u). "
                                  "Resumes by itself.", (unsigned)sram, (unsigned)kGazeMinSram);
                }
                paused_for = "low internal RAM";
                dbg_skip = paused_for;
                debug_line(now);
                continue;
            }
            if (paused_for != nullptr) {
                ESP_LOGI(TAG, "follow faces: RESUMED (%s over), internal free %u", paused_for,
                         (unsigned)sram);
                paused_for = nullptr;
            }
            // photos win: no grab/detect while a photo is being taken
            if (!hg_cam_gate::Get().TryEnterFollow()) {
                dbg_skip = "photo";
                debug_line(now);
                continue;
            }
            int fw = 0, fh = 0, kind = 0;
            bool got = camera_->GrabFrame(frame, kFrameCap, &fw, &fh, &kind);
            hg_gaze::Face f;
            if (got) hg_face_dl::Detect(frame, fw, fh, kind, &f);
            hg_cam_gate::Get().LeaveFollow();
            if (!got) {
                dbg_skip = "no frame";
                debug_line(now);
                continue;
            }
            now = now_ms();
            ++dbg_checks;
            if (f.found) {
                ++dbg_found;
                if (f.score >= dbg_face.score) dbg_face = f;
            }
            if (now - last_info > 10000) {
                last_info = now;
                ESP_LOGI(TAG, "follow faces: detect %d ms, face %s, internal free %u min %u",
                         hg_face_dl::LastMs(), f.found ? "yes" : "no",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
            }
            if (!f.found) {
                streak = 0;
                if (moved) {   // lost right after a move: maybe we turned the wrong way
                    moved = false;
                    if (lx.Lost(before_x)) {
                        SaveGazeSign("gz_sx", lx.sign);
                        ESP_LOGW(TAG, "follow faces: yaw direction flipped to %d (face lost after move)", lx.sign);
                    }
                    if (ly.Lost(before_y)) {
                        SaveGazeSign("gz_sy", ly.sign);
                        ESP_LOGW(TAG, "follow faces: pitch direction flipped to %d (face lost after move)", ly.sign);
                    }
                }
                if (hg_gaze::ShouldReturnHome(away_from_home, last_seen, now) && GazeMayMove()) {
                    motion_busy_.store(true);
                    servo_->MoveSmooth(servo_->GetHomeYaw(), servo_->GetHomePitch(), 140, 900);
                    EndMotion();
                    away_from_home = false;
                    last_seen = 0;
                    ESP_LOGI(TAG, "follow faces: nobody for a while, back to level");
                }
                debug_line(now);
                continue;
            }
            last_seen = now;
            if (moved) {   // did the last step bring her closer?
                moved = false;
                if (lx.Update(before_x, f.cx)) {
                    SaveGazeSign("gz_sx", lx.sign);
                    ESP_LOGW(TAG, "follow faces: yaw direction flipped to %d", lx.sign);
                }
                if (ly.Update(before_y, f.cy)) {
                    SaveGazeSign("gz_sy", ly.sign);
                    ESP_LOGW(TAG, "follow faces: pitch direction flipped to %d", ly.sign);
                }
            }
            if (++streak < hg_gaze::SightingsNeeded(f.score)) { debug_line(now); continue; }
            hg_gaze::Step st = hg_gaze::PlanStep(f, lx.sign, ly.sign);
            if (!st.move) { dbg_skip = "centred"; debug_line(now); continue; }
            float cy = servo_->GetYawAngle();
            float cp = servo_->GetPitchAngle();
            if (cy <= -900.0f || cp <= -900.0f) { dbg_skip = "servo read"; debug_line(now); continue; }
            // window centred on the saved home (level by default)
            hg_gaze::Window win = hg_gaze::GazeWindow(servo_->GetHomeYaw(), servo_->GetHomePitch());
            float ty = hg_gaze::Clamp(cy + st.dyaw, win.yaw_lo, win.yaw_hi);
            float tp = hg_gaze::Clamp(cp + st.dpitch, win.pitch_lo, win.pitch_hi);
            if (std::fabs(ty - cy) < 1.5f && std::fabs(tp - cp) < 1.5f) {
                dbg_skip = "at limit";
                debug_line(now);
                continue;
            }
            if (!GazeMayMove()) continue;
            motion_busy_.store(true);
            servo_->MoveSmooth(ty, tp, 300, hg_gaze::MoveMs(ty - cy, tp - cp));
            EndMotion();
            ++dbg_moves;
            away_from_home = true;
            gaze_last_move_ms_.store(now_ms());
            settle_until = now_ms() + hg_gaze::kSettleMs;   // let the picture settle
            moved = std::fabs(ty - cy) >= 3.0f || std::fabs(tp - cp) >= 3.0f;
            before_x = std::fabs(ty - cy) >= 3.0f ? f.cx : 0.0f;
            before_y = std::fabs(tp - cp) >= 3.0f ? f.cy : 0.0f;
            debug_line(now_ms());
        }
    }

    void ReactPet() {
        ESP_LOGI(TAG, "摸头 → 开心 + 冒爱心");
        auto display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            // headphone-girl patch: "loving" resolves to pet.gif (hearts) in GIF
            // mode and to the happy face in geometry mode
            display->SetEmotion("loving");
        }
        face_restore_at_ = esp_timer_get_time() / 1000 + 3500;   // v3.5
        // ★ 冒爱心 + 脸红（用户要求：「冒爱心」；官方摸头时这两个是一起冒的
        //   —— head_pet.h L90/L91 同一句里连加两个装饰器）
        //   ⛔ 两个方法内部都判 geometry_visible_ ⇒ 只在【官方小智皮肤】上出现，
        //      Fairy 皮肤下不会冒任何东西
        if (geometry_display_ != nullptr) {
            geometry_display_->ShowHeart();
            geometry_display_->ShowShy();
        }

        if (servo_ == nullptr || !servo_->IsReady()) {
            return;
        }

        float base_yaw = 0.0f, base_pitch = kLevelPitchDeg;
        SnapshotPose(&base_yaw, &base_pitch);
        base_pitch = servo_->GetHomePitch();   // v3: pet returns to level
        pet_base_yaw_ = base_yaw;
        pet_base_pitch_ = base_pitch;

        float ty = base_yaw;
        float tp = base_pitch;

        // 官方 head_pet 也是「三种动作随机挑一种」
        switch (esp_random() % 3) {
            case 0:   // 抬头（像被摸头顶）
                tp = base_pitch + 12.0f + (esp_random() % 7);           // +12~18°
                ty = base_yaw + static_cast<int>(esp_random() % 17) - 8;
                break;
            case 1:   // 歪头
                ty = base_yaw + ((esp_random() % 2) ? 1.0f : -1.0f) *
                                       (15.0f + (esp_random() % 11));
                tp = base_pitch - 1.0f - (esp_random() % 4);            // 略低 1~4°
                break;
            default:  // 大幅度开心
                tp = base_pitch + 18.0f + (esp_random() % 7);           // +18~24°
                break;
        }
        ty = ClampSoftYaw(ty);
        tp = ClampSoftPitch(tp);

        motion_busy_.store(true);
        servo_->MoveSmooth(ty, tp, 300, 360);
        EndMotion();

        // 3 秒后还原；期间再被摸会刷新这个时间戳（= 手还在摸就不还原）
        pet_restore_at_ = (esp_timer_get_time() / 1000) + 3000;
        ESP_LOGI(TAG, "摸头动作: yaw=%.1f→%.1f pitch=%.1f→%.1f",
                 base_yaw, ty, base_pitch, tp);
    }

    // ★ 摇晃反应：晕眩（用户要求：「甩晕」）
    //
    // ★★ 用户反馈：「甩晕的过程中它会不断扭头，但俯仰角第一下就会变到最高」
    //   旧实现三次 MoveTo 都把 pitch 写成 45°（绝对值）：
    //     · 开机默认姿势不是 45° 时，第一下就把俯仰猛拽到 45°
    //     · 后两次同样写 45° ⇒ 只有「第一下」看得出变化
    //   现在：pitch 全程【保持当前值不动】，只做 yaw 的平滑小幅摆动，
    //         结束时平滑回到原角度。扭头保留，俯仰不再乱动。
    void ReactShake() {
        ESP_LOGI(TAG, "摇晃 → 晕眩");
        // v3.4: still being shaken while already dizzy -> keep the face, don't
        // replay the head wobble (that wobble used to re-trigger itself forever)
        bool was_dizzy = dizzy_active_.exchange(true);
        if (!was_dizzy) dizzy_since_ms_ = esp_timer_get_time() / 1000;
        if (was_dizzy) return;
        auto display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->SetEmotion("doubtful");
        }
        // ★ 转圈晕眩装饰器
        if (geometry_display_ != nullptr) {
            geometry_display_->ShowDizzy();
        }

        if (servo_ == nullptr || !servo_->IsReady()) {
            return;
        }

        float base_yaw = 0.0f, base_pitch = kLevelPitchDeg;
        SnapshotPose(&base_yaw, &base_pitch);

        float left  = ClampSoftYaw(base_yaw - 25.0f);
        float right = ClampSoftYaw(base_yaw + 25.0f);

        motion_busy_.store(true);
        // ★ pitch 一律传 base_pitch：甩晕不改俯仰
        servo_->MoveSmooth(left, base_pitch, 350, 240);
        servo_->MoveSmooth(right, base_pitch, 350, 300);
        servo_->MoveSmooth(left, base_pitch, 350, 300);
        servo_->MoveSmooth(base_yaw, base_pitch, 250, 260);
        EndMotion();
        ESP_LOGI(TAG, "摇晃动作结束，回到 yaw=%.1f pitch=%.1f",
                 base_yaw, base_pitch);
    }

    // ═══════════════ 官方遥控器（K151-R / ESP-NOW）═══════════════
    //
    // ★ 遥控器是什么、协议是什么 ⇒ 见 cores3_espnow_remote.h 顶部注释
    //   （实读官方 official-stackchan/remote/ 源码，不是猜的）
    //
    // ★ 本函数只做两件事：
    //   ① 起 ESP-NOW 接收（内部会等 WiFi 连上再初始化 —— 信道要先确定）
    //   ② 起消费任务：角度送舵机、BtnB 接到对话开关
    //
    // ⛔ 收包回调跑在 WiFi 任务上下文 ⇒ 那里只解析 + 置标志，
    //    绝不碰 LVGL / UART / 舵机（会拖垮 WiFi，音频也会断）
    // ⛔ 设备侧一个字节都不动 WiFi 信道（peer.channel = 0 跟随）
    //    ⇒ 不会影响设备连服务器
    //   代价 = 遥控器 SETUP 页的 Channel 要调到与路由器一致（日志会打印）
    void InitializeEspNowRemote() {
        espnow_ = new CoreS3EspNowRemote();
        if (!espnow_->Start()) {
            ESP_LOGW(TAG, "遥控器接收未启动");
            return;
        }

        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                float last_yaw   = -999.0f;
                float last_pitch = -999.0f;
                while (true) {
                    CoreS3EspNowRemote::Command cmd;
                    if (self->espnow_ != nullptr &&
                        self->espnow_->TakeCommand(&cmd)) {
                        if (self->servo_ != nullptr && self->servo_->IsReady()) {
                            // ★ 变化 ≥1.0° 才下发：遥控器 30ms 一包，
                            //   不判变化会把舵机 UART 打满（舵机也来不及跟）
                            // ★ 1.0° 起步才下发（原来是 0.5°）：
                            //   0.5° 时摇杆的轻微漂移会一直触发下发，舵机跟着抖
                            //   （用户反馈「动起来偶尔会抖」）。1° 肉眼看不出延迟。
                            if (fabsf(cmd.yaw_deg - last_yaw) >= 1.0f ||
                                fabsf(cmd.pitch_deg - last_pitch) >= 1.0f) {
                                last_yaw   = cmd.yaw_deg;
                                last_pitch = cmd.pitch_deg;
                                self->servo_->MoveTo(cmd.yaw_deg,
                                                     cmd.pitch_deg,
                                                     cmd.speed);
                            }
                        }
                    }

                    // ★ BtnB 按下 = 开/关对话（与按屏幕上的对话键同一个入口）
                    if (self->espnow_ != nullptr &&
                        self->espnow_->TakeButtonPress()) {
                        ESP_LOGI(TAG, "遥控器 BtnB → 切换对话状态");
                        Application::GetInstance().ToggleChatState();
                    }

                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            },
            "espnow_rx", 3072, this, 2, &espnow_task_);
    }

    // ═══════════ 遥控器（ESP-NOW）初始化时机 ═══════════
    //
    // ★★ 教训（2026-10-06 实测踩到，设备白屏 + 重启循环 26 次）：
    //   最初把 InitializeEspNowRemote() 放在【构造函数】里，并让后台任务
    //   「轮询 esp_now_init() 直到它返回 ESP_OK —— WiFi 未就绪时应返回
    //    ESP_ERR_WIFI_NOT_INIT」。**这个假设是错的**：
    //   在 esp_wifi_init() 之前调 esp_now_init() 不会优雅返回错误，
    //   而是直接 LoadProhibited 崩溃（EXCVADDR 0x4c，addr2line 指向 esp_now_init）。
    //
    //   正解 = 在这里初始化：StartNetwork() 内部先走
    //   WifiManager::Initialize() → esp_wifi_init()（wifi_manager.cc:90），
    //   之后才回来 ⇒ 此处 WiFi 模块【一定】已就绪。
    //   而且【不要求连上】—— 没配网 / 路由器没开也照样初始化，
    //   射频待在默认信道 1，遥控器保持出厂 1 就能用。
    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();     // 内部 esp_wifi_init + 开始连接 / 进配网
        InitializeEspNowRemote();      // ★ 现在才安全
    }

    void InitializeServo() {
        servo_ = new CoreS3Servo();
        bool ok = servo_->Init();

        // ★ 2026-09-26 修「每次重启电脑后调不了角度」：
        //   设备由 PC 的 USB 供电 ⇒ 电脑一重启，设备跟着断电重启。那次开机
        //   PY32 IO 扩展刚上电、舵机自己的控制芯片启动又比 ESP32 慢，
        //   原来【只探测一次、零延时】⇒ 读不到位置就判定失败 ⇒ 那 6 个舵机
        //   动作根本不注册 ⇒ AI 得到「动作通道里没有角度接口」。
        //   现在：重新使能舵机供电 + 延时重试（最多约 5 轮 ≈ 8 秒）。
        for (int round = 0; round < 5 && !ok; round++) {
            if (fairy_led_ != nullptr) {
                fairy_led_->EnableServoPower();
            }
            vTaskDelay(pdMS_TO_TICKS(400));
            ok = servo_->ProbeRetry(3, 250);
            ESP_LOGW(TAG, "servo 重试第 %d 轮: %s", round + 1,
                     ok ? "成功" : "仍失败");
        }

        if (ok) {
            servo_->RegisterMcpTools();   // 让 AI 能控制转头
            ESP_LOGI(TAG, "servo ready, MCP tools registered");
        } else {
            ESP_LOGW(TAG, "servo init failed (check wiring / servo power)");
            // ★ 还不成就在后台继续补试：成了就补注册动作，用户不必拔电重启。
            StartServoRecoverTask();
        }
    }

    // ★ 后台补试舵机（只在开机那几轮都失败时才起）
    //   进了这个任务说明开机时舵机没探到；这里每 5 秒再试一次，最多 12 次
    //   （约 1 分钟）。一旦探到就【补注册 MCP 动作】——工具是运行时可加的，
    //   服务器每次连接都会重新问一遍动作清单，所以下一次唤醒就能用了。
    void StartServoRecoverTask() {
        if (servo_ == nullptr) {
            return;
        }
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                for (int i = 0; i < 12; i++) {
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    if (self->servo_ == nullptr || self->servo_->IsReady()) {
                        break;
                    }
                    if (self->fairy_led_ != nullptr) {
                        self->fairy_led_->EnableServoPower();
                    }
                    if (self->servo_->ProbeRetry(2, 200)) {
                        self->servo_->RegisterMcpTools();
                        ESP_LOGI(TAG, "servo 后台补试成功，MCP 动作已补注册");
                        break;
                    }
                }
            },
            "servo_recover", 3072, this, 2, nullptr);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CoreS3AudioCodec audio_codec(i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_AW88298_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    // v3.6.3: self.get_device_status also says whether Follow faces is on.
    virtual std::string GetDeviceStatusJson() override {
        std::string base = WifiBoard::GetDeviceStatusJson();
        cJSON* root = cJSON_Parse(base.c_str());
        if (root == nullptr) return base;
        cJSON* head = cJSON_CreateObject();
        cJSON_AddBoolToObject(head, "follow_faces", gaze_follow_.load());
        cJSON_AddBoolToObject(head, "camera_enabled", HgCameraEnabled().load());
        cJSON_AddBoolToObject(head, "tracker_running", gaze_task_ != nullptr);
        cJSON_AddItemToObject(root, "head", head);
        char* str = cJSON_PrintUnformatted(root);
        std::string out = str ? std::string(str) : base;
        if (str) cJSON_free(str);
        cJSON_Delete(root);
        return out;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual Backlight *GetBacklight() override {
        static CustomBacklight backlight(pmic_);
        return &backlight;
    }
};

DECLARE_BOARD(M5StackCoreS3Board);
