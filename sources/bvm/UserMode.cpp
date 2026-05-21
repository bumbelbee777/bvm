#include "Shared.h"

#include <stdexcept>

using namespace Honeycomb;

bool Cpu::InUserMode() const {
    return (Fr & StatusFlags::UserMode) != 0;
}

void Cpu::RequireSupervisor(const char* Context) const {
    if (InUserMode()) {
        throw std::runtime_error(Context);
    }
}

void Cpu::OpEnableUser() {
    RequireSupervisor("Honeycomb EU is supervisor-only");
    if (InUserMode()) {
        throw std::runtime_error("Honeycomb EU with FR.UserMode already set");
    }
    Fr |= StatusFlags::UserMode;
}

void Cpu::OpDisableUser() {
    RequireSupervisor("Honeycomb DU is supervisor-only");
    Fr &= ~StatusFlags::UserMode;
}
