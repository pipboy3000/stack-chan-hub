#include "ui_view.h"

#include <cstdio>
#include <cstring>

#include <M5Unified.h>

namespace ui {
namespace {

constexpr uint16_t Rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) |
                               (b >> 3));
}

constexpr uint16_t kBackground = Rgb(16, 20, 28);
constexpr uint16_t kPanel = Rgb(36, 44, 58);
constexpr uint16_t kLine = Rgb(58, 68, 84);
constexpr uint16_t kText = Rgb(240, 244, 248);
constexpr uint16_t kMuted = Rgb(150, 162, 178);
constexpr uint16_t kGreen = Rgb(70, 200, 120);
constexpr uint16_t kAmber = Rgb(240, 180, 60);
constexpr uint16_t kRed = Rgb(240, 96, 96);
constexpr uint16_t kAccent = Rgb(70, 140, 240);

M5Canvas canvas(&M5.Display);
const char* const* names = nullptr;

const lgfx::IFont* const kSmall = &fonts::efontJA_16;
const lgfx::IFont* const kMedium = &fonts::efontJA_24;
const lgfx::IFont* const kLarge = &fonts::FreeSansBold18pt7b;

void text(const char* string, int x, int y, const lgfx::IFont* font,
          uint16_t color, textdatum_t datum = textdatum_t::middle_left) {
  canvas.setFont(font);
  canvas.setTextColor(color);
  canvas.setTextDatum(datum);
  canvas.drawString(string, x, y);
}

void button(const Rect& rect, const char* label, uint16_t fill,
            uint16_t color, const lgfx::IFont* font = kMedium) {
  canvas.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, fill);
  text(label, rect.x + rect.w / 2, rect.y + rect.h / 2, font, color,
       textdatum_t::middle_center);
}

void header(const char* title, bool back = true) {
  canvas.fillRect(0, 0, kWidth, kHeaderHeight, kPanel);
  if (back) {
    text("< 戻る", 10, kHeaderHeight / 2, kSmall, kAccent);
  }
  text(title, kWidth / 2, kHeaderHeight / 2, kSmall, kText,
       textdatum_t::middle_center);
}

void row(int index, const char* label, const char* value,
         uint16_t value_color = kMuted) {
  const Rect rect = ListRow(index);
  text(label, 12, rect.y + rect.h / 2, kSmall, kText);
  text(value, kWidth - 12, rect.y + rect.h / 2, kSmall, value_color,
       textdatum_t::middle_right);
  canvas.drawFastHLine(0, rect.y + rect.h - 1, kWidth, kLine);
}

uint16_t link_color(Link link) {
  return link == Link::kConnected
             ? kGreen
             : (link == Link::kConnecting ? kAmber : kRed);
}

const char* power_name(PowerSetting power) {
  switch (power) {
    case PowerSetting::kInternal:
      return "本体から";
    case PowerSetting::kExternal:
      return "外部給電";
    default:
      return "自動";
  }
}

void draw_boot(const Model& model, uint32_t now_ms) {
  char line[64];
  text("StackChan Hub", kWidth / 2, 56, kLarge, kText,
       textdatum_t::middle_center);
  snprintf(line, sizeof(line), "%d秒後に開始します",
           model.BootSecondsLeft(now_ms));
  text(line, kWidth / 2, 116, kMedium, kText, textdatum_t::middle_center);
  snprintf(line, sizeof(line), "キーボード給電: %s",
           power_name(model.power()));
  text(line, kWidth / 2, 152, kSmall, kMuted, textdatum_t::middle_center);
  button({40, 184, kWidth - 80, 40}, "タップで手動選択", kPanel, kAccent,
         kSmall);
}

void draw_boot_manual() {
  header("起動方法を選ぶ", false);
  text("キーボードへの給電", 24, 54, kSmall, kMuted);
  button(ChoiceButton(0), "本体から（電池で使う）", kPanel, kText, kSmall);
  button(ChoiceButton(1), "外部給電（台座のUSB）", kPanel, kText, kSmall);
  button(ChoiceButton(2), "開始しない（USB書き込み用）", kPanel, kMuted,
         kSmall);
}

void draw_home(const Model& model, const Status& status) {
  canvas.fillRect(0, 0, kWidth, 40, kPanel);
  text("Voyager", 8, 20, kSmall, kMuted);
  const char* keyboard = "停止";
  uint16_t keyboard_color = kMuted;
  if (status.keyboard == Keyboard::kReady) {
    keyboard = "接続";
    keyboard_color = kGreen;
  } else if (status.keyboard == Keyboard::kWaiting) {
    keyboard = "未接続";
    keyboard_color = kRed;
  }
  text(keyboard, 72, 20, kSmall, keyboard_color);

  char power[32];
  const char* source = status.charging
                           ? "充電中"
                           : (status.external_power ? "外部給電" : "電池");
  if (status.battery_percent < 0) {
    snprintf(power, sizeof(power), "%s", source);
  } else {
    snprintf(power, sizeof(power), "%s %d%%", source, status.battery_percent);
  }
  text(power, kSettingsButton.x - 6, 20, kSmall,
       status.charging ? kGreen : kText, textdatum_t::middle_right);
  button({static_cast<int16_t>(kSettingsButton.x + 4), 5, 56, 30}, "設定",
         kBackground, kAccent, kSmall);

  const uint8_t active = status.active_slot;
  const bool registered = status.registered[active];
  text(names[active], kWidth / 2, 74, kLarge, kText,
       textdatum_t::middle_center);
  const char* state = "未接続";
  if (!registered) {
    state = "未登録";
  } else if (status.link == Link::kConnected) {
    state = "接続中";
  } else if (status.link == Link::kConnecting) {
    state = "接続待ち";
  }
  const uint16_t state_color = registered ? link_color(status.link) : kMuted;
  text(state, kWidth / 2, 108, kMedium, state_color,
       textdatum_t::middle_center);
  const char* notice = status.notice;
  if (notice[0] == '\0' && !registered) {
    notice = "設定 > 接続先の管理 でペアリング";
  }
  text(notice, kWidth / 2, 136, kSmall, kAmber, textdatum_t::middle_center);

  const int count = model.home_slots();
  for (int i = 0; i < count; ++i) {
    Rect rect = SlotButton(i, count);
    rect.x += 4;
    rect.y += 4;
    rect.w -= 8;
    rect.h -= 8;
    const bool selected = i == active;
    button(rect, names[i], selected ? state_color : kPanel,
           selected ? kBackground : kText, count > 2 ? kSmall : kMedium);
  }
}

void draw_settings(const Model& model, uint32_t now_ms) {
  static const char* const kBrightness[kBrightnessLevels] = {"暗", "中",
                                                             "明"};
  header("設定");
  row(kRowHosts, "接続先の管理", ">");
  row(kRowPower, "キーボード給電", power_name(model.power()));
  row(kRowBrightness, "画面の明るさ", kBrightness[model.brightness()]);
  row(kRowDiagnostics, "診断情報", ">");
  if (model.RestartArmed(now_ms)) {
    row(kRowRestart, "再起動", "もう一度タップで再起動", kRed);
  } else {
    row(kRowRestart, "再起動", "");
  }
  row(kRowSleep, "休止",
      model.SleepArmed(now_ms) ? "もう一度タップで休止" : "画面タップで復帰",
      model.SleepArmed(now_ms) ? kRed : kMuted);
}

void draw_power(const Model& model) {
  static const char* const kLabels[3] = {"自動（外部5Vがあれば外部）",
                                         "本体から（電池で使う）",
                                         "外部給電（台座のUSB）"};
  header("キーボード給電");
  text("切替時にUSBが一時中断します", kWidth / 2, 54, kSmall, kMuted,
       textdatum_t::middle_center);
  for (int i = 0; i < 3; ++i) {
    const bool selected = static_cast<int>(model.power()) == i;
    button(ChoiceButton(i), kLabels[i], selected ? kAccent : kPanel, kText,
           kSmall);
  }
}

void draw_diagnostics(const char* diagnostics) {
  header("診断情報");
  canvas.setFont(kSmall);
  canvas.setTextColor(kText);
  canvas.setTextDatum(textdatum_t::top_left);
  char line[48];
  int y = kHeaderHeight + 4;
  for (const char* cursor = diagnostics; *cursor != '\0' && y < kHeight;) {
    const char* end = strchr(cursor, '\n');
    const size_t length = end != nullptr ? static_cast<size_t>(end - cursor)
                                         : strlen(cursor);
    snprintf(line, sizeof(line), "%.*s", static_cast<int>(length), cursor);
    canvas.drawString(line, 8, y);
    y += 18;
    cursor += length + (end != nullptr ? 1 : 0);
  }
}

void draw_hosts(const Status& status) {
  header("接続先の管理");
  char label[32];
  for (int i = 0; i < kSlotCount; ++i) {
    snprintf(label, sizeof(label), "%d  %s", i + 1, names[i]);
    row(i, label, status.registered[i] ? "登録済み >" : "未登録 >",
        status.registered[i] ? kText : kMuted);
  }
  text("PCを入れ替えるときは登録を解除し、", 12, 160, kSmall, kMuted);
  text("あらためてペアリングします。", 12, 180, kSmall, kMuted);
}

void draw_host(const Model& model, const Status& status) {
  char line[64];
  header(names[model.host()]);
  const uint8_t* a = status.host_address;
  if (model.host_registered()) {
    snprintf(line, sizeof(line), "登録済み  %02X:%02X:%02X:%02X:%02X:%02X",
             a[5], a[4], a[3], a[2], a[1], a[0]);
  } else {
    snprintf(line, sizeof(line), "未登録");
  }
  text(line, kWidth / 2, 52, kSmall, kMuted, textdatum_t::middle_center);
  const Rect info = ChoiceButton(0);
  const int middle = info.y + info.h / 2;
  if (model.host_pairing()) {
    text("PCのBluetooth設定で", kWidth / 2, middle - 10, kSmall, kText,
         textdatum_t::middle_center);
    text("StackChan Hub を選んでください", kWidth / 2, middle + 10, kSmall,
         kText, textdatum_t::middle_center);
    snprintf(line, sizeof(line), "中止（残り%u秒）", status.pairing_seconds);
    button(ChoiceButton(1), line, kPanel, kAmber, kSmall);
    return;
  }
  text(model.host_busy() ? "処理中…" : status.host_message, kWidth / 2,
       middle, kSmall, kAmber, textdatum_t::middle_center);
  if (model.host_busy()) {
    return;
  }
  button(ChoiceButton(1),
         model.host_registered() ? "ペアリングし直す" : "ペアリングを開始",
         model.host_registered() ? kPanel : kAccent, kText, kSmall);
  if (model.host_registered()) {
    button(ChoiceButton(2), "登録を解除する", kPanel, kRed, kSmall);
  }
}

void draw_confirm_unbind(const Model& model) {
  char line[64];
  header("登録の解除", false);
  snprintf(line, sizeof(line), "%s の登録を解除しますか？",
           names[model.host()]);
  text(line, kWidth / 2, 70, kSmall, kText, textdatum_t::middle_center);
  text("PC側のBluetooth登録も削除してください", kWidth / 2, 96, kSmall,
       kMuted, textdatum_t::middle_center);
  button(ChoiceButton(1), "解除する", kRed, kBackground, kSmall);
  button(ChoiceButton(2), "やめる", kPanel, kText, kSmall);
}

}  // namespace

bool Begin(const char* const* slot_names) {
  names = slot_names;
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  return canvas.createSprite(kWidth, kHeight) != nullptr;
}

void Draw(const Model& model, const Status& status, const char* diagnostics,
          uint32_t now_ms) {
  canvas.fillSprite(kBackground);
  switch (model.screen()) {
    case Screen::kBoot:
      draw_boot(model, now_ms);
      break;
    case Screen::kBootManual:
      draw_boot_manual();
      break;
    case Screen::kHome:
      draw_home(model, status);
      break;
    case Screen::kSettings:
      draw_settings(model, now_ms);
      break;
    case Screen::kPower:
      draw_power(model);
      break;
    case Screen::kDiagnostics:
      draw_diagnostics(diagnostics);
      break;
    case Screen::kHosts:
      draw_hosts(status);
      break;
    case Screen::kHost:
      draw_host(model, status);
      break;
    case Screen::kConfirmUnbind:
      draw_confirm_unbind(model);
      break;
  }
  canvas.pushSprite(0, 0);
}

const uint8_t* Pixels(size_t* size) {
  *size = canvas.bufferLength();
  return static_cast<const uint8_t*>(canvas.getBuffer());
}

}  // namespace ui
