

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/rtl8139.h"
#include "drivers/pcnet.h"
#include "drivers/e1000.h"
#include "drivers/r8169.h"
#include "drivers/ne2000.h"
#include "drivers/virtio_net.h"

static const net_dev_t* net_devs[NET_DEV_LIMIT];
static uint32_t net_dev_num;
static const net_dev_t* net_dev_current;

static void net_dev_install(const net_dev_t* drv) {
    if (!drv || net_dev_num >= NET_DEV_LIMIT) return;
    for (uint32_t i = 0; i < net_dev_num; i++)
        if (net_devs[i] == drv) return;
    net_devs[net_dev_num++] = drv;
}

void net_drivers_init(void) {
    net_dev_install(rtl8139_driver_get());
    net_dev_install(pcnet_driver_get());
    net_dev_install(e1000_driver_get());
    net_dev_install(r8169_driver_get());
    net_dev_install(ne2000_driver_get());
    net_dev_install(virtio_net_driver_get());
}

const net_dev_t* net_dev_probe(uint8_t bus, uint8_t slot, uint8_t func,
                               uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; i < net_dev_num; i++) {
        const net_dev_t* drv = net_devs[i];
        if (drv->probe && drv->probe(bus, slot, func, vendor, device)) return drv;
    }
    return NULL;
}

const net_dev_t* net_dev_active(void) { return net_dev_current; }
void net_dev_set_active(const net_dev_t* drv) { net_dev_current = drv; }
uint32_t net_dev_count(void) { return net_dev_num; }
const net_dev_t* net_dev_get(uint32_t index) {
    return index < net_dev_num ? net_devs[index] : NULL;
}
