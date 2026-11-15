// StackChan (CoreS3) 的「官方几何脸」皮肤 —— 实现
//
// ★ 设计（读 small 源码后确定）：
//   LcdDisplay 里已有 emoji_box_（GIF 表情容器）
//   本类再加 geometry_root_（几何脸容器，浮在最上层）
//   ⇒ 两者显隐互斥 ⇒ 切换 = 换显隐，不销毁重建
//
// ★ 素材/代码来源：official-stackchan（M5Stack 官方，MIT）
//   已搬到 ../stackchan_avatar/

#include "stackchan_geometry_display.h"
#include "board.h"
#include "led/led.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <cstring>

#include "stackchan_avatar/avatar.h"
#include "stackchan_avatar/decorators/decorators.h"  // ★ 爱心/晕眩装饰器
#include "stackchan_avatar/decorators/floating_hearts.h"  // ★ 漂浮爱心
#include <memory>
#include "settings.h"
#include "audio/hg_audio_meter.h"
#include "hg_mouth_frames.h"
#include "hg_notes.h"
#include "hg_lip.h"
#include "hg_eq_task.h"
#include "hg_meme_overlay.h"   // v3.6.7-dj6
#include <cstring>
#include "audio/hg_lipmap.h"
#include <esp_random.h>
#include <cmath>
#include <initializer_list>

#define TAG "StackChanGeometry"

using namespace stackchan::avatar;

// ── 情绪名归一化 ────────────────────────────────────────────
// 小智下发的是它自己的词汇（happy/laughing/crying/doubtful...），
// 官方几何脸只认 6 个（neutral/happy/angry/sad/doubt/sleepy）。
const char* StackChanGeometryDisplay::NormalizeEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return "neutral";
    }
    if (strcmp(emotion, "neutral")   == 0) return "neutral";
    if (strcmp(emotion, "happy")     == 0) return "happy";
    if (strcmp(emotion, "laughing")  == 0) return "happy";    // 同义
    if (strcmp(emotion, "angry")     == 0) return "angry";
    if (strcmp(emotion, "scowl")     == 0) return "angry";    // v3.6.7-dj
    if (strcmp(emotion, "sad")       == 0) return "sad";
    if (strcmp(emotion, "crying")    == 0) return "sad";      // 同义
    if (strcmp(emotion, "sleepy")    == 0) return "sleepy";
    if (strcmp(emotion, "doubtful")  == 0) return "doubt";    // 同义
    if (strcmp(emotion, "doubt")     == 0) return "doubt";
    // headphone-girl patch: extra names used by the GIF set / device states
    if (strcmp(emotion, "loving") == 0 || strcmp(emotion, "pet") == 0 ||
        strcmp(emotion, "love") == 0)                       return "happy";
    if (strcmp(emotion, "shake") == 0 || strcmp(emotion, "dizzy") == 0 ||
        strcmp(emotion, "confused") == 0)                   return "doubt";
    if (strcmp(emotion, "sleeping") == 0)                   return "sleepy";
    if (strcmp(emotion, "listening") == 0 || strcmp(emotion, "speaking") == 0 ||
        strcmp(emotion, "thinking") == 0 || strcmp(emotion, "email") == 0 ||
        strcmp(emotion, "look_left") == 0 || strcmp(emotion, "look_right") == 0)
                                                            return "neutral";
    ESP_LOGD(TAG, "unknown emotion '%s' -> neutral", emotion);
    return "neutral";
}

static Emotion ToEmotionEnum(const char* norm) {
    if (strcmp(norm, "happy")  == 0) return Emotion::Happy;
    if (strcmp(norm, "angry")  == 0) return Emotion::Angry;
    if (strcmp(norm, "sad")    == 0) return Emotion::Sad;
    if (strcmp(norm, "doubt")  == 0) return Emotion::Doubt;
    if (strcmp(norm, "sleepy") == 0) return Emotion::Sleepy;
    return Emotion::Neutral;
}

// ── 构造 / 析构 ─────────────────────────────────────────────
StackChanGeometryDisplay::StackChanGeometryDisplay(
    esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, int offset_x, int offset_y, bool mirror_x,
    bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {
    ESP_LOGI(TAG, "constructed (%dx%d)", width, height);
}

StackChanGeometryDisplay::~StackChanGeometryDisplay() {
    if (idle_sleep_timer_ != nullptr) {
        esp_timer_stop(idle_sleep_timer_);
        esp_timer_delete(idle_sleep_timer_);
        idle_sleep_timer_ = nullptr;
    }
    for (esp_timer_handle_t* t : {&lip_timer_, &notes_timer_, &anger_timer_}) {
        if (*t != nullptr) {
            esp_timer_stop(*t);
            esp_timer_delete(*t);
            *t = nullptr;
        }
    }
    DestroyGeometryLayer();
}

// ── 建几何脸层（黑底 + 眼/嘴/眉）────────────────────────────
void StackChanGeometryDisplay::CreateGeometryLayer() {
    if (geometry_built_) {
        return;
    }

    // 挂在 LVGL 顶层（screen），盖住 emoji_box_
    lv_obj_t* scr = lv_screen_active();
    if (scr == nullptr) {
        ESP_LOGE(TAG, "lv_screen_active() == nullptr");
        return;
    }

    // 根容器：整屏、黑底、不可滚动、默认隐藏
    geometry_root_ = lv_obj_create(scr);
    lv_obj_set_size(geometry_root_, width_, height_);
    lv_obj_center(geometry_root_);
    lv_obj_set_style_bg_color(geometry_root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(geometry_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(geometry_root_, 0, 0);
    lv_obj_set_style_radius(geometry_root_, 0, 0);
    lv_obj_set_style_pad_all(geometry_root_, 0, 0);
    lv_obj_remove_flag(geometry_root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);   // 默认不显示
    // 不拦触摸（保持小智原本的触屏交互）
    lv_obj_remove_flag(geometry_root_, LV_OBJ_FLAG_CLICKABLE);

    // 官方几何脸：黑底 + 白线（primaryColor 白 / secondaryColor 黑）
    avatar_ = std::make_unique<DefaultAvatar>();
    avatar_->primaryColor   = lv_color_white();
    avatar_->secondaryColor = lv_color_black();
    avatar_->init(geometry_root_, &lv_font_montserrat_14);
    avatar_->setEmotion(Emotion::Neutral);

    geometry_built_ = true;
    ESP_LOGI(TAG, "geometry layer created (%dx%d, black bg)", width_,
             height_);
}

void StackChanGeometryDisplay::DestroyGeometryLayer() {
    if (!geometry_built_) {
        return;
    }
    avatar_.reset();          // 先删 avatar（它的容器挂在 root 下）
    if (geometry_root_ != nullptr) {
        lv_obj_del(geometry_root_);
        geometry_root_ = nullptr;
    }
    geometry_built_ = false;
    ESP_LOGI(TAG, "geometry layer destroyed");
}

// ── 皮肤切换 ────────────────────────────────────────────────
void StackChanGeometryDisplay::SetGeometryVisible(bool visible) {
    DisplayLockGuard lock(this);

    if (visible && !geometry_built_) {
        CreateGeometryLayer();
    }
    if (!geometry_built_) {
        return;   // 建失败
    }

    geometry_visible_ = visible;
    if (visible) {
        // v3.5: the GIF-face overlays are not inside emoji_box_; hide them too
        if (lip_active_.exchange(false)) {
            hg_lip::SetActive(false);
            if (lip_timer_ != nullptr) esp_timer_stop(lip_timer_);
        }
        if (notes_timer_ != nullptr) esp_timer_stop(notes_timer_);
        if (anger_timer_ != nullptr) esp_timer_stop(anger_timer_);
        if (dj_root_ != nullptr) lv_obj_add_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);   // v3.6.7-dj
        // v3.6.7-dj4: the rave tint starts again from white (fades in) next time
        if (dj_tint_ != nullptr) lv_obj_add_flag(dj_tint_, LV_OBJ_FLAG_HIDDEN);
        for (auto* t : dj_track_) if (t != nullptr) lv_obj_set_style_bg_opa(t, 70, 0);
        rave_tint_visible_.store(false);
        rave_drawn_565_ = 0xFFFF;
        rave_reset_.store(true);
        if (dj_closing_.exchange(false) && dj_timer_ != nullptr) esp_timer_stop(dj_timer_);
        for (auto* l : scowl_line_) if (l != nullptr) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        scowl_on_ = false;
        if (mouth_img_ != nullptr) lv_obj_add_flag(mouth_img_, LV_OBJ_FLAG_HIDDEN);
        if (anger_img_ != nullptr) lv_obj_add_flag(anger_img_, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < kNotes; ++i) {
            notes_[i].on = false;
            if (note_img_[i] != nullptr) lv_obj_add_flag(note_img_[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);
        // 藏掉 GIF 表情，避免两套同时显示（用户要求：不是叠加，是二选一）
        if (emoji_box_ != nullptr) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        // 停掉 GIF 解码，释放那 ~384KB（用户问过内存）
        if (gif_controller_) {
            gif_controller_->Stop();
        }
        // 黑底 ⇒ 把字幕/聊天文字也藏掉，保持"官方味"
        if (chat_message_label_ != nullptr) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_ != nullptr) {
            lv_obj_clear_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        if (gif_controller_) {
            gif_controller_->Start();
        }
        if (party_.load() && !dj_on_.load()) StartNotes();   // v3.5: back to the GIF face mid-song
        if (dj_on_.load() && dj_root_ != nullptr) lv_obj_clear_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);
        if (chat_message_label_ != nullptr) {
            lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ESP_LOGI(TAG, "geometry %s", visible ? "ON" : "OFF");
}

// ── 说话嘴动画（用户报「说话的时候嘴怎么不会动，只有一条线」）──
//
// ★ 为什么要这样写（读官方源码得出）：
//   DefaultMouth::setWeight(0~100)：
//     weight=0   → 90x6 的细线（闭嘴）
//     weight=100 → 60x50 的圆（大张嘴）
//   本类原先只调 setEmotion，从没调 setWeight ⇒ 嘴恒为 0 ⇒ 永远一条线。
//
//   官方 SpeakingModifier（official-stackchan/firmware/main/stackchan/
//   modifiers/speaking.h）的做法：每 180ms 随机开合一次
//     开 = random(40..80)，闭 = random(0..20)
//   ⇒ 不读音频，纯随机开合，视觉上就像在说话。
//   本实现照抄同一套参数，保证观感与官方一致。
void StackChanGeometryDisplay::SetSpeaking(bool speaking) {
    if (speaking_ == speaking) {
        return;
    }
    speaking_ = speaking;
    DisplayLockGuard lock(this);
    if (!speaking_ && avatar_) {
        avatar_->mouth().setWeight(0);   // 说完闭嘴（回到细线）
    }
    ESP_LOGI(TAG, "speaking %s", speaking ? "ON" : "OFF");
}

void StackChanGeometryDisplay::TickMouth() {
    if (!speaking_ || !geometry_visible_ || avatar_ == nullptr) {
        return;
    }
    // 板卡定时器 20ms 一跳 ⇒ 每 9 跳 = 180ms 调一次（与官方 interval 一致）
    if (++mouth_tick_cnt_ % 9 != 0) {
        return;
    }
    mouth_open_ = !mouth_open_;
    int w;
    if (mouth_open_) {
        w = 40 + (int)(esp_random() % 41);   // 40..80
    } else {
        w = (int)(esp_random() % 21);        // 0..20
    }
    DisplayLockGuard lock(this);
    avatar_->mouth().setWeight(w);
}

// ── 装饰器：摸头冒爱心/脸红、甩晕、情绪怒/汗 ─────────────────
//
// ★ 为什么之前"没做"：
//   官方装饰器（HeartDecorator / DizzyDecorator / Shy / Sweat / Angry）
//   的代码早已移植进 stackchan_avatar/decorators/，
//   但【从没有任何地方调用 addDecorator()】⇒ 永远不显示。
//   这是"漏接线"，不是"做不到"。
//
// ★ 官方签名（读 decorators.h）：
//     HeartDecorator (lv_obj_t* parent, uint32_t destroyAfterMs = 0,
//                     uint32_t animationIntervalMs = 500);
//     AngryDecorator (…, 500 默认)   SweatDecorator (…, 700 默认)
//     ShyDecorator   (lv_obj_t* parent, uint32_t destroyAfterMs = 0)  ← 无动画参数
//     avatar.addDecorator(std::unique_ptr<Decorator>(...))  → int id
//     avatar.removeDecorator(int id)
//   destroyAfterMs = 生命周期（到点自动销毁，避免堆积）
//
// ⛔⛔ 五个方法全部【只作用于几何脸（官方小智皮肤）】：
//     判 geometry_visible_ + parent 传 geometry_root_
//     ⇒ Fairy 皮肤下不建对象、不显示。
//
// ★ 统一的 3 步写法（照官方 head_pet.h）：
//     ① 加锁（LVGL 非线程安全，必须）
//     ② 重触发时【先撤旧的】—— 否则情绪反复下发会叠一堆
//     ③ addDecorator 并把返回的 id 记下来，下次撤它

// ── 内部小工具：撤掉同类的旧装饰器 ──
static inline void DropIfAny(std::unique_ptr<stackchan::avatar::DefaultAvatar>& av,
                             int& id) {
    if (id >= 0) {
        av->removeDecorator(id);
        id = -1;
    }
}

void StackChanGeometryDisplay::ShowHeart() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 用户反馈：「爱心漂浮，但效果不佳，不如做回原来的爱心样式」
    //   ⇒ 回退到【官方 HeartDecorator】（原地心跳跳动）
    //   参数对齐官方 heart.cpp：动画 500ms 一跳（150°/200° 来回）
    //   存活 1.5 秒后自动销毁（靠 AvatarTick → cleanup）
    DropIfAny(avatar_, heart_id_);
    heart_id_ = avatar_->addDecorator(
        std::make_unique<HeartDecorator>(geometry_root_, 1500, 500));
    ESP_LOGI(TAG, "冒爱心");
}

void StackChanGeometryDisplay::ShowShy() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 官方摸头时是【爱心 + 腮红一起冒】（head_pet.h L90/L91），
    //   本项目照抄 —— 所以摸头会同时看到心跳和两张脸红。
    //   ShyDecorator 是两张图（左 -108,28 / 右 +108,28），
    //   颜色 0xF7A59E（官方默认），无动画、有生命周期。
    DropIfAny(avatar_, shy_id_);
    shy_id_ = avatar_->addDecorator(
        std::make_unique<ShyDecorator>(geometry_root_, 2000));
    ESP_LOGI(TAG, "脸红");
}

void StackChanGeometryDisplay::ShowDizzy() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 参数对齐官方 dizzy.cpp（动画间隔走官方默认，转圈才明显）
    //   存活 2 秒后自动销毁
    DropIfAny(avatar_, dizzy_id_);
    dizzy_id_ = avatar_->addDecorator(
        std::make_unique<DizzyDecorator>(geometry_root_, 2000, 300));
    ESP_LOGI(TAG, "转圈晕眩");
}

void StackChanGeometryDisplay::ShowAngry() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 官方【没有】任何调用点（只有类定义）⇒ 触发时机由本项目定：
    //   接在服务器下发的情绪上（SetEmotion → "angry"）。
    //   官方默认：颜色 0xFDB034，位置 (108,-70)（右上角），
    //   动画帧 150°/200° 来回 —— 参数全走官方默认，只是给个生命周期。
    DropIfAny(avatar_, angry_id_);
    angry_id_ = avatar_->addDecorator(
        std::make_unique<AngryDecorator>(geometry_root_, 2500, 500));
    ESP_LOGI(TAG, "冒怒符");
}

void StackChanGeometryDisplay::ShowSweat() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 同 Angry：官方自己没接，本项目接在「难过/委屈」的情绪上。
    //   官方默认动画间隔 700ms，走官方默认值。
    DropIfAny(avatar_, sweat_id_);
    sweat_id_ = avatar_->addDecorator(
        std::make_unique<SweatDecorator>(geometry_root_, 2500, 700));
    ESP_LOGI(TAG, "冒汗");
}

// ★★★ 驱动 avatar 每帧更新（装饰器动画 + 自动销毁）
//
//   官方依据：stackchan.h L136 `_avatar->update();`
//   avatar.h L23-37 的 update() 做三件事：
//     ① key_elements 各自 _update()（眨眼/呼吸等）
//     ② _decorator_pool 各自 _update()（爱心心跳旋转 / 晕眩转圈）
//     ③ _decorator_pool.cleanup()  ← 到期的装饰器在这里被销毁
//
//   ⛔ 不调它 = 用户实测的三个现象：
//     · 「爱心不是漂浮的，甩晕也不是动态的」→ 动画从不推进
//     · 「一旦冒出爱心和甩晕，表情就不会变了，被这两个覆盖」
//       → requestDestroy() 从不触发 ⇒ 装饰器永不过期 ⇒ 一直挡着
//
//   ★ 调用频度：由板卡 20ms 轮询定时器调（50Hz，足够流畅）
//     只在几何脸显示时才需要（Fairy 模式走 GIF，没 avatar）
void StackChanGeometryDisplay::AvatarTick() {
    if (!geometry_visible_ || avatar_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    avatar_->update();
}

// ── Display 接口覆写 ────────────────────────────────────────
void StackChanGeometryDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI called twice, skip");
        return;
    }
    // 先让父类把基础 UI 建好（emoji_box_ 等）
    SpiLcdDisplay::SetupUI();

    // ★ 读 NVS 决定开机用哪套皮肤（在 UI 建好之后）
    //   这里用 weak 回调避免显示类依赖板卡：板卡通过 SetBootSkinHook 注入
    if (boot_skin_hook_) {
        boot_skin_hook_();
    }

    CreateMouthOverlay();   // headphone-girl v3 lip sync
    ESP_LOGI(TAG, "SetupUI done");
}

void StackChanGeometryDisplay::SetBootSkinHook(std::function<void()> hook) {
    boot_skin_hook_ = std::move(hook);
}

// ── headphone-girl patch: GIF alias resolution ─────────────────────────────
// File stems in fairy-assets/ become emoji names (scripts/build_default_assets.py);
// _emote_aliases.json is NOT read by the firmware, so aliases live here.
// v3.5: angry no longer shows the sad face (neutral face + red anger mark),
// surprised shows the startled head shake instead of the laughing face, and
// embarrassed shows the blushing pet face instead of the sad one.
namespace {
struct GifAlias { const char* name; const char* candidates[3]; };
const GifAlias kGifAliases[] = {
    {"loving",    {"pet", "happy", nullptr}},
    {"love",      {"pet", "happy", nullptr}},
    {"kissy",     {"pet", "happy", nullptr}},
    {"pet",       {"pet", "happy", nullptr}},
    {"doubtful",  {"shake", "neutral", nullptr}},
    {"dizzy",     {"shake", "neutral", nullptr}},
    {"confused",  {"shake", "neutral", nullptr}},
    {"shocked",   {"shake", "neutral", nullptr}},
    {"surprised", {"shake", "happy", nullptr}},
    {"sleepy",    {"sleeping", "neutral", nullptr}},
    {"talking",   {"speaking", "neutral", nullptr}},
    {"speaking",  {"speaking", "neutral", nullptr}},
    {"listening", {"listening", "neutral", nullptr}},
    {"thinking",  {"thinking", "neutral", nullptr}},
    {"mail",      {"email", "happy", nullptr}},
    {"sending",   {"email", "happy", nullptr}},
    {"laughing",  {"happy", nullptr, nullptr}},
    {"funny",     {"happy", nullptr, nullptr}},
    {"winking",   {"happy", nullptr, nullptr}},
    {"cool",      {"happy", nullptr, nullptr}},
    {"confident", {"happy", nullptr, nullptr}},
    {"relaxed",   {"happy", nullptr, nullptr}},
    {"delicious", {"happy", nullptr, nullptr}},
    {"silly",     {"happy", nullptr, nullptr}},
    {"crying",    {"sad", nullptr, nullptr}},
    {"embarrassed", {"pet", "sad", nullptr}},
    {"angry",     {"neutral", nullptr, nullptr}},
    {"scowl",     {"neutral", nullptr, nullptr}},   // v3.6.7-dj: + lids + held anger mark
};

bool OneOf(const std::string& s, std::initializer_list<const char*> names) {
    for (const char* n : names) if (s == n) return true;
    return false;
}
}  // namespace

const char* StackChanGeometryDisplay::ResolveGifEmotion(const char* emotion) {
    if (emotion == nullptr) return "neutral";
    auto theme = static_cast<LvglTheme*>(current_theme_);
    auto collection = theme != nullptr ? theme->emoji_collection() : nullptr;
    if (collection == nullptr) return emotion;
    if (collection->GetEmojiImage(emotion) != nullptr) return emotion;
    for (const auto& a : kGifAliases) {
        if (strcmp(a.name, emotion) != 0) continue;
        for (const char* c : a.candidates) {
            if (c != nullptr && collection->GetEmojiImage(c) != nullptr) return c;
        }
    }
    return collection->GetEmojiImage("neutral") != nullptr ? "neutral" : emotion;
}

void StackChanGeometryDisplay::IdleSleepTimerCb(void* arg) {
    auto self = static_cast<StackChanGeometryDisplay*>(arg);
    // v3.5: never doze off while music or a dance is going
    if (self->party_.load()) return;
    self->SetEmotion("sleepy");
}

void StackChanGeometryDisplay::ArmIdleSleep(const char* resolved) {
    if (idle_sleep_timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = &StackChanGeometryDisplay::IdleSleepTimerCb;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "hg_idle_sleep";
        if (esp_timer_create(&args, &idle_sleep_timer_) != ESP_OK) {
            idle_sleep_timer_ = nullptr;
            return;
        }
    }
    esp_timer_stop(idle_sleep_timer_);
    // Neutral for 120 s in a row => resting face. Any other emotion cancels.
    if (resolved != nullptr && strcmp(resolved, "neutral") == 0 && !party_.load()) {
        esp_timer_start_once(idle_sleep_timer_, 120ULL * 1000 * 1000);
    }
}

void StackChanGeometryDisplay::ShowGif(const char* name) {
    SpiLcdDisplay::SetEmotion(ResolveGifEmotion(name));
}

void StackChanGeometryDisplay::SetEmotion(const char* emotion) {
    std::string e = emotion != nullptr ? emotion : "neutral";
    // v3.5: "speaking" or "speaking:<mood>" = she is talking in that mood
    bool talking = false;
    std::string mood;
    if (e.rfind("speaking", 0) == 0) {
        talking = true;
        size_t c = e.find(':');
        if (c != std::string::npos) mood = e.substr(c + 1);
    }
    {
        std::lock_guard<std::mutex> l(emo_mutex_);
        last_emotion_ = e;
    }
    // LED tint per emotion (no-op unless the board LED implements it)
    if (auto led = Board::GetInstance().GetLed(); led != nullptr) {
        led->OnEmotion(talking ? (mood.empty() ? "speaking" : mood.c_str()) : e.c_str());
    }

    if (geometry_visible_) {
        const char* norm = NormalizeEmotion(
            talking ? (mood.empty() ? "neutral" : mood.c_str()) : e.c_str());
        // 几何脸模式：用官方几何脸自己的表情
        {
            DisplayLockGuard lock(this);
            if (avatar_) {
                avatar_->setEmotion(ToEmotionEnum(norm));
                // 睡觉时顺带给个台词（官方也是这么做的）
                if (strcmp(norm, "sleepy") == 0) {
                    avatar_->setSpeech("Zzz…");
                } else {
                    avatar_->clearSpeech();
                }
            }
        }
        // ★ 情绪装饰器（官方自己没接线，本项目接上）
        //   ⛔ 必须在锁【外】调用 —— ShowXxx 内部自己加 DisplayLockGuard
        if (strcmp(norm, "angry") == 0) {
            ShowAngry();     // 服务器下发 angry → 头顶怒符
        } else if (strcmp(norm, "sad") == 0) {
            ShowSweat();     // 服务器下发 sad/crying → 冒冷汗
        }
        return;
    }

    // v3.5 party: music or dance => happy face + notes, no lip sync.
    // Listening and thinking still show so she looks attentive.
    if (party_.load() && !(e == "listening" || e == "thinking")) {
        if (idle_sleep_timer_ != nullptr) esp_timer_stop(idle_sleep_timer_);
        StopLipSync();
        SetScowl(false);
        ShowGif("happy");
        return;
    }
    // v3.6.7-dj: scowl = neutral GIF + lowered lids + red anger mark held on
    const bool scowl = (e == "scowl") || (talking && mood == "scowl");

    // v3.5: lip sync on the matching face for the mood
    if (talking && mouth_img_ != nullptr && LipSyncEnabled()) {
        int set = 0;                 // 0 neutral, 1 sad, 2 thinking, 3 pet
        const char* gif = "neutral";
        bool smile = true;
        bool anger = false;
        if (OneOf(mood, {"sad", "crying"})) {
            set = 1; gif = "sad"; smile = false;
        } else if (mood == "thinking") {
            set = 2; gif = "thinking"; smile = false;
        } else if (OneOf(mood, {"loving", "love", "kissy", "pet", "embarrassed"})) {
            set = 3; gif = "pet"; smile = false;
        } else if (mood == "angry" || mood == "scowl") {
            anger = true; smile = false;
        } else if (OneOf(mood, {"neutral", "sleepy", "confused", "doubtful", "shocked",
                                "surprised", "dizzy"})) {
            smile = false;
        }
        if (strcmp(ResolveGifEmotion(gif), gif) != 0) { set = 0; gif = "neutral"; }
        if (idle_sleep_timer_ != nullptr) esp_timer_stop(idle_sleep_timer_);
        SpiLcdDisplay::SetEmotion(ResolveGifEmotion(gif));
        StartLipSync(set, smile);
        SetScowl(scowl && set == 0);
        if (anger && !scowl) ShowAngerMark();
        return;
    }

    // Fairy 模式：交回父类（走 GIF）
    const char* resolved = ResolveGifEmotion(talking ? "speaking" : e.c_str());
    ArmIdleSleep(resolved);
    StopLipSync();
    SpiLcdDisplay::SetEmotion(resolved);
    SetScowl(scowl && strcmp(resolved, "neutral") == 0);
    if (e == "angry") ShowAngerMark();
}

void StackChanGeometryDisplay::SetStatus(const char* status) {
    if (geometry_visible_) {
        // 几何脸模式下不显示状态栏文字（用户说"状态栏不写也没关系"）
        return;
    }
    SpiLcdDisplay::SetStatus(status);
}

void StackChanGeometryDisplay::SetChatMessage(const char* role,
                                              const char* content) {
    // v3.6.7-dj6: a new voice turn (what the user said) hides a meme early.
    if (role != nullptr && strcmp(role, "user") == 0 && content != nullptr && content[0] != '\0') {
        hg_meme::Dismiss("voice turn");
    }
    if (geometry_visible_) {
        // 几何脸模式下不显示字幕（保持官方那套纯脸效果）
        return;
    }
    SpiLcdDisplay::SetChatMessage(role, content);
}

// ── headphone-girl lip sync (v3, vowels and mood faces in v3.5) ────────────
// Frames (hg_mouth_frames.h, from Alex's art): 0 closed, 1 half, 2 open (A),
// 3 wide (E), 4 round (O), 5 small (U), 6 wide teeth (I), 7 closed smile.
// Four sets: neutral, sad, thinking, pet (each drawn for that GIF's mouth).
// The vowel comes from hg_lip (avmic formant analysis, MIT, Takao Akaki);
// without a confident vowel the v3.4 loudness + zero-crossing rule is used.
bool StackChanGeometryDisplay::LipSyncEnabled() {
    Settings s("hg", false);
    return s.GetInt("lipsync", 1) != 0;
}

void StackChanGeometryDisplay::SetLipSyncEnabled(bool on) {
    Settings s("hg", true);
    s.SetInt("lipsync", on ? 1 : 0);
}

static_assert(HG_MOUTH_SET_COUNT == 4, "update kMouthSets");
static_assert(HG_MOUTH_COUNT == 8, "update kMouthFrames");

namespace {
const uint32_t kNoteColors[] = {0xFF4FA0, 0xFFB000, 0x00B8D9, 0x8E5CFF, 0x2EBD5F, 0xFF7A00};

void InitA8(lv_image_dsc_t& d, const uint8_t* data, int w, int h) {
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_A8;
    d.header.w = w;
    d.header.h = h;
    d.header.stride = w;
    d.data_size = w * h;
    d.data = data;
}

lv_obj_t* MakeGlyph(lv_obj_t* parent, const lv_image_dsc_t* dsc, uint32_t color) {
    lv_obj_t* o = lv_image_create(parent);
    if (o == nullptr) return nullptr;
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(o, dsc);
    lv_obj_set_style_image_recolor(o, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
}  // namespace

void StackChanGeometryDisplay::CreateMouthOverlay() {
    if (mouth_img_ != nullptr || emoji_image_ == nullptr) return;
    mouth_count_ = kMouthFrames;
    for (int s = 0; s < kMouthSets; ++s) {
        const HgMouthSet& set = hg_mouth_sets[s];
        for (int i = 0; i < kMouthFrames; ++i) {
            auto& d = mouth_dsc_[s][i];
            d.header.magic = LV_IMAGE_HEADER_MAGIC;
            d.header.cf = LV_COLOR_FORMAT_RGB565;
            d.header.w = set.w;
            d.header.h = set.h;
            d.header.stride = set.w * 2;
            d.data_size = set.w * set.h * 2;
            d.data = set.frames[i];
        }
    }
    InitA8(note_dsc_[0], hg_note8_a8, HG_NOTE8_W, HG_NOTE8_H);
    InitA8(note_dsc_[1], hg_note16_a8, HG_NOTE16_W, HG_NOTE16_H);
    InitA8(anger_dsc_, hg_anger_a8, HG_ANGER_W, HG_ANGER_H);

    DisplayLockGuard lock(this);
    lv_obj_t* parent = lv_obj_get_parent(emoji_box_ != nullptr ? emoji_box_ : emoji_image_);
    if (parent == nullptr) parent = lv_screen_active();
    mouth_img_ = lv_image_create(parent);
    if (mouth_img_ == nullptr) return;
    lv_obj_add_flag(mouth_img_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(mouth_img_, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(mouth_img_, &mouth_dsc_[0][0]);
    lv_obj_add_flag(mouth_img_, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < kNotes; ++i) {
        note_img_[i] = MakeGlyph(parent, &note_dsc_[i % 2], kNoteColors[i % 6]);
    }
    anger_img_ = MakeGlyph(parent, &anger_dsc_, 0xE0202A);
    ESP_LOGI(TAG, "lip sync overlay ready (%d sets x %d frames, party notes)", kMouthSets,
             mouth_count_);
}

void StackChanGeometryDisplay::LipTimerCb(void* arg) {
    static_cast<StackChanGeometryDisplay*>(arg)->LipTick();
}

void StackChanGeometryDisplay::StartLipSync(int set, bool smile_rest) {
    if (mouth_img_ == nullptr) return;
    if (set < 0 || set >= kMouthSets) set = 0;
    if (lip_timer_ == nullptr) {
        esp_timer_create_args_t a = {};
        a.callback = &StackChanGeometryDisplay::LipTimerCb;
        a.arg = this;
        a.dispatch_method = ESP_TIMER_TASK;
        a.name = "hg_lipsync";
        a.skip_unhandled_events = true;
        if (esp_timer_create(&a, &lip_timer_) != ESP_OK) { lip_timer_ = nullptr; return; }
    }
    const int rest = smile_rest ? 7 : 0;
    {
        DisplayLockGuard lock(this);
        lip_set_.store(set);
        smile_rest_.store(smile_rest);
        lv_obj_align_to(mouth_img_, emoji_image_, LV_ALIGN_TOP_LEFT, hg_mouth_sets[set].x,
                        hg_mouth_sets[set].y);
        lip_frame_ = rest;
        lv_image_set_src(mouth_img_, &mouth_dsc_[set][rest]);
        lv_obj_remove_flag(mouth_img_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(mouth_img_);
    }
    lip_env_ = 0.0f;
    hg_lip::SetActive(true);
    if (!lip_active_.exchange(true)) {
        esp_timer_start_periodic(lip_timer_, 40 * 1000);
    }
}

void StackChanGeometryDisplay::StopLipSync() {
    if (!lip_active_.exchange(false)) return;
    hg_lip::SetActive(false);
    if (lip_timer_ != nullptr) esp_timer_stop(lip_timer_);
    if (voice_hook_) voice_hook_(-1.0f);
    if (mouth_img_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_obj_add_flag(mouth_img_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StackChanGeometryDisplay::LipTick() {
    if (!lip_active_.load() || mouth_img_ == nullptr) return;
    uint16_t rms = 0;
    uint8_t zcr = 0, vowel = 0, vconf = 0;
    bool live = hg_meter::Sample(&rms, &zcr, &vowel, &vconf);
    float x = live ? (float)rms : 0.0f;
    // fast attack, slower release, adaptive peak so loud and quiet voices both move
    lip_env_ = (x > lip_env_) ? (0.65f * x + 0.35f * lip_env_) : (0.55f * lip_env_ + 0.45f * x);
    lip_peak_ = lip_peak_ * 0.995f;
    if (lip_env_ > lip_peak_) lip_peak_ = lip_env_;
    if (lip_peak_ < 1200.0f) lip_peak_ = 1200.0f;
    float level = lip_env_ / lip_peak_;

    if (voice_hook_) voice_hook_(live ? level : 0.0f);   // talking lights

    const bool smile = smile_rest_.load();
    int want = hg_lipmap::PickMouth(live, level, zcr, vowel, vconf, smile);
    if (want < 0 || want >= mouth_count_) want = 0;
    const int rest = smile ? 7 : 0;
    int64_t now = esp_timer_get_time();
    // hold each shape >= 70 ms (closing is allowed sooner) so it reads as speech, not flicker
    if (want == lip_frame_) return;
    if (want != rest && now - lip_frame_us_ < 70 * 1000) return;
    // v3.3: never block the shared esp_timer task on the LVGL lock.
    if (!lvgl_port_lock(2)) return;
    if (lip_active_.load()) {
        lip_frame_ = want;
        lip_frame_us_ = now;
        lv_image_set_src(mouth_img_, &mouth_dsc_[lip_set_.load()][want]);
    }
    lvgl_port_unlock();
}

// ── v3.5 party face: happy GIF + floating music notes ──────────────────────
// FloatingHearts-style: notes rise from the empty strips left and right of
// her head, sway, fade in and out. Drawn in code (A8 glyphs, recolored).
void StackChanGeometryDisplay::SetPartyMode(bool on) {
    if (party_.exchange(on) == on) return;
    ESP_LOGI(TAG, "party face %s", on ? "on" : "off");
    if (geometry_visible_ || mouth_img_ == nullptr) return;
    if (on) {
        if (idle_sleep_timer_ != nullptr) esp_timer_stop(idle_sleep_timer_);
        std::string last;
        {
            std::lock_guard<std::mutex> l(emo_mutex_);
            last = last_emotion_;
        }
        if (!(last == "listening" || last == "thinking")) {
            StopLipSync();
            ShowGif("happy");
        }
        if (!dj_on_.load()) StartNotes();   // v3.6.7-dj: the DJ screen has its own show
    } else {
        StopNotes();
        std::string last;
        {
            std::lock_guard<std::mutex> l(emo_mutex_);
            last = last_emotion_;
        }
        SetEmotion(last.c_str());   // back to whatever she should show now
    }
}

void StackChanGeometryDisplay::NotesTimerCb(void* arg) {
    static_cast<StackChanGeometryDisplay*>(arg)->NotesTick();
}

void StackChanGeometryDisplay::StartNotes() {
    if (notes_timer_ == nullptr) {
        esp_timer_create_args_t a = {};
        a.callback = &StackChanGeometryDisplay::NotesTimerCb;
        a.arg = this;
        a.dispatch_method = ESP_TIMER_TASK;
        a.name = "hg_notes";
        a.skip_unhandled_events = true;
        if (esp_timer_create(&a, &notes_timer_) != ESP_OK) { notes_timer_ = nullptr; return; }
    }
    for (auto& n : notes_) n.on = false;
    notes_tick_ = 0;
    esp_timer_stop(notes_timer_);
    esp_timer_start_periodic(notes_timer_, 40 * 1000);
}

void StackChanGeometryDisplay::StopNotes() {
    if (notes_timer_ != nullptr) esp_timer_stop(notes_timer_);
    DisplayLockGuard lock(this);
    for (int i = 0; i < kNotes; ++i) {
        notes_[i].on = false;
        if (note_img_[i] != nullptr) lv_obj_add_flag(note_img_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void StackChanGeometryDisplay::NotesTick() {
    if (!party_.load() || emoji_image_ == nullptr || dj_on_.load()) return;
    if (!lvgl_port_lock(2)) return;       // never stall the esp_timer task
    ++notes_tick_;
    lv_area_t a;
    lv_obj_get_coords(emoji_image_, &a);  // GIF top-left on screen
    // spawn one note every 0.4 s into a free slot, alternating sides
    if (notes_tick_ % 10 == 1) {
        for (int i = 0; i < kNotes; ++i) {
            Note& n = notes_[i];
            if (n.on || note_img_[i] == nullptr) continue;
            const bool left = (notes_tick_ / 10) % 2 == 0;
            uint32_t r = esp_random();
            n.x = left ? 4.0f + (float)(r % 26) : 272.0f + (float)(r % 22);
            n.y = 176.0f + (float)((r >> 8) % 30);
            n.vy = -(1.1f + (float)((r >> 16) % 8) * 0.1f);   // 28..45 px/s up
            n.vx = left ? 0.12f : -0.12f;
            n.age = 0;
            n.life = 70 + (int)((r >> 24) % 30);
            n.on = true;
            lv_obj_set_style_image_recolor(note_img_[i], lv_color_hex(kNoteColors[(r >> 4) % 6]), 0);
            lv_obj_set_style_opa(note_img_[i], LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(note_img_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(note_img_[i]);
            break;
        }
    }
    for (int i = 0; i < kNotes; ++i) {
        Note& n = notes_[i];
        if (!n.on) continue;
        ++n.age;
        n.x += n.vx + 0.7f * std::sin(n.age * 0.16f + i);
        n.y += n.vy;
        if (n.age >= n.life || n.y < 6.0f) {
            n.on = false;
            lv_obj_add_flag(note_img_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int opa = 255;
        if (n.age < 8) opa = n.age * 32;
        int left = n.life - n.age;
        if (left < 16) opa = left * 16;
        lv_obj_set_style_opa(note_img_[i], (lv_opa_t)(opa > 255 ? 255 : opa), 0);
        lv_obj_set_pos(note_img_[i], a.x1 + (int)n.x, a.y1 + (int)n.y);
    }
    lvgl_port_unlock();
}

// ── v3.5 anger mark on the GIF face ────────────────────────────────────────
void StackChanGeometryDisplay::AngerTimerCb(void* arg) {
    auto self = static_cast<StackChanGeometryDisplay*>(arg);
    if (!lvgl_port_lock(5)) {             // busy: try again shortly
        esp_timer_start_once(self->anger_timer_, 50 * 1000);
        return;
    }
    if (self->anger_img_ != nullptr) lv_obj_add_flag(self->anger_img_, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

void StackChanGeometryDisplay::ShowAngerMark() {
    if (anger_img_ == nullptr || geometry_visible_) return;
    if (anger_timer_ == nullptr) {
        esp_timer_create_args_t a = {};
        a.callback = &StackChanGeometryDisplay::AngerTimerCb;
        a.arg = this;
        a.dispatch_method = ESP_TIMER_TASK;
        a.name = "hg_anger";
        if (esp_timer_create(&a, &anger_timer_) != ESP_OK) { anger_timer_ = nullptr; return; }
    }
    {
        DisplayLockGuard lock(this);
        lv_obj_align_to(anger_img_, emoji_image_, LV_ALIGN_TOP_LEFT, 226, 30);
        lv_obj_remove_flag(anger_img_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(anger_img_);
    }
    esp_timer_stop(anger_timer_);
    esp_timer_start_once(anger_timer_, 2500ULL * 1000);
}


// ── v3.6.7-dj2: DJ screen = her face + a 13-band equalizer ────────────────
// Everything else from v3.6.7-dj (deck, records, tone arm, hand, title, the
// top VU bars, buttons) is gone. Her GIF face (and scowl, mouth, anger mark)
// stays full size; 13 vertical columns sit along the bottom (y 186..236,
// hg_eq layout), each over a faint dark track, with a peak-hold cap.
// Levels come from the PCM going to the speaker (hg_eq_task.cc, a 1024-point
// FFT per 20 ms window on its own low-priority task); this 25 fps timer only
// smooths and moves 26 objects, created once and reused.
// All objects are non-clickable; the board handles taps and swipes.
namespace {
lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    return o;
}
lv_obj_t* Line(lv_obj_t* parent, lv_point_precise_t* pts, int width, uint32_t color) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}
}  // namespace

// v3.6.7-dj3: each bar is a solid color of its own (no LVGL gradient: one
// color per column reads clearly on the small panel and costs less to draw);
// the cap is the bar color brightened toward white.
void StackChanGeometryDisplay::ColorDjColumn(int k, uint32_t c, uint32_t cap) {
    lv_obj_set_style_bg_color(dj_bar_[k], lv_color_hex(c), 0);
    lv_obj_set_style_bg_color(dj_cap_[k], lv_color_hex(cap), 0);
}

// v3.6.7-dj4: DJ screen refresh timing (LVGL task). Logged every 10 s from
// DjTick while the DJ screen shows: how long one LVGL refresh (render + SPI
// flush of all dirty areas) takes, average and worst. The worst is about one
// full-screen redraw (her GIF frame, or a rave color step).
namespace {
std::atomic<bool> s_refr_track{false};
int64_t s_refr_start_us = 0;
bool s_refr_rendered = false;
std::atomic<uint32_t> s_refr_n{0};
std::atomic<uint32_t> s_refr_sum_us{0};
std::atomic<uint32_t> s_refr_max_us{0};
}  // namespace

void StackChanGeometryDisplay::RefrEventCb(lv_event_t* e) {
    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START:
            s_refr_start_us = esp_timer_get_time();
            s_refr_rendered = false;
            break;
        case LV_EVENT_RENDER_START: s_refr_rendered = true; break;
        case LV_EVENT_REFR_READY:
            if (s_refr_rendered && s_refr_track.load()) {
                uint32_t d = (uint32_t)(esp_timer_get_time() - s_refr_start_us);
                s_refr_n.fetch_add(1);
                s_refr_sum_us.fetch_add(d);
                if (d > s_refr_max_us.load()) s_refr_max_us.store(d);
            }
            break;
        default: break;
    }
}

void StackChanGeometryDisplay::CreateDjOverlay() {
    if (dj_root_ != nullptr || emoji_image_ == nullptr) return;
    lv_obj_t* scr = lv_obj_get_screen(emoji_image_);
    if (scr == nullptr) scr = lv_screen_active();
    dj_root_ = Plain(scr);
    lv_obj_set_pos(dj_root_, 0, 0);
    lv_obj_set_size(dj_root_, 320, 240);
    // v3.6.7-dj4 rave tint: one full-screen rectangle MULTIPLIED over everything
    // under the DJ screen (her GIF face: white -> the tint, black ink stays
    // black). Below the equalizer, so the bars keep their own colors. Hidden
    // (no cost) unless rave mode is on or still fading out.
    dj_tint_ = Plain(dj_root_);
    lv_obj_set_pos(dj_tint_, 0, 0);
    lv_obj_set_size(dj_tint_, 320, 240);
    lv_obj_set_style_bg_color(dj_tint_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dj_tint_, LV_OPA_COVER, 0);
    lv_obj_set_style_blend_mode(dj_tint_, LV_BLEND_MODE_MULTIPLY, 0);
    lv_obj_add_flag(dj_tint_, LV_OBJ_FLAG_HIDDEN);
    rave_tint_visible_.store(false);
    dj_eq_root_ = Plain(dj_root_);
    lv_obj_set_pos(dj_eq_root_, 0, 0);
    lv_obj_set_size(dj_eq_root_, 320, 240);
    for (int k = 0; k < hg_eq::kBands; ++k) {
        const int x = hg_eq::ColX(k);
        lv_obj_t* t = Plain(dj_eq_root_);          // track
        lv_obj_set_pos(t, x, hg_eq::kBottom - hg_eq::kMaxH);
        lv_obj_set_size(t, hg_eq::kColW, hg_eq::kMaxH);
        lv_obj_set_style_radius(t, 3, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(0x0B0B14), 0);
        lv_obj_set_style_bg_opa(t, 70, 0);
        dj_track_[k] = t;
        lv_obj_t* b = Plain(dj_eq_root_);          // bar
        lv_obj_set_pos(b, x, hg_eq::kBottom);
        lv_obj_set_size(b, hg_eq::kColW, 0);
        lv_obj_set_style_radius(b, 3, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* c = Plain(dj_eq_root_);          // peak cap
        lv_obj_set_pos(c, x, hg_eq::CapY(0.0f));
        lv_obj_set_size(c, hg_eq::kColW, hg_eq::kCapH);
        lv_obj_set_style_radius(c, 1, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        dj_bar_[k] = b;
        dj_cap_[k] = c;
        dj_bar_h_[k] = 0;
        dj_cap_y_[k] = -1;
        dj_col_drawn_[k] = 0xFFFFFFFFu;
    }
    dj_vu_palette_drawn_ = 0;
    dj_vu_on_drawn_ = true;
    lv_obj_add_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);
    lv_display_t* disp = lv_obj_get_display(dj_root_);
    if (disp != nullptr) {
        lv_display_add_event_cb(disp, &StackChanGeometryDisplay::RefrEventCb, LV_EVENT_REFR_START, nullptr);
        lv_display_add_event_cb(disp, &StackChanGeometryDisplay::RefrEventCb, LV_EVENT_RENDER_START, nullptr);
        lv_display_add_event_cb(disp, &StackChanGeometryDisplay::RefrEventCb, LV_EVENT_REFR_READY, nullptr);
    }
    ESP_LOGI(TAG, "DJ overlay ready (13-band equalizer, rave tint)");
}

// v3.6.7-dj4: rave mode on / off (voice: self.dj.mode rave_on / rave_off;
// swipe up / down on the DJ screen). Not saved: every boot starts with it off.
void StackChanGeometryDisplay::SetRave(bool on) {
    if (rave_want_.exchange(on) == on) return;
    ESP_LOGI(TAG, "rave mode %s%s", on ? "on" : "off", dj_on_.load() ? "" : " (shows with the DJ screen)");
}

// Under the display lock: show / recolor / hide the tint after a rave step.
// Setting the color invalidates the whole screen, so this runs at most once
// per hg_rave::kPeriodMs, and only when the RGB565 color really changes.
void StackChanGeometryDisplay::ApplyRaveTint() {
    if (dj_tint_ == nullptr) return;
    if (!rave_.Idle()) {
        const uint32_t rgb = rave_.Rgb();
        const uint16_t c565 = hg_rave::To565(rgb);
        if (!rave_tint_visible_.load()) {
            lv_obj_set_style_bg_color(dj_tint_, lv_color_hex(rgb), 0);
            lv_obj_clear_flag(dj_tint_, LV_OBJ_FLAG_HIDDEN);
            for (auto* t : dj_track_) if (t != nullptr) lv_obj_set_style_bg_opa(t, 150, 0);   // darker strip under the bars
            rave_tint_visible_.store(true);
            rave_drawn_565_ = c565;
        } else if (c565 != rave_drawn_565_) {
            lv_obj_set_style_bg_color(dj_tint_, lv_color_hex(rgb), 0);
            rave_drawn_565_ = c565;
        }
    } else if (rave_tint_visible_.load()) {
        lv_obj_add_flag(dj_tint_, LV_OBJ_FLAG_HIDDEN);
        for (auto* t : dj_track_) if (t != nullptr) lv_obj_set_style_bg_opa(t, 70, 0);
        rave_tint_visible_.store(false);
        rave_drawn_565_ = 0xFFFF;
    }
}

// self.dj.vu / set list "vu" cue: color, on / off, pulse. Only a hint: the
// bars run on their own from the audio without it.
bool StackChanGeometryDisplay::SetDjVu(const std::string& mode, int pulse_ms) {
    hg_vu::Mode m = hg_vu::ParseMode(mode);
    switch (m.kind) {
        case hg_vu::ModeKind::kBad: return false;
        case hg_vu::ModeKind::kColor:
            dj_vu_palette_.store((int)m.palette);
            dj_vu_hint_seq_.fetch_add(1);      // v3.6.7-dj3: every color call re-aims the palette
            dj_vu_on_.store(true);
            break;
        case hg_vu::ModeKind::kOn: dj_vu_on_.store(true); break;
        case hg_vu::ModeKind::kOff: dj_vu_on_.store(false); break;
        case hg_vu::ModeKind::kPulse: {
            int ms = pulse_ms <= 0 ? 2000 : std::min(pulse_ms, 30000);
            dj_vu_on_.store(true);
            dj_vu_pulse_until_.store(esp_timer_get_time() / 1000 + ms);
            break;
        }
    }
    ESP_LOGI(TAG, "DJ EQ: %s", mode.c_str());
    return true;
}

void StackChanGeometryDisplay::SetDjMode(bool on) {
    if (dj_on_.exchange(on) == on) return;
    ESP_LOGI(TAG, "DJ screen %s", on ? "on" : "off");
    if (dj_timer_ == nullptr) {
        esp_timer_create_args_t a = {};
        a.callback = &StackChanGeometryDisplay::DjTimerCb;
        a.arg = this;
        a.dispatch_method = ESP_TIMER_TASK;
        a.name = "hg_dj";
        a.skip_unhandled_events = true;
        if (esp_timer_create(&a, &dj_timer_) != ESP_OK) dj_timer_ = nullptr;
    }
    if (on) {
        StopNotes();
        if (dj_timer_ != nullptr) esp_timer_stop(dj_timer_);
        dj_closing_.store(false);
        {
            DisplayLockGuard lock(this);
            CreateDjOverlay();
            if (dj_root_ != nullptr && !geometry_visible_) {
                lv_obj_clear_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(dj_root_);
            }
            if (dj_eq_root_ != nullptr && dj_vu_on_drawn_) lv_obj_clear_flag(dj_eq_root_, LV_OBJ_FLAG_HIDDEN);
        }
        dj_bars_.Clear();
        std::memset(dj_target_, 0, sizeof(dj_target_));
        dj_color_.last_ms = -1;                // keeps its phase, restarts its clock
        dj_color_.flash = 0.0f;
        dj_beat_us_ = esp_timer_get_time();
        dj_stats_ms_ = esp_timer_get_time() / 1000;
        s_refr_track.store(true);
        hg_eq_task::SetActive(true);          // also starts the DJ light sync
        if (dj_timer_ != nullptr) esp_timer_start_periodic(dj_timer_, 40 * 1000);
    } else {
        hg_eq_task::SetActive(false);
        s_refr_track.store(false);
        // v3.6.7-dj4: a rave tint never cuts to white: the timer keeps running
        // (bars hidden) until the tint has faded out, then DjTick closes.
        if (dj_timer_ != nullptr && rave_tint_visible_.load() && !geometry_visible_) {
            dj_closing_.store(true);
            DisplayLockGuard lock(this);
            if (dj_eq_root_ != nullptr) lv_obj_add_flag(dj_eq_root_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (dj_timer_ != nullptr) esp_timer_stop(dj_timer_);
        {
            DisplayLockGuard lock(this);
            if (dj_root_ != nullptr) lv_obj_add_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);
        }
        if (party_.load() && !geometry_visible_) StartNotes();
    }
}

void StackChanGeometryDisplay::DjTimerCb(void* arg) {
    static_cast<StackChanGeometryDisplay*>(arg)->DjTick();
}

void StackChanGeometryDisplay::DjTick() {
    if (dj_root_ == nullptr || geometry_visible_) return;
    const bool on = dj_on_.load();
    if (!on && !dj_closing_.load()) return;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (rave_reset_.exchange(false)) { rave_.Reset(); rave_last_ms_ = -1000000; }
    bool beat = false;
    bool vu_on = dj_vu_on_drawn_;
    if (on) {
        switch (hg_eq_task::Sample(dj_target_)) {
            case hg_eq_task::kLive: dj_silent_ = false; break;
            case hg_eq_task::kPending: break;                 // keep the last frame
            case hg_eq_task::kSilent: dj_silent_ = true; break;
        }
        float pulse[hg_eq::kBands];
        const bool pulsing = hg_eq::PulseWave(now_ms, dj_vu_pulse_until_.load(), pulse);
        dj_bars_.Step(dj_silent_ ? nullptr : dj_target_, pulsing ? pulse : nullptr);
        const int palette = dj_vu_palette_.load();
        vu_on = dj_vu_on_.load();
        const uint32_t hint_seq = dj_vu_hint_seq_.load();
        if (palette != dj_vu_palette_drawn_ || hint_seq != dj_vu_hint_drawn_) {   // self.dj.vu color hint
            dj_vu_palette_drawn_ = palette;
            dj_vu_hint_drawn_ = hint_seq;
            dj_color_.Hint(palette);
        }
        // v3.6.7-dj3: palette turns, flashes on bass beats (<= 4 per second)
        beat = hg_eq_task::BeatSince(&dj_beat_us_) && !dj_silent_;
        dj_color_.Step(now_ms, beat);
        float energy = 0.0f;
        for (int k = 0; k < hg_eq::kBands; ++k) energy += dj_bars_.level[k];
        dj_energy_ = energy / hg_eq::kBands;
        uint32_t leds[hg_eq::kLedCount];
        dj_color_.Leds(dj_energy_, leds);
        hg_eq_task::PublishLeds(leds);        // the hg_djled task writes them (<= 20 Hz)
        if (now_ms - dj_stats_ms_ >= 10000) {  // v3.6.7-dj4: refresh cost, every 10 s
            dj_stats_ms_ = now_ms;
            uint32_t n = s_refr_n.exchange(0), sum = s_refr_sum_us.exchange(0), mx = s_refr_max_us.exchange(0);
            ESP_LOGI(TAG, "DJ refresh: %lu frames/10 s, avg %lu us, max %lu us, rave %s",
                     (unsigned long)n, (unsigned long)(n ? sum / n : 0), (unsigned long)mx,
                     rave_tint_visible_.load() ? "on" : "off");
        }
    }
    // v3.6.7-dj4 rave background: one step per hg_rave::kPeriodMs (every 3rd
    // 40 ms tick); beats in between are kept for it.
    rave_beat_pending_ = rave_beat_pending_ || beat;
    bool rave_step = false;
    if (now_ms - rave_last_ms_ >= hg_rave::kPeriodMs - 10) {
        rave_last_ms_ = now_ms;
        const bool want = on && rave_want_.load();
        if (want || !rave_.Idle()) {
            rave_.Step(now_ms, want, dj_color_.phase, dj_silent_ ? 0.0f : dj_energy_, rave_beat_pending_);
            rave_step = true;
        } else {
            rave_.last_ms = -1;                // idle: restart its clock next time
        }
        rave_beat_pending_ = false;
    }

    if (!lvgl_port_lock(2)) return;        // never stall the esp_timer task
    if (rave_step || (!rave_.Idle() && !rave_tint_visible_.load())) ApplyRaveTint();
    if (!on) {                             // closing: the tint has faded out -> done
        if (rave_.Idle()) {
            ApplyRaveTint();
            lv_obj_add_flag(dj_root_, LV_OBJ_FLAG_HIDDEN);
            dj_closing_.store(false);
            if (dj_timer_ != nullptr) esp_timer_stop(dj_timer_);
            lvgl_port_unlock();
            if (party_.load() && !dj_on_.load()) StartNotes();
            return;
        }
        lvgl_port_unlock();
        return;
    }
    if (vu_on != dj_vu_on_drawn_ && dj_eq_root_ != nullptr) {
        dj_vu_on_drawn_ = vu_on;
        if (vu_on) lv_obj_clear_flag(dj_eq_root_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(dj_eq_root_, LV_OBJ_FLAG_HIDDEN);
    }
    for (int k = 0; k < hg_eq::kBands && vu_on; ++k) {
        const uint32_t c = dj_color_.Column(k);
        // compare in RGB565 (what the panel shows): skip invisible changes
        const uint32_t c565 = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
        if (c565 != dj_col_drawn_[k]) {
            dj_col_drawn_[k] = c565;
            ColorDjColumn(k, c, dj_color_.Cap(k));
        }
    }
    for (int k = 0; k < hg_eq::kBands && vu_on; ++k) {
        const int h = hg_eq::BarH(dj_bars_.level[k]);
        if (h != dj_bar_h_[k]) {
            if (h == 0) lv_obj_add_flag(dj_bar_[k], LV_OBJ_FLAG_HIDDEN);
            else {
                if (dj_bar_h_[k] == 0) lv_obj_clear_flag(dj_bar_[k], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_y(dj_bar_[k], hg_eq::kBottom - h);
                lv_obj_set_height(dj_bar_[k], h);
            }
            dj_bar_h_[k] = h;
        }
        const bool cap_shown = hg_eq::BarH(dj_bars_.peak[k]) > 0;
        const int cy = cap_shown ? hg_eq::CapY(dj_bars_.peak[k]) : -1;
        if (cy != dj_cap_y_[k]) {
            if (!cap_shown) lv_obj_add_flag(dj_cap_[k], LV_OBJ_FLAG_HIDDEN);
            else {
                if (dj_cap_y_[k] < 0) lv_obj_clear_flag(dj_cap_[k], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_y(dj_cap_[k], cy);
            }
            dj_cap_y_[k] = cy;
        }
    }
    lvgl_port_unlock();
}

// ── v3.6.7-dj: scowl (fierce angry) on the GIF face ────────────────────────
// Two thick black lids over the top-inner part of each eye of the neutral GIF
// (her near eye x~113..142, far eye x~80..95, lids at y~97..107), slanted down
// toward the nose, plus the red anger mark held on. Positions read off
// fairy-assets/neutral.gif; not yet checked on the robot.
void StackChanGeometryDisplay::SetScowl(bool on) {
    if (on == scowl_on_) return;
    if (emoji_image_ == nullptr) return;
    scowl_on_ = on;
    {
        DisplayLockGuard lock(this);
        if (on && scowl_line_[0] == nullptr) {
            lv_obj_t* scr = lv_obj_get_screen(emoji_image_);
            if (scr == nullptr) scr = lv_screen_active();
            scowl_pts_[0][0] = {110, 106};   // near eye: inner (nose side) low
            scowl_pts_[0][1] = {147, 96};    //           outer high
            scowl_pts_[1][0] = {78, 99};     // far eye:  outer high
            scowl_pts_[1][1] = {97, 107};    //           inner low
            for (int i = 0; i < 2; ++i) {
                scowl_line_[i] = Line(scr, scowl_pts_[i], 6, 0x000000);
                lv_obj_set_pos(scowl_line_[i], 0, 0);
            }
        }
        for (auto* l : scowl_line_) {
            if (l == nullptr) continue;
            if (on && !geometry_visible_) {
                lv_area_t a;
                lv_obj_get_coords(emoji_image_, &a);   // GIF top-left on screen
                lv_obj_set_pos(l, a.x1, a.y1);
                lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(l);
            } else {
                lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (on) {
        ShowAngerMark();
        if (anger_timer_ != nullptr) esp_timer_stop(anger_timer_);   // hold it on
    } else if (anger_img_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_obj_add_flag(anger_img_, LV_OBJ_FLAG_HIDDEN);
    }
}
