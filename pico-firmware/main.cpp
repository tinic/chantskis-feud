#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"
#include "pico/stdlib.h"
#include "feud.h"
#include "usb_serial.h"
#include "command_handler.h"
#include "ws2812_controller.h"

// Callback function for when a line is received
static void on_line_received(std::string_view line) {
    CommandHandler::instance().handle_line(line);
}

int main() {
    // Initialize stdio (required for USB)
    stdio_init_all();

    USBSerial& usb_serial = USBSerial::instance();
    usb_serial.set_line_callback(on_line_received);
    
    sleep_ms(1000);
    
    usb_serial.send_line("chantskis feud usb serial interface");
    usb_serial.send_line("type 'help' for available commands");

    Feud& feud = Feud::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    // Set default rainbow animation on startup
    ws2812.set_animation(AnimationMode::RAINBOW, 100);
    
    while (1) {
        feud.update();
        ws2812.update();
        usb_serial.update();
        sleep_ms(10);  // Small delay instead of WFI to ensure regular updates
    }
}
