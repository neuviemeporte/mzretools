#include "dos/routine.h"

bool Routine::contains(const Address &addr) const {
    if (extents.contains(addr)) return true;
    for (const auto &c : chunks) {
        if (c.contains(addr)) return true;
    }
    return false;
}