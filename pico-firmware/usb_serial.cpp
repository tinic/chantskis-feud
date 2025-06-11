#include "usb_serial.h"

#include <stdio.h>
#include <string.h>
#include <bit>
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "hardware/irq.h"

USBSerial& USBSerial::instance() {
    static USBSerial usb_serial;
    if (!usb_serial.initialized) {
        usb_serial.initialized = true;
        usb_serial.init();
    }
    return usb_serial;
}

void USBSerial::init() {
    stdio_usb_init();
    
    rx_buffer.fill(0);
    rx_buffer_pos = 0;
}

void USBSerial::set_line_callback(LineCallback callback) {
    line_callback = callback;
}

void USBSerial::send_line(std::string_view line) {
    send_data(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    
    if (line.empty() || line.back() != '\n') {
        constexpr std::string_view newline = "\n";
        send_data(reinterpret_cast<const uint8_t*>(newline.data()), newline.size());
    }
}

void USBSerial::send_data(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        putchar(data[i]);
    }
    fflush(stdout);
}

void USBSerial::process_rx_buffer() {
    char* newline = strchr(rx_buffer.data(), '\n');
    
    while (newline != nullptr) {
        *newline = '\0';
        
        size_t line_length = strlen(rx_buffer.data());
        if (line_length > 0 && rx_buffer[line_length - 1] == '\r') {
            rx_buffer[line_length - 1] = '\0';
        }
        
        if (line_callback) {
            line_callback(std::string_view{rx_buffer.data()});
        }
        
        size_t consumed = (newline - rx_buffer.data()) + 1;
        size_t remaining = rx_buffer_pos - consumed;
        if (remaining > 0) {
            memmove(rx_buffer.data(), newline + 1, remaining);
        }
        rx_buffer_pos = remaining;
        rx_buffer[rx_buffer_pos] = '\0';
        
        newline = strchr(rx_buffer.data(), '\n');
    }
}

void USBSerial::update() {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (rx_buffer_pos < BUFFER_SIZE - 1) {
            rx_buffer[rx_buffer_pos++] = (char)c;
            rx_buffer[rx_buffer_pos] = '\0';
            
            if (c == '\n') {
                process_rx_buffer();
            }
        } else {
            process_rx_buffer();
            
            if (rx_buffer_pos >= BUFFER_SIZE - 1) {
                rx_buffer_pos = 0;
                rx_buffer[0] = '\0';
            }
        }
    }
}