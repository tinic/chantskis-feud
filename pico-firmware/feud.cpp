#include "feud.h"

#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

Feud& Feud::instance() {
    static Feud feud;
    if (!feud.initialized) {
        feud.initialized = true;
        feud.init();
    }
    return feud;
}

void Feud::init() {
    btn_gpio_init();
}

void Feud::btn_gpio_init() {
}

void Feud::update() {
}
