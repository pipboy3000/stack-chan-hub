#include "hid_descriptor.h"

namespace hid_descriptor {
namespace {

constexpr uint16_t kPageGenericDesktop = 0x01;
constexpr uint16_t kPageButton = 0x09;
constexpr uint16_t kPageConsumer = 0x0C;
constexpr uint16_t kPageDigitizer = 0x0D;
constexpr uint16_t kUsageMouse = 0x02;
constexpr uint16_t kUsageKeyboard = 0x06;
constexpr uint16_t kUsageSystemControl = 0x80;
constexpr uint16_t kUsageConsumerControl = 0x01;
constexpr uint16_t kUsageX = 0x30;
constexpr uint16_t kUsageY = 0x31;
constexpr uint16_t kUsageWheel = 0x38;
constexpr uint16_t kUsageAcPan = 0x0238;
constexpr uint16_t kUsageTipSwitch = 0x42;
constexpr uint16_t kUsageConfidence = 0x47;
constexpr uint16_t kUsageContactId = 0x51;
constexpr uint16_t kUsageContactCount = 0x54;
constexpr uint16_t kUsageScanTime = 0x56;
constexpr uint16_t kUsageInputMode = 0x52;
constexpr uint16_t kUsageSurfaceSwitch = 0x57;

constexpr size_t kMaxUsages = 16;
constexpr size_t kMaxPush = 4;

struct GlobalState {
  uint16_t usage_page = 0;
  int32_t logical_minimum = 0;
  int32_t logical_maximum = 0;
  uint8_t report_size = 0;
  uint8_t report_id = 0;
  uint16_t report_count = 0;
};

// One local usage: page 0 means "use the current Usage Page".
struct Usage {
  uint16_t page;
  uint16_t id;
};

struct LocalState {
  Usage usages[kMaxUsages];
  size_t usage_count = 0;
  bool has_range = false;
  Usage range_min{};
  uint16_t range_max = 0;

  // Usage assigned to field `index` of the current main item.
  Usage For(size_t index) const {
    if (has_range) {
      const uint32_t id = range_min.id + index;
      return {range_min.page,
              static_cast<uint16_t>(id > range_max ? range_max : id)};
    }
    if (usage_count == 0) {
      return {0, 0};
    }
    return usages[index < usage_count ? index : usage_count - 1];
  }
};

ReportKind classify(uint16_t page, uint16_t usage) {
  if (page >= 0xFF00) {
    return ReportKind::kVendor;
  }
  if (page == kPageDigitizer) {
    return ReportKind::kDigitizer;
  }
  if (page == kPageConsumer && usage == kUsageConsumerControl) {
    return ReportKind::kConsumer;
  }
  if (page == kPageGenericDesktop) {
    switch (usage) {
      case kUsageKeyboard:
        return ReportKind::kKeyboard;
      case kUsageMouse:
        return ReportKind::kMouse;
      case kUsageSystemControl:
        return ReportKind::kSystem;
      default:
        break;
    }
  }
  return ReportKind::kUnknown;
}

uint32_t read_unsigned(const uint8_t* data, size_t size) {
  uint32_t value = 0;
  for (size_t i = 0; i < size; ++i) {
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  }
  return value;
}

int32_t read_signed(const uint8_t* data, size_t size) {
  const uint32_t value = read_unsigned(data, size);
  switch (size) {
    case 1:
      return static_cast<int8_t>(value);
    case 2:
      return static_cast<int16_t>(value);
    default:
      return static_cast<int32_t>(value);
  }
}

class Parser {
 public:
  explicit Parser(Interface& out) : out_(out) {}

  bool Run(const uint8_t* data, size_t length) {
    size_t pos = 0;
    while (pos < length) {
      const uint8_t prefix = data[pos++];
      if (prefix == 0xFE) {  // Long item: data size, tag, then data.
        if (pos + 2 > length) {
          return false;
        }
        pos += 2 + data[pos];
        continue;
      }
      const size_t size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
      if (pos + size > length) {
        return false;
      }
      const uint8_t type = (prefix >> 2) & 0x03;
      const uint8_t tag = prefix >> 4;
      const uint8_t* item = &data[pos];
      pos += size;
      bool ok = true;
      switch (type) {
        case 0:
          ok = Main(tag, read_unsigned(item, size));
          break;
        case 1:
          ok = Global(tag, item, size);
          break;
        case 2:
          Local(tag, read_unsigned(item, size), size);
          break;
        default:
          break;
      }
      if (!ok) {
        return false;
      }
    }
    return true;
  }

 private:
  bool Main(uint8_t tag, uint32_t value) {
    switch (tag) {
      case 0x8:  // Input
        if (!Input(value)) {
          return false;
        }
        break;
      case 0xA:  // Collection
        if (depth_ == 0) {
          const Usage usage = local_.For(0);
          app_kind_ = classify(usage.page ? usage.page : global_.usage_page,
                               usage.id);
        }
        ++depth_;
        break;
      case 0xC:  // End Collection
        if (depth_ > 0) {
          --depth_;
        }
        break;
      case 0xB:  // Feature: only the touchpad configuration is of interest.
        for (size_t i = 0; i < local_.usage_count; ++i) {
          const Usage usage = local_.usages[i];
          const uint16_t page = usage.page ? usage.page : global_.usage_page;
          if (page != kPageDigitizer) {
            continue;
          }
          if (usage.id == kUsageInputMode) {
            out_.input_mode_report_id = global_.report_id;
          } else if (usage.id == kUsageSurfaceSwitch) {
            out_.selective_report_id = global_.report_id;
          }
        }
        break;
      default:  // Output reports are not forwarded.
        break;
    }
    local_ = LocalState{};
    return true;
  }

  bool Global(uint8_t tag, const uint8_t* item, size_t size) {
    switch (tag) {
      case 0x0:
        global_.usage_page = read_unsigned(item, size);
        break;
      case 0x1:
        global_.logical_minimum = read_signed(item, size);
        break;
      case 0x2:
        // Unsigned when the minimum is not negative (e.g. 0..0xFFFF).
        global_.logical_maximum = global_.logical_minimum < 0
                                      ? read_signed(item, size)
                                      : static_cast<int32_t>(
                                            read_unsigned(item, size));
        break;
      case 0x7:
        global_.report_size = read_unsigned(item, size);
        break;
      case 0x8:
        global_.report_id = read_unsigned(item, size);
        out_.uses_report_ids = true;
        break;
      case 0x9:
        global_.report_count = read_unsigned(item, size);
        break;
      case 0xA:  // Push
        if (stack_size_ == kMaxPush) {
          return false;
        }
        stack_[stack_size_++] = global_;
        break;
      case 0xB:  // Pop
        if (stack_size_ == 0) {
          return false;
        }
        global_ = stack_[--stack_size_];
        break;
      default:
        break;
    }
    return true;
  }

  void Local(uint8_t tag, uint32_t value, size_t size) {
    // A 4-byte usage carries its own page in the upper 16 bits.
    const Usage usage{static_cast<uint16_t>(size == 4 ? value >> 16 : 0),
                      static_cast<uint16_t>(value)};
    switch (tag) {
      case 0x0:
        if (local_.usage_count < kMaxUsages) {
          local_.usages[local_.usage_count++] = usage;
        }
        break;
      case 0x1:
        local_.has_range = true;
        local_.range_min = usage;
        break;
      case 0x2:
        local_.range_max = usage.id;
        break;
      default:
        break;
    }
  }

  InputReport* ReportFor(uint8_t report_id) {
    for (size_t i = 0; i < out_.count; ++i) {
      if (out_.reports[i].report_id == report_id) {
        return &out_.reports[i];
      }
    }
    if (out_.count == Interface::kMaxReports) {
      return nullptr;
    }
    InputReport& report = out_.reports[out_.count++];
    report = InputReport{};
    report.report_id = report_id;
    report.kind = app_kind_;
    return &report;
  }

  bool Input(uint32_t flags) {
    InputReport* report = ReportFor(global_.report_id);
    if (report == nullptr) {
      return false;
    }
    const bool constant = flags & 0x01;
    const bool variable = flags & 0x02;
    const uint32_t start = report->bit_length;
    const uint32_t total =
        start + static_cast<uint32_t>(global_.report_size) * global_.report_count;
    if (total > 0xFFFF) {
      return false;
    }
    report->bit_length = total;
    if (constant || global_.report_size == 0 || global_.report_count == 0) {
      return true;
    }

    Field field;
    field.present = true;
    field.bit_size = global_.report_size;
    field.is_signed = global_.logical_minimum < 0;
    field.logical_max = global_.logical_maximum;

    if (!report->usage.present) {
      field.bit_offset = start;
      report->usage = field;
    }
    if (!variable) {
      return true;
    }

    for (uint16_t i = 0; i < global_.report_count; ++i) {
      const Usage usage = local_.For(i);
      const uint16_t page = usage.page ? usage.page : global_.usage_page;
      field.bit_offset = start + i * global_.report_size;
      if (page == kPageButton && !report->buttons.present) {
        report->buttons = field;
        report->buttons.bit_offset = start;
        report->buttons.bit_size = global_.report_size * global_.report_count;
        report->buttons.is_signed = false;
      } else if (report->kind == ReportKind::kDigitizer &&
                 AssignFinger(*report, page, usage.id, field)) {
        // Stored in the current contact slot.
      } else if (page == kPageDigitizer && usage.id == kUsageContactCount) {
        report->contact_count = field;
      } else if (page == kPageDigitizer && usage.id == kUsageScanTime) {
        report->scan_time = field;
      } else if (page == kPageGenericDesktop && usage.id == kUsageX) {
        report->x = field;
      } else if (page == kPageGenericDesktop && usage.id == kUsageY) {
        report->y = field;
      } else if (page == kPageGenericDesktop && usage.id == kUsageWheel) {
        report->wheel = field;
      } else if (page == kPageConsumer && usage.id == kUsageAcPan) {
        report->pan = field;
      }
    }
    return true;
  }

  // Places a per-contact field into the current slot; a field that the slot
  // already has starts the next slot.
  static bool AssignFinger(InputReport& report, uint16_t page, uint16_t id,
                           const Field& field) {
    Field Finger::*member = nullptr;
    if (page == kPageDigitizer && id == kUsageTipSwitch) {
      member = &Finger::tip;
    } else if (page == kPageDigitizer && id == kUsageConfidence) {
      member = &Finger::confidence;
    } else if (page == kPageDigitizer && id == kUsageContactId) {
      member = &Finger::contact_id;
    } else if (page == kPageGenericDesktop && id == kUsageX) {
      member = &Finger::x;
    } else if (page == kPageGenericDesktop && id == kUsageY) {
      member = &Finger::y;
    } else {
      return false;
    }
    if (report.finger_count == 0 ||
        (report.fingers[report.finger_count - 1].*member).present) {
      if (report.finger_count == InputReport::kMaxFingers) {
        return true;  // Extra slots are ignored.
      }
      ++report.finger_count;
    }
    report.fingers[report.finger_count - 1].*member = field;
    return true;
  }

  Interface& out_;
  GlobalState global_;
  GlobalState stack_[kMaxPush];
  size_t stack_size_ = 0;
  LocalState local_;
  int depth_ = 0;
  ReportKind app_kind_ = ReportKind::kUnknown;
};

}  // namespace

const InputReport* Interface::Find(uint8_t report_id) const {
  for (size_t i = 0; i < count; ++i) {
    if (reports[i].report_id == report_id) {
      return &reports[i];
    }
  }
  return nullptr;
}

bool Parse(const uint8_t* descriptor, size_t length, Interface& out) {
  // Reset in place: an Interface is several KB, too big for a temporary on a
  // small task stack. Report slots are cleared as the parser claims them.
  out.count = 0;
  out.uses_report_ids = false;
  out.input_mode_report_id = 0;
  out.selective_report_id = 0;
  return Parser(out).Run(descriptor, length);
}

int32_t Extract(const uint8_t* payload, size_t length, const Field& field) {
  if (!field.present || field.bit_size == 0 || field.bit_size > 32 ||
      field.bit_offset + field.bit_size > length * 8) {
    return 0;
  }
  uint32_t value = 0;
  for (uint8_t i = 0; i < field.bit_size; ++i) {
    const uint32_t bit = field.bit_offset + i;
    if (payload[bit / 8] & (1u << (bit % 8))) {
      value |= 1u << i;
    }
  }
  if (field.is_signed && field.bit_size < 32 &&
      (value & (1u << (field.bit_size - 1)))) {
    value |= ~0u << field.bit_size;
  }
  return static_cast<int32_t>(value);
}

}  // namespace hid_descriptor
