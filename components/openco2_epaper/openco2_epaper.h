#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::openco2_epaper {

class OpenCO2EPaper : public display::DisplayBuffer,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 public:
  static constexpr int WIDTH = 200;
  static constexpr int HEIGHT = 200;
  static constexpr uint32_t BUFFER_SIZE = WIDTH * HEIGHT / 8;

  void set_dc_pin(GPIOPin *pin) { this->dc_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { this->busy_pin_ = pin; }
  void set_full_update_interval(uint32_t seconds) { this->full_update_interval_s_ = seconds; }

  /// Make the next update a full (flashing) refresh, e.g. to clear ghosting.
  void request_full_update() { this->force_full_ = true; }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void fill(Color color) override;
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }

 protected:
  int get_width_internal() override { return WIDTH; }
  int get_height_internal() override { return HEIGHT; }
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  void command_(uint8_t value);
  void data_(uint8_t value);
  void write_ram_(uint8_t ram_command, const uint8_t *image);
  bool wait_until_idle_(uint32_t timeout_ms);
  bool init_panel_(bool full);
  void set_lut_(const uint8_t *lut);
  void sleep_panel_();

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  uint32_t full_update_interval_s_{7200};
  bool force_full_{false};
};

}  // namespace esphome::openco2_epaper
