#include "openco2_epaper.h"

#include <cstring>

#include "esp_attr.h"
#include "esp_rtc_time.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::openco2_epaper {

static const char *const TAG = "openco2_epaper";

// Waveforms from the stock OpenCO2 firmware (Waveshare EPD_1in54_V2 driver):
// 153 LUT bytes, then EOPT (0x3F), gate voltage (0x03), source voltages
// (0x04, three bytes) and VCOM (0x2C).
static const uint8_t LUT_FULL[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x08, 0x01,
    0x00, 0x02, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x22, 0x17, 0x41,
    0x00, 0x32, 0x20,
};
static const uint8_t LUT_PARTIAL[159] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x02, 0x17, 0x41,
    0xB0, 0x32, 0x28,
};

// The image currently on the panel, kept in RTC slow memory so it survives
// deep sleep. rtc_magic is initialised data, so any boot other than a deep
// sleep wake (power switch, flashing, reboot) resets it and forces a full
// refresh - the panel contents are unknown then. The image itself is
// no-init: only valid while the magic says so, and it costs no flash.
static const uint32_t RTC_MAGIC = 0x0C02E1A1;
static RTC_DATA_ATTR uint32_t rtc_magic = 0;
static RTC_DATA_ATTR uint64_t rtc_last_full_us = 0;
static RTC_NOINIT_ATTR uint8_t rtc_prev_image[OpenCO2EPaper::BUFFER_SIZE];

void OpenCO2EPaper::setup() {
  this->init_internal_(BUFFER_SIZE);
  if (this->buffer_ == nullptr) {
    this->mark_failed();
    return;
  }
  this->dc_pin_->setup();
  this->dc_pin_->digital_write(false);
  this->reset_pin_->setup();
  this->reset_pin_->digital_write(true);
  this->busy_pin_->setup();
  this->spi_setup();
  // No panel access here: the first update() initialises it, and until then
  // the panel keeps showing whatever was last drawn (e-paper needs no power).
}

void OpenCO2EPaper::fill(Color color) {
  if (this->get_clipping().is_set()) {
    Display::fill(color);
    return;
  }
  memset(this->buffer_, color.is_on() ? 0x00 : 0xFF, BUFFER_SIZE);
}

void HOT OpenCO2EPaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT)
    return;
  const uint32_t pos = (x + y * WIDTH) / 8u;
  const uint8_t bit = 0x80 >> (x & 0x07);
  if (color.is_on()) {
    this->buffer_[pos] &= ~bit;  // 0 = black
  } else {
    this->buffer_[pos] |= bit;  // 1 = white
  }
}

void OpenCO2EPaper::command_(uint8_t value) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(value);
  this->disable();
}

void OpenCO2EPaper::data_(uint8_t value) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(value);
  this->disable();
}

void OpenCO2EPaper::write_ram_(uint8_t ram_command, const uint8_t *image) {
  // Rewind the address counters, then stream the whole frame.
  this->command_(0x4E);
  this->data_(0x00);
  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);
  this->command_(ram_command);
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_array(image, BUFFER_SIZE);
  this->disable();
}

bool OpenCO2EPaper::wait_until_idle_(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {  // HIGH = busy
    if (millis() - start > timeout_ms) {
      ESP_LOGE(TAG, "Timeout waiting for the panel (BUSY stuck high)");
      return false;
    }
    // A full refresh blocks for ~1.5 s; keep the task watchdog (5 s) fed so
    // a stuck panel ends in the timeout above, not in a watchdog reset.
    App.feed_wdt();
    delay(1);
  }
  return true;
}

void OpenCO2EPaper::set_lut_(const uint8_t *lut) {
  this->command_(0x32);
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_array(lut, 153);
  this->disable();
  this->wait_until_idle_(1000);
  this->command_(0x3F);
  this->data_(lut[153]);
  this->command_(0x03);
  this->data_(lut[154]);
  this->command_(0x04);
  this->data_(lut[155]);
  this->data_(lut[156]);
  this->data_(lut[157]);
  this->command_(0x2C);
  this->data_(lut[158]);
}

// Full controller init after a hardware reset - the stock firmware's
// EPD_1IN54_V2_Init() / Init_Partial_After_Powerdown(). Always done, because
// the panel is reset or unpowered between updates and keeps no registers.
// RAM addressing (X/Y increment from the top left) is the same as ESPHome's
// waveshare_epaper, so `rotation:` means the same thing as before.
bool OpenCO2EPaper::init_panel_(bool full) {
  // 200 ms low: a short pulse starves the SSD1681's reset on this board.
  this->reset_pin_->digital_write(false);
  delay(200);  // NOLINT
  this->reset_pin_->digital_write(true);
  delay(20);  // NOLINT
  if (!this->wait_until_idle_(1000))
    return false;

  this->command_(0x12);  // software reset
  if (!this->wait_until_idle_(1000))
    return false;

  this->command_(0x01);  // driver output control: 200 gate lines
  this->data_(HEIGHT - 1);
  this->data_(0x00);
  this->data_(0x00);

  this->command_(0x11);  // data entry: X+, Y+
  this->data_(0x03);
  this->command_(0x44);  // RAM X window, in bytes
  this->data_(0x00);
  this->data_((WIDTH - 1) >> 3);
  this->command_(0x45);  // RAM Y window
  this->data_(0x00);
  this->data_(0x00);
  this->data_(HEIGHT - 1);
  this->data_(0x00);

  this->command_(0x3C);  // border waveform
  this->data_(full ? 0x01 : 0x80);
  this->command_(0x18);  // internal temperature sensor
  this->data_(0x80);

  this->command_(0x22);  // load temperature + waveform settings
  this->data_(0xB1);
  this->command_(0x20);
  if (!this->wait_until_idle_(1000))
    return false;

  this->set_lut_(full ? LUT_FULL : LUT_PARTIAL);
  return true;
}

void OpenCO2EPaper::sleep_panel_() {
  this->command_(0x10);  // deep sleep mode 1
  this->data_(0x01);
  delay(10);  // NOLINT
  this->reset_pin_->digital_write(false);  // as the stock driver does
}

void OpenCO2EPaper::update() {
  this->do_update_();  // render the lambda into buffer_

  const uint64_t now_us = esp_rtc_get_time_us();
  const bool have_prev = rtc_magic == RTC_MAGIC;
  const uint64_t interval_us = (uint64_t) this->full_update_interval_s_ * 1000000ULL;
  const bool due = !have_prev || now_us < rtc_last_full_us || now_us - rtc_last_full_us >= interval_us;
  const bool full = this->force_full_ || due;

  // Until the refresh has completed the panel contents are uncertain.
  rtc_magic = 0;

  if (!this->init_panel_(full)) {
    this->sleep_panel_();
    this->status_set_warning();
    return;
  }

  // The SSD1681 decides per pixel from the pair (0x26 "previous", 0x24 "new").
  // A full refresh starts from the new image in both, like the stock
  // DisplayPartBaseImage(). A partial refresh needs the image that is really
  // on the glass in 0x26 - which is what ESPHome's driver never wrote, so its
  // partial refreshes could not erase what was there before.
  this->write_ram_(0x26, full ? this->buffer_ : rtc_prev_image);
  this->write_ram_(0x24, this->buffer_);

  this->command_(0x22);
  this->data_(full ? 0xC7 : 0xCF);  // display mode 1 / mode 2, waveform from LUT
  this->command_(0x20);
  const bool ok = this->wait_until_idle_(full ? 5000 : 3000);
  this->sleep_panel_();
  if (!ok) {
    this->status_set_warning();
    return;
  }

  memcpy(rtc_prev_image, this->buffer_, BUFFER_SIZE);
  rtc_magic = RTC_MAGIC;
  if (full) {
    rtc_last_full_us = now_us;
    this->force_full_ = false;
  }
  this->status_clear_warning();
  ESP_LOGD(TAG, "%s refresh done", full ? "Full" : "Partial");
}

void OpenCO2EPaper::dump_config() {
  LOG_DISPLAY("", "OpenCO2 e-paper (SSD1681, 200x200)", this);
  ESP_LOGCONFIG(TAG, "  Full refresh at most every %" PRIu32 " s", this->full_update_interval_s_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace esphome::openco2_epaper
