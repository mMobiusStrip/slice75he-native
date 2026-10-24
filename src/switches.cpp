// Built-in switch table of the web configurator (AXISLIST ids index into it). Names are only known for
// the switches Chilkey ships; others are shown by id.
#include "switches.h"

#include <cstdio>

namespace {
const uint16_t kMaxTravel[] = {
    0, 3300, 3300, 3300, 3300, 3300, 3300, 3300, 3800, 3500, 3600, 3400,
    3400, 3400, 3400, 3400, 3700, 3800, 3300, 3300, 3600, 3400, 3500, 3600,
    3500, 3400, 3400, 3360, 3400, 3400, 3300, 3900, 3400, 3500, 3200, 3500,
    3500, 3700, 3400, 3600, 3400, 3400, 3400, 3400, 3800, 3300, 3300, 3300,
    3500, 3300, 4000, 3300, 3500, 3500, 3400, 3400, 3400, 3300, 3400, 3500,
    3500, 3500, 3500, 3600, 3300, 3500, 3300, 3400, 3400, 3400, 3300, 3300,
    3400, 3300, 3300, 3400, 3400, 3500, 3400, 3600, 3300, 3300, 3500, 3300,
    3500, 3300, 3500, 3300, 3100, 3300, 3300, 3300, 3500, 3400, 3400, 3400,
    3400, 3500, 3500, 3400, 3400, 3400, 3300, 3300, 3400, 3400, 3300, 3400,
    3400, 3400, 3400, 3400, 3400, 3400, 3400, 3300, 3400, 3300, 3400, 3400,
    3500, 3400, 3300, 3600, 3400, 3300, 3400, 3400, 3200, 3200, 3300, 3400,
    3400, 3400, 3400, 3200, 3600,
};
}  // namespace

uint16_t SwitchMaxTravel(uint16_t id) {
    return id < sizeof(kMaxTravel) / sizeof(kMaxTravel[0]) ? kMaxTravel[id] : 4000;
}

std::string SwitchName(uint16_t id) {
    switch (id) {
        case 1: case 58: return "Gateron Jade Pro";
        case 2: case 55: case 111: return "TTC Magneto RGB";
        case 3: return "TTC Magneto";
        case 4: case 105: return "Gateron Jade Gaming";
        case 9: return "Haimu";
        case 35: case 79: case 120: return "WS Flux";
        case 136: return "WS Flux-deep Octagon";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Switch #%u", id);
    return buf;
}