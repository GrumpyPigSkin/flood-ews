#pragma once

#include "common/periodic_task.hpp"
#include "lorawan/modem.hpp"
#include <array>
#include <cstring>
#include <optional>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

namespace fog::lora {

/**
 * @brief E5 UART handles the OS interrupts and coordination between the nRF5340
 * and the LoRa E5-mini.
 */
class E5Uart {

  /** @brief ISR buffer size. */
  static constexpr std::size_t RX_ISR_BUF_SIZE = 64;

  /** @brief RX buffer size. */
  static constexpr std::size_t RX_BUF_SIZE = 256;

public:
  /**
   * @brief Constructor.
   * @param [in] uart A pointer to the device from the device tree.
   */
  explicit E5Uart(struct device const *const uart) : m_uart{uart} {}

  /**
   * @brief Initialise the instance.
   * @return true if the UART device is ready, safe to call until it is.
   */
  bool init() {
    if (!device_is_ready(m_uart)) {
      return false;
    }
    ring_buf_init(&m_rx_ring, m_rx_storage.size(), m_rx_storage.data());
    k_sem_init(&m_rx_sem, 0, 1);
    s_self = this;
    uart_irq_callback_user_data_set(m_uart, &E5Uart::isr, this);
    uart_irq_rx_enable(m_uart);
    return true;
  }

  /**
   * @brief A UartPort bound to this device.
   * write() drives the IRQ TX.
   * read_line() blocks on the RX ring + semaphore up to timeout_ms.
   * @return UartPort
   */
  UartPort port() {
    return UartPort{
        .m_write = [this](std::string_view str) { send(str); },
        .m_read_line =
            [this](std::uint32_t timeout) { return read_line(timeout); }};
  }

private:
  /**
   * @brief Send the string str over UART.
   * @param [in] str The string to send.
   */
  void send(const std::string_view str) {
    unsigned int key = irq_lock();
    m_tx = str.data();
    m_tx_len = str.size();
    m_tx_pos = 0;
    uart_irq_tx_enable(m_uart);
    irq_unlock(key);
  }

  /**
   * @brief Read a line up to `\n\r`.
   * @param [in] timeout_ms The timeout is miliseconds.
   * @return std::optional<std::string_view> nullopt if timedout.
   */
  std::optional<std::string_view> read_line(const std::uint32_t timeout_ms) {
    std::size_t pos = 0;
    const std::int64_t deadline = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < deadline) {
      // Wait fot a byte to appear on the RX buffer.
      std::uint8_t byte;
      if (ring_buf_get(&m_rx_ring, &byte, 1) == 0) {
        const std::int64_t remaining = deadline - k_uptime_get();
        const k_timeout_t wait =
            common::ms_to_k_timeout(std::max(remaining, std::int64_t{50}));
        k_sem_take(&m_rx_sem, wait);
        continue;
      }

      // If we get an \n\r then set the last char to \0 and return.
      if (byte == '\n') {
        if (pos > 0 && m_line[pos - 1] == '\r') {
          --pos;
        }
        m_line[pos] = '\0';
        return std::string_view{m_line.data(), pos};
      }

      // Keep processing chars.
      if (pos < m_line.size() - 1) {
        m_line[pos++] = static_cast<char>(byte);
      }
    }

    // Timedout.
    return std::nullopt;
  }

  /**
   * @brief The ISR called on RX and TX interrupts from the UART hardware.
   * @param [in] dev The device.
   * @param [in] user User data.
   */
  static void isr(const struct device *dev, void *user) {

    // Cast the user data back to our class instance.
    auto *self = static_cast<E5Uart *>(user);

    // Check this IRQ has actually been called.
    if (uart_irq_update(dev) == 0) {
      return;
    }

    // Handle the RX interrupt, read up to 64 bytes into buffer, add it to the
    // ring, and give the semaphore to release the thread waiting on RX data.
    if (uart_irq_rx_ready(dev) != 0) {
      std::array<std::uint8_t, RX_ISR_BUF_SIZE> buf{};
      const int len = uart_fifo_read(dev, buf.data(), buf.size());
      if (len > 0) {
        ring_buf_put(&self->m_rx_ring, buf.data(),
                     static_cast<std::uint32_t>(len));
        k_sem_give(&self->m_rx_sem);
      }
    }

    // Handle the TX ISR, send data out of our TX buffer until all data is sent,
    // the disable the ISR.
    if (uart_irq_tx_ready(dev) != 0) {
      if (self->m_tx_pos < self->m_tx_len) {
        const int sent = uart_fifo_fill(
            dev,
            reinterpret_cast<const std::uint8_t *>(self->m_tx + self->m_tx_pos),
            static_cast<int>(self->m_tx_len - self->m_tx_pos));
        self->m_tx_pos += sent;
      } else {
        uart_irq_tx_disable(dev);
      }
    }
  }

  /** @brief The hardware UART handle. */
  const struct device *m_uart;

  /** @brief The rx storage for the messages received over UART. */
  std::array<std::uint8_t, RX_BUF_SIZE> m_rx_storage{};

  /** @brief The rx ring that sits over the m_rx_storage. */
  ring_buf m_rx_ring{};

  /** @brief Semaphore for signalling when new data is available to be read. */
  k_sem m_rx_sem{};

  /** @brief Pointer to data to send over uart in the ISR. */
  const char *m_tx{nullptr};

  /** @brief The length of data to write. */
  volatile std::size_t m_tx_len{0};

  /** @brief The current position withing the data to send. */
  volatile std::size_t m_tx_pos{0};

  /**
   * @brief Buffer for assembling a line from the RX uart, a view to this is
   * returned when RX is completed.
   */
  std::array<char, RX_BUF_SIZE> m_line{};

  /** @brief Pointer to self. */
  static inline E5Uart *s_self{nullptr};
};

} // namespace fog::lora
