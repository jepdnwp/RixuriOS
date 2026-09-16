/* Host test: USB hub class helpers (descriptor, route, TT, port state).
 * Hardware-touching paths (control transfers, attach) fail closed here
 * through stubs; QEMU covers them on emulated hubs.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/usb/hub.h"

/* ---- Stubs (fail closed, never succeed) ---- */
int xhci_control_transfer(size_t c, uint8_t s, const rix_usb_setup_packet_t *q,
                          void *d, uint16_t *a) {
    (void)c; (void)s; (void)q; (void)d; (void)a; return -1;
}
int xhci_enable_slot(size_t c, uint8_t *s) { (void)c; (void)s; return -1; }
int xhci_disable_slot(size_t c, uint8_t s) { (void)c; (void)s; return -1; }
int xhci_address_device(size_t c, uint8_t s, uint8_t p, uint8_t v,
                        const rix_xhci_tt_info_t *t) {
    (void)c; (void)s; (void)p; (void)v; (void)t; return -1;
}
int xhci_device_detach(size_t c, uint8_t s) { (void)c; (void)s; return -1; }
void xhci_usb_state_transition(size_t c, uint8_t s, uint8_t n, const char *r) {
    (void)c; (void)s; (void)n; (void)r;
}
void serial_write(const char *s) { (void)s; }
void serial_write_hex(uint64_t v) { (void)v; }
void serial_write_dec(uint64_t v) { (void)v; }

int main(void) {
    rix_usb_hub_descriptor_t hub;
    /* Valid 2-port hub descriptor. */
    static const uint8_t desc[] = {9, 0x29, 2, 0x09, 0x00, 100, 0, 0xff, 0xff};
    assert(usb_hub_parse_descriptor(desc, sizeof(desc), &hub) == 0);
    assert(hub.port_count == 2);
    assert(hub.characteristics == 0x0009u);
    assert(hub.power_good_2ms == 100u);
    /* Rejections: short, bad length, bad type, zero/too many ports. */
    static const uint8_t short_desc[] = {9, 0x29, 2, 0x09, 0x00, 100, 0, 0xff};
    assert(usb_hub_parse_descriptor(short_desc, sizeof(short_desc), &hub) != 0);
    static const uint8_t bad_type[] = {9, 0x04, 2, 0x09, 0x00, 100, 0, 0xff, 0xff};
    assert(usb_hub_parse_descriptor(bad_type, sizeof(bad_type), &hub) != 0);
    static const uint8_t no_ports[] = {9, 0x29, 0, 0x09, 0x00, 100, 0, 0xff, 0xff};
    assert(usb_hub_parse_descriptor(no_ports, sizeof(no_ports), &hub) != 0);
    static const uint8_t many_ports[] = {9, 0x29, 32, 0x09, 0x00, 100, 0, 0xff, 0xff};
    assert(usb_hub_parse_descriptor(many_ports, sizeof(many_ports), &hub) != 0);
    assert(usb_hub_parse_descriptor(0, 9, &hub) != 0);
    assert(usb_hub_parse_descriptor(desc, sizeof(desc), 0) != 0);
    /* Route strings: nibble (level-1) carries the parent port. */
    assert(usb_hub_route_string(0u, 1u, 5u) == 5u);
    assert(usb_hub_route_string(0x10u, 2u, 3u) == 0x40u);
    assert(usb_hub_route_string(0u, 1u, 20u) == 15u);
    assert(usb_hub_route_string(0x77u, 0u, 5u) == 0x77u);
    /* TT think-time codes from characteristics bits 6:5. */
    assert(usb_hub_think_code(0x0000u) == 0u);
    assert(usb_hub_think_code(0x0020u) == 1u);
    assert(usb_hub_think_code(0x0040u) == 2u);
    assert(usb_hub_think_code(0x0060u) == 3u);
    /* Port state decode: HIGH wins over LOW, else FULL. */
    rix_usb_hub_port_state_t ps;
    assert(usb_hub_port_state(0x0001u, 0, &ps) == 0);
    assert(ps.connected && !ps.enabled && ps.speed == 1u);
    assert(usb_hub_port_state(0x0103u, 0, &ps) == 0);
    assert(ps.connected && ps.enabled && ps.speed == 1u);
    assert(usb_hub_port_state(0x0201u, 0, &ps) == 0 && ps.speed == 2u);
    assert(usb_hub_port_state(0x0401u, 0, &ps) == 0 && ps.speed == 3u);
    assert(usb_hub_port_state(0x0601u, 0, &ps) == 0 && ps.speed == 3u);
    assert(usb_hub_port_state(0x0000u, 0, &ps) == 0 && !ps.connected);
    assert(usb_hub_port_state(0x0001u, 0, 0) != 0);
    /* Control paths fail closed through stubs. */
    {
        uint16_t st = 0;
        uint16_t ch = 0;
        uint16_t actual = 0;
        uint8_t buf[16];
        usb_hub_parent_t parent = {1, 0, 1, 0};
        rix_xhci_device_t dev;
        assert(usb_hub_get_descriptor(0, 1, buf, sizeof(buf), &actual) != 0);
        assert(usb_hub_get_descriptor(0, 1, 0, sizeof(buf), &actual) != 0);
        assert(usb_hub_port_status(0, 1, 1, &st, &ch) != 0);
        assert(usb_hub_port_status(0, 1, 0, &st, &ch) != 0);
        assert(usb_hub_set_port_feature(0, 1, 0, 8u) != 0);
        assert(usb_hub_clear_port_feature(0, 1, 0, 16u) != 0);
        assert(usb_hub_port_reset(0, 1, 1) != 0);
        assert(usb_hub_port_reset(0, 1, 0) != 0);
        assert(usb_hub_attach_child(0, &parent, 1, &dev) != 0);
        assert(usb_hub_attach_child(0, 0, 1, &dev) != 0);
        assert(usb_hub_attach_child(0, &parent, 0, &dev) != 0);
    }
    printf("usb-hub: OK\n");
    return 0;
}
