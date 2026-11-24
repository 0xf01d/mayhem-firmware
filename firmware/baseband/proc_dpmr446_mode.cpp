/* See dpmr446_mode.hpp: which direction the combined image should run in. */
#include "dpmr446_mode.hpp"

#include "portapack_shared_memory.hpp"

namespace dpmr446 {

void set_mode(dpmr446_mode_t mode) {
    shared_memory.dpmr446_mode = (uint8_t)mode;
}

dpmr446_mode_t requested_mode() {
    return (dpmr446_mode_t)shared_memory.dpmr446_mode;
}

}  // namespace dpmr446
