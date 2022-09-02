#include <Libvm.h>
#include <cstring>

using namespace Libvm::Device;

namespace Libvm {
    namespace Device {
        void AddDevice(Device *devices[129], struct Device device) {
            assert(state->devices[device.id].id == 0);
            memcpy(&state->devices[device.id], &device, sizeof(struct Device));
        }

        void RemoveDevice(Device *devices[129], uint8_t Id) {
            struct Device *dev = &state->Devices[id];
                if (dev->destroy) {
                    dev->destroy(state, dev);
                }

                if (dev->state) {
                    free(dev->state);
                }

            dev->id = 0;
        }

        void RemoveDevices(Device *devices[129]) {
            struct Device *dev = &state->Devices[id];
            FOREACH_DEVICE(state, id, dev) {
                if (dev->id != 0) {
                    RemoveDevice(state, dev->id);
                } 
            }
        }
        
    }
}