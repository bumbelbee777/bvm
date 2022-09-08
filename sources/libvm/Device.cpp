#include <Libvm.h>
#include <cstring>

using namespace Libvm::Device;

namespace Libvm {
    namespace Device {
        void AddDevice(Device *devices[129], struct Device device) {
            assert(state->devices[device.Id].id == 0);
            memcpy(&state->devices[device.Id], &device, sizeof(struct Device));
        }

        void RemoveDevice(Device *devices[129], uint8_t Id) {
            struct Device *dev = &state->Devices[Id];
                if (dev->destroy) {
                    dev->destroy(state, dev);
                }

                if (dev->state) {
                    free(dev->state);
                }

            dev->Id = 0;
        }

        void RemoveDevices(Device *devices[129]) {
            FORDEVICES(dev->state, dev->Id, dev) {
                if (dev->Id != 0) {
                    RemoveDevice(dev->state, dev->Id);
                } 
            }
        }
    }
}