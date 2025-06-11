#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stddef.h>
#include <stdint.h>
#include <array>
#include <string_view>

class USBSerial {
 private:
    bool initialized = false;
    static constexpr size_t BUFFER_SIZE = 256;
    std::array<char, BUFFER_SIZE> rx_buffer{};
    size_t rx_buffer_pos = 0;
    
    using LineCallback = void (*)(std::string_view line);
    LineCallback line_callback = nullptr;
    
    void init();
    void process_rx_buffer();
    
 public:
    static USBSerial& instance();
    
    void set_line_callback(LineCallback callback);
    
    void send_line(std::string_view line);
    
    void send_data(const uint8_t* data, size_t length);
    
    void update();
};

#endif  // USB_SERIAL_H