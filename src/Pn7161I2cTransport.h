#pragma once

#include <Wire.h>
#include "transport.hpp"

// PN7161 NCI I2C framing (UM11495, section 4.2). Wire owns the shared bus;
// this transport owns only VEN/DWL and never calls Wire.end().
class Pn7161I2cTransport final : public IPN7160Transport
{
  public:
    Pn7161I2cTransport(TwoWire& wire, int sda, int scl, int irq, int ven,
                      int dwl, uint8_t address, uint32_t clock)
        : wire(wire), sda(sda), scl(scl), irq(irq), ven(ven), dwl(dwl),
          address(address), clock(clock) {}
    ~Pn7161I2cTransport() override { deinit(); }
    esp_err_t init() override;
    void deinit() override;
    esp_err_t read(uint8_t* buffer, size_t length) override;
    esp_err_t write(const uint8_t* buffer, size_t length) override;
    esp_err_t wait_for_irq(bool expected, TickType_t timeout) override;
    bool read_irq_level() const override;
    void set_ven(bool enabled) override;
    bool has_ven() const override { return ven >= 0; }

  private:
    TwoWire& wire;
    int sda, scl, irq, ven, dwl;
    uint8_t address;
    uint32_t clock;
    bool ready = false;
};
