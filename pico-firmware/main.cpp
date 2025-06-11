#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"
#include "pico/stdlib.h"
#include "feud.h"
#include "usb_serial.h"
#include "command_handler.h"

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
    
    usb_serial.send_line("Chantskis Feud USB Serial Interface");
    usb_serial.send_line("Type 'help' for available commands");

    Feud& feud = Feud::instance();
    while (1) {
        feud.update();
        usb_serial.update();
        __wfi();
    }
}
