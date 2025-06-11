#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"
#include "pico/stdlib.h"
#include "feud.h"

int main() {
    stdio_init_all();

    Feud& feud = Feud::instance();

    while (1) {
        feud.update();
        __wfi();
    }
}
