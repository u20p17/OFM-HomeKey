#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "Pn7161I2cTransport.h"
#include <Arduino.h>
#include <esp32-hal-i2c.h>
#include <esp_idf_version.h>
#include <freertos/task.h>
#include <cstring>

esp_err_t Pn7161I2cTransport::init()
{
    if (ready) return ESP_OK;
    if (sda < 0 || scl < 0 || irq < 0 || ven < 0 || address < 8 || address > 0x77)
        return ESP_ERR_INVALID_ARG;
    if (dwl >= 0)
    {
        pinMode(dwl, OUTPUT);
        digitalWrite(dwl, LOW);
    }
    pinMode(ven, OUTPUT);
    digitalWrite(ven, LOW);
    pinMode(irq, INPUT);
    if (wire.setBufferSize(258) < 258) return ESP_ERR_NO_MEM;
    if (!wire.begin(sda, scl, clock)) return ESP_FAIL;
    wire.setTimeOut(20);
    ready = true;
    return ESP_OK;
}

void Pn7161I2cTransport::deinit()
{
    if (ready) digitalWrite(ven, LOW);
    ready = false;
}

esp_err_t Pn7161I2cTransport::read(uint8_t* buffer, size_t length)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (!buffer || length == 0 || length > 258) return ESP_ERR_INVALID_ARG;
    // NCI reads the three-byte header, then its payload using split mode.
    // A partial transaction must never expose stale data as a valid packet.
    std::memset(buffer, 0, length);
    if (wire.requestFrom(address, length) != length) return ESP_FAIL;
    for (size_t i = 0; i < length; ++i)
    {
        const int value = wire.read();
        if (value < 0) { std::memset(buffer, 0, length); return ESP_FAIL; }
        buffer[i] = static_cast<uint8_t>(value);
    }
    return ESP_OK;
}

esp_err_t Pn7161I2cTransport::write(const uint8_t* buffer, size_t length)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (!buffer || length == 0 || length > 258) return ESP_ERR_INVALID_ARG;
    // Wire collapses ESP-IDF 5.4/5.5 NACKs into status 4. Use its locked HAL
    // to retain the error; TwoWire still owns bus initialization and reads.
    if (!i2cIsInit(wire.getBusNum())) return ESP_ERR_INVALID_STATE;
    esp_err_t status = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        // Let the NCI receiver drain pending notifications before writing.
        if (wait_for_irq(false, pdMS_TO_TICKS(20)) != ESP_OK) return ESP_ERR_TIMEOUT;
        status = i2cWrite(wire.getBusNum(), address, buffer, length, wire.getTimeOut());
        if (status == ESP_OK) return ESP_OK;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        const bool nack = status == ESP_ERR_INVALID_RESPONSE;
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        const bool nack = status == ESP_ERR_INVALID_STATE;
#else
        const bool nack = status == ESP_FAIL;
#endif
        // UM11495 section 4.2.3 requires resending the complete frame after
        // any byte is NACKed (including standby wake-up). Never retry a timeout.
        if (!nack || attempt == 2) return status;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return status;
}

esp_err_t Pn7161I2cTransport::wait_for_irq(bool expected, TickType_t timeout)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    const TickType_t start = xTaskGetTickCount();
    while (read_irq_level() != expected)
    {
        if (timeout != portMAX_DELAY && xTaskGetTickCount() - start >= timeout)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    return ESP_OK;
}

bool Pn7161I2cTransport::read_irq_level() const
{
    return ready && digitalRead(irq) == HIGH;
}

void Pn7161I2cTransport::set_ven(bool enabled)
{
    if (ready)
    {
        digitalWrite(ven, enabled ? HIGH : LOW);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif
