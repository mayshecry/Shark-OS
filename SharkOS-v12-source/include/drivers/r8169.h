#ifndef NET_DRIVERS_R8169_H
#define NET_DRIVERS_R8169_H

#include "netdev.h"

unsigned r8169_xid(void);
unsigned r8169_speed(void);
const net_dev_t* r8169_driver_get(void);

#endif
