# Realtek adapters: what works and how to test

| PCI ID | Role in this build |
| --- | --- |
| `10ec:8168` | Native polled Ethernet driver for the RTL8111/8168 family. The common PCI name also mentions 8211/8411; the ID alone does not identify the silicon stepping. Experimental on physical machines. |
| `10ec:8169` | Uses the same native ring driver. |
| `10ec:b821`, `10ec:c821` | RTL8821CE Wi-Fi is named in `devices` / `lspci`. **Detection only**: no radio firmware upload, scanning, association, WPA authentication or wireless packet transport. Never selected as an Ethernet NIC. |

## First boot on the machine

Boot the live desktop first; you do not need to install or overwrite a disk to test networking. Plug in a cable to a DHCP-capable router. Run `devices` to read the vendor/device pair, revision, subsystem and BARs. Run `ifconfig` to see which NIC is active. If it is the Realtek gigabit NIC, the output includes the MAC XID (chip revision), physical link and negotiated speed. Run `dhcp` if necessary, then `ping` the gateway address shown by `ifconfig`. Disconnect and reconnect the cable and check that the link state follows it. If this does not work, keep the corresponding `devices` and `ifconfig` lines and any `[r8169]` serial messages. Do not send Wi-Fi credentials. SharkOS still selects a single primary NIC, so seeing a PCI entry does not prove that it is the active one.

The driver looks for the first assigned memory BAR, handles 64-bit BAR pairs, and falls back to a valid BAR0 I/O range. It requests PCI D0 when needed, resets the MAC with a timeout, checks the station address, and programs aligned TX/RX descriptor rings and packet-size registers. Supported PHY register interfaces can advertise 1000BASE-T full-duplex and restart negotiation when the cable is down at startup. It samples PHY link status rather than assuming a cable is connected. Buffer lengths match the allocated buffers; RX fragments and descriptors flagged as errors are dropped. DMA memory is allocated only when the driver probes a matching NIC. This is not a port of every vendor PHY tuning sequence or firmware patch. It requires reachable BARs, direct-mapped DMA below 4 GiB, and no blocking IOMMU. Unknown revisions may still need chip-specific work.

## Using a wireless host in the meantime

If the machine already connects over RTL8821CE Wi-Fi under Linux, SharkOS **as a guest** can use that connection through QEMU user networking:

```sh
qemu-system-x86_64 -m 512 -cdrom sharkos.iso -nic user,model=rtl8139
```

The host joins the access point; the guest sees an emulated Ethernet card. This is **not** native Wi-Fi on a bare-metal SharkOS boot and does not let the guest select an SSID. A native RTL8821CE port needs the 8821C firmware and radio initialization, PCI DMA, an 802.11 station implementation and key management. An existing Linux `.ko` is not loadable by SharkOS.

## Verification

`make test-realtek` builds the production driver with simulated PCI registers, DMA memory, reset completion and PHY transactions. It exercises rejection of wireless IDs, BAR2 behind an I/O BAR0, reset timeout, D3hot-to-D0, bounded descriptor lengths, successful and failed RX, TX padding, ring wrap, ownership and cable changes. This validates code paths, not physical signal, negotiated throughput or interoperability. `make test-installed-boot` tests regression through an emulated RTL8139; it is not an RTL8168 or RTL8821CE hardware test.

Register references: [Linux r8169](https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/realtek/r8169_main.c), [iPXE realtek](https://github.com/ipxe/ipxe/blob/master/src/drivers/net/realtek.c), [RTL8821CE PCI IDs](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw88/rtw8821ce.c).
