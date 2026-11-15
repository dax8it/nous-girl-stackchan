// StackChan (CoreS3) 的「官方几何脸」皮肤
//
// 背景（用户需求）：
//   「机器里同时装着两套皮肤：① Fairy（GIF 脸）② 官方小表情（几何脸）。
//     平时用 Fairy，哪天不想用了 → 切一下 → 整个界面换成小表情那套。
//     不是叠加，是二选一。」
//
// 本类实现第 ② 套：官方 stack-chan 的几何脸（LVGL 矢量绘制 + 装饰器）。
//
// ★ 与 Fairy 并存的架构（读小智 SpiLcdDisplay 源码后确定）：
//   SpiLcdDisplay 里已有两个容器：
//       emoji_box_       ← GIF 表情（Fairy 用）
//       preview_image_   ← 预览图
//   本类再加第三个：
//       geometry_root_   ← 几何脸（眼/嘴/眉 + 装饰器）
//   ⇒ 三者的显隐互斥 ⇒ 切换 = 换显隐，【不销毁重建、无黑屏】
//
// ★ 素材来源：official-stackchan/firmware/main/stackchan/avatar/
//   （M5Stack 官方，MIT 许可）
//   几何脸主体 = skins/default/（eyes/mouth/speech_bubble）
//   情绪       = elements/emotion.h（6 种）
//
// ★ 依赖：smooth_ui_toolkit（uitk::lvgl_cpp），已拉到 components/

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "display/lcd_display.h"
#include <esp_timer.h>
#include <atomic>
#include <mutex>
#include "hg_dj_logic.h"   // v3.6.7-dj
#include "audio/hg_eq.h"   // v3.6.7-dj2 equalizer
#include "audio/hg_rave.h" // v3.6.7-dj4 rave background

namespace stackchan::avatar {
class Avatar;
class DefaultAvatar;
}

class StackChanGeometryDisplay : public SpiLcdDisplay {
public:
    StackChanGeometryDisplay(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_handle_t panel,
                             int width, int height,
                             int offset_x, int offset_y,
                             bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~StackChanGeometryDisplay();

    // ── Display 接口 ────────────────────────────────────────
    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;

    // ★ 开机皮肤钩子：板卡注入，SetupUI 建好 UI 后回调
    //   （避免显示类反向依赖板卡）
    void SetBootSkinHook(std::function<void()> hook);

    // ── 皮肤切换 ────────────────────────────────────────────
    // show_geometry = true  → 显示几何脸，隐藏 GIF
    // show_geometry = false → 显示 GIF（Fairy），隐藏几何脸
    void SetGeometryVisible(bool visible);
    bool IsGeometryVisible() const { return geometry_visible_; }

    // ★ 说话时嘴开合动画（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    //   根因：DefaultMouth::setWeight(0~100) 控制张嘴，本类从没调它 ⇒ 恒为细线
    //   做法：照官方 SpeakingModifier —— 180ms 随机开合（开 40~80 / 闭 0~20）
    void TickMouth();                 // 由板卡 20ms 定时器驱动（内部限频 180ms）
    void SetSpeaking(bool speaking);  // 说话开始/结束
    bool IsSpeaking() const { return speaking_; }

    // ★ 装饰器 —— 官方 modifier 的「视觉反馈」层
    //   官方 5 个装饰器（Heart / Shy / Dizzy / Angry / Sweat）的代码与素材
    //   早已在 stackchan_avatar/decorators/ 并已编进固件，
    //   此前【从没有任何地方调用 addDecorator()】⇒ 永远不显示（漏接线）。
    //   ⇒ 这里逐个封装成方法，由板卡反应函数 / 情绪下发调用。
    //
    //   ⛔⛔ 全部只作用于【几何脸（官方小智皮肤）】——
    //     ① 内部判 `geometry_visible_`（Fairy 皮肤下直接 return，不建对象）
    //     ② parent 传 `geometry_root_`（几何脸容器）⇒ 随容器一起显隐
    //     ⇒ 与 Fairy 皮肤【完全不搭边】，两套皮肤互不干扰。
    //
    //   ★ 官方各自用在哪（读 official-stackchan/firmware/main/stackchan/）：
    //     · 摸头：Heart + Shy 一起冒（head_pet.h L90/L91 同一句里连加两个）
    //     · 摇晃：Dizzy（imu.h）
    //     · Angry / Sweat —— 官方【自己也没接线】（全仓库只有类定义、0 调用点）
    //       ⇒ 本项目接在「服务器下发的情绪」上（见 SetEmotion）
    void ShowHeart();   // 摸头   → 冒爱心
    void ShowShy();     // 摸头   → 脸红（官方就是跟爱心同时冒）
    void ShowDizzy();   // 摇晃   → 转圈晕眩
    void ShowAngry();   // 情绪 angry → 头顶怒符
    void ShowSweat();   // 情绪 sad   → 冒冷汗

    // ★★★ 驱动 avatar 的每帧更新 —— 装饰器动画与自动销毁的唯一入口
    //   官方：stackchan.h L136 `_avatar->update();`（主循环每帧调）
    //   avatar.h L23-37 的 update() 会：
    //     ① 遍历 key_elements → element->_update()（眨眼/呼吸）
    //     ② 遍历 _decorator_pool → decorator->_update()（爱心心跳/晕眩转动）
    //     ③ _decorator_pool.cleanup()（★ 到期的装饰器在此销毁）
    //   ⛔ 不调它的后果（用户实测过）：
    //     · 爱心/甩晕静止不动（不动态）
    //     · 装饰器永不过期 ⇒ 一直盖住表情 ⇒ 表情再也不变
    void AvatarTick();

    // 情绪名（与官方一致）：neutral/happy/angry/sad/doubt/sleepy
    // 另兼容小智下发的：laughing→happy、crying→sad、doubtful→doubt
    static const char* NormalizeEmotion(const char* emotion);

    // headphone-girl patch: map an emotion/state name onto a GIF that exists
    // in the emoji collection (pet, shake, sleeping, speaking, email, ...).
    const char* ResolveGifEmotion(const char* emotion);

private:
    void CreateGeometryLayer();
    void DestroyGeometryLayer();

    // ★ 官方几何脸（stackchan::avatar::DefaultAvatar）
    std::unique_ptr<stackchan::avatar::DefaultAvatar> avatar_;

    // ★ 装饰器 id（-1 = 当前没有）—— 重触发时【先撤旧的再挂新的】
    //   官方 head_pet.h 就是这么做的（removeDecorator 后紧跟 addDecorator）。
    //   ⛔ 不这么做：情绪反复下发会一层层叠上去，满屏都是怒符/汗滴
    int heart_id_ = -1;
    int shy_id_   = -1;
    int dizzy_id_ = -1;
    int angry_id_ = -1;
    int sweat_id_ = -1;

    lv_obj_t* geometry_root_ = nullptr;   // 几何脸根容器（浮在 emoji_box 之上）
    bool      geometry_visible_ = false;
    bool      geometry_built_ = false;
    std::function<void()> boot_skin_hook_;   // 开机皮肤回调（板卡注入）
    // ★ 说话嘴动画状态（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    bool     speaking_ = false;
    bool     mouth_open_ = false;
    uint32_t mouth_tick_cnt_ = 0;

    // headphone-girl patch: show the sleeping GIF after a quiet idle period
    esp_timer_handle_t idle_sleep_timer_ = nullptr;
    static void IdleSleepTimerCb(void* arg);
    void ArmIdleSleep(const char* resolved);

    // headphone-girl v3: real lip sync. While she speaks, the neutral GIF runs
    // (it already blinks) and a 40x44 mouth overlay is swapped every 40 ms
    // from the loudness of the PCM being played (hg_audio_meter.h).
public:
    static bool LipSyncEnabled();          // NVS hg/lipsync, default on
    static void SetLipSyncEnabled(bool on);

    // v3.5 party face: while music or a dance runs, every face is the happy
    // GIF with floating music notes (no lip sync). Listening and thinking
    // still show. Ending the party restores the last face.
    void SetPartyMode(bool on);
    bool IsPartyMode() const { return party_.load(); }
    // v3.5: red anger mark on the GIF face for about 2.5 s.
    void ShowAngerMark();
    // v3.5 talking lights: called from the lip sync timer with the mouth
    // level 0..1, and with -1 when the mouth stops. Must not block.
    void SetVoiceLevelHook(std::function<void(float)> hook) { voice_hook_ = std::move(hook); }

private:
    static constexpr int kMouthSets = 4;   // checked against hg_mouth_frames.h
    static constexpr int kMouthFrames = 8;
    static constexpr int kNotes = 6;
    void CreateMouthOverlay();             // in SetupUI, under the display lock
    void StartLipSync(int set, bool smile_rest);
    void StopLipSync();
    void LipTick();
    static void LipTimerCb(void* arg);
    void ShowGif(const char* name);        // parent SetEmotion + bookkeeping
    lv_obj_t* mouth_img_ = nullptr;
    lv_image_dsc_t mouth_dsc_[kMouthSets][kMouthFrames] = {};
    int mouth_count_ = 0;
    esp_timer_handle_t lip_timer_ = nullptr;
    std::atomic<bool> lip_active_{false};
    float lip_env_ = 0.0f;
    float lip_peak_ = 1500.0f;
    int lip_frame_ = 0;
    int64_t lip_frame_us_ = 0;
    std::atomic<int> lip_set_{0};
    std::atomic<bool> smile_rest_{false};
    std::function<void(float)> voice_hook_;
    int64_t voice_us_ = 0;

    // party
    std::atomic<bool> party_{false};
    std::string last_emotion_ = "neutral";   // restored when the party ends
    std::mutex emo_mutex_;
    struct Note { float x, y, vx, vy; int age, life; bool on; };
    Note notes_[kNotes] = {};
    lv_obj_t* note_img_[kNotes] = {};
    lv_image_dsc_t note_dsc_[2] = {};
    esp_timer_handle_t notes_timer_ = nullptr;
    int notes_tick_ = 0;
    void NotesTick();
    static void NotesTimerCb(void* arg);
    void StartNotes();
    void StopNotes();

    // ── v3.6.7-dj2: DJ screen (her face + 13-band equalizer) + scowl face ──
public:
    // DJ screen on top of her own GIF face (she stays on screen): 13 vertical
    // equalizer columns along the bottom, driven by the PCM that is actually
    // being played (hg_eq_task.h). No buttons; the board handles taps
    // (interrupt) and swipes (next / previous), LVGL gets no click.
    void SetDjMode(bool on);
    bool IsDjVisible() const { return dj_on_.load(); }
    // Kept for the board's wiring; the v3.6.7-dj2 screen shows no title.
    void SetDjTitleProvider(std::function<std::string()> f) { dj_title_fn_ = std::move(f); }
    // Equalizer hint: mode = purple | blue | gold | warm | rainbow | classic | on | off | pulse
    // (pulse_ms long). Safe from any task. false = unknown mode.
    bool SetDjVu(const std::string& mode, int pulse_ms);
    // v3.6.7-dj4 rave mode: while the DJ screen shows, the background behind
    // her ink lines changes color with the music (hg_rave.h). Session only
    // (not saved), default off. Safe from any task; fades in / out.
    void SetRave(bool on);
    bool RaveOn() const { return rave_want_.load(); }
private:
    std::atomic<int> dj_vu_palette_{0};        // hg_vu::Palette
    std::atomic<bool> dj_vu_on_{true};
    std::atomic<int64_t> dj_vu_pulse_until_{0};
    int dj_vu_palette_drawn_ = -1;
    std::atomic<uint32_t> dj_vu_hint_seq_{0};
    uint32_t dj_vu_hint_drawn_ = 0;
    bool dj_vu_on_drawn_ = true;
    void CreateDjOverlay();                // under the display lock
    void ColorDjColumn(int k, uint32_t c, uint32_t cap); // under the display lock
    void DjTick();
    static void DjTimerCb(void* arg);
    std::atomic<bool> dj_on_{false};
    std::function<std::string()> dj_title_fn_;
    esp_timer_handle_t dj_timer_ = nullptr;
    lv_obj_t* dj_root_ = nullptr;
    lv_obj_t* dj_eq_root_ = nullptr;
    lv_obj_t* dj_bar_[hg_eq::kBands] = {};
    lv_obj_t* dj_cap_[hg_eq::kBands] = {};
    int dj_bar_h_[hg_eq::kBands] = {};
    int dj_cap_y_[hg_eq::kBands] = {};
    uint8_t dj_target_[hg_eq::kBands] = {};
    bool dj_silent_ = true;
    int dj_tick_ = 0;
    hg_eq::Bars dj_bars_;
    hg_eq::ColorState dj_color_;              // v3.6.7-dj3: rotating palette + beat flash
    int64_t dj_beat_us_ = 0;
    uint32_t dj_col_drawn_[hg_eq::kBands] = {};
    // v3.6.7-dj4 rave mode
    lv_obj_t* dj_tint_ = nullptr;             // full screen, multiplied over her face
    lv_obj_t* dj_track_[hg_eq::kBands] = {};
    std::atomic<bool> rave_want_{false};
    std::atomic<bool> rave_reset_{false};
    std::atomic<bool> rave_tint_visible_{false};
    std::atomic<bool> dj_closing_{false};     // DJ off, rave tint still fading out
    hg_rave::Background rave_;
    int64_t rave_last_ms_ = -1000000;
    bool rave_beat_pending_ = false;
    uint16_t rave_drawn_565_ = 0xFFFF;
    float dj_energy_ = 0.0f;
    int64_t dj_stats_ms_ = 0;
    void ApplyRaveTint();                     // under the display lock
    static void RefrEventCb(lv_event_t* e);   // DJ screen refresh timing log

    // scowl: neutral GIF + two lowered black lids + the red anger mark held on
    void SetScowl(bool on);
    lv_obj_t* scowl_line_[2] = {};
    lv_point_precise_t scowl_pts_[2][2] = {};
    bool scowl_on_ = false;

    // anger mark
    lv_obj_t* anger_img_ = nullptr;
    lv_image_dsc_t anger_dsc_ = {};
    esp_timer_handle_t anger_timer_ = nullptr;
    static void AngerTimerCb(void* arg);
};
