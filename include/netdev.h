#ifndef NET_NETDEV_H
#define NET_NETDEV_H

#include <stdint.h>

typedef void (*net_dev_send_t)(const uint8_t* frame, uint32_t len);
typedef void (*net_dev_poll_t)(void);

typedef struct net_dev {
    const char* name;
    uint16_t vendor;
    uint16_t device;
    net_dev_send_t send;
    net_dev_poll_t poll;
    int (*probe)(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device);
} net_dev_t;

#define NET_DEV_LIMIT 12

void net_drivers_init(void);
const net_dev_t* net_dev_probe(uint8_t bus, uint8_t slot, uint8_t func,
                               uint16_t vendor, uint16_t device);
const net_dev_t* net_dev_active(void);
void net_dev_set_active(const net_dev_t* drv);
uint32_t net_dev_count(void);
const net_dev_t* net_dev_get(uint32_t index);

void netdev_rx_submit(const uint8_t* frame, uint32_t len);
void netdev_set_mac(const uint8_t mac[6]);
void netdev_report_link(int up);
void netdev_set_name(const char* name);

void net_pci_enable(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bits);

#endif
