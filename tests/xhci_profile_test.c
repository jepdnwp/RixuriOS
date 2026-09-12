#include "../kernel/usb/xhci_profile.h"
#include <assert.h>

static void test_lookup_and_data(void) {
    const xhci_profile_t *p = xhci_profile_lookup(0x1022u, 0x15B7u);
    assert(p != 0);
    assert(p->expected_ports == 18u);
    assert(p->expected_hci == 0x0120u);
    assert(xhci_profile_port_known_bad(p, 9u));
    assert(xhci_profile_port_known_bad(p, 16u));
    assert(!xhci_profile_port_known_bad(p, 1u));
    assert(xhci_profile_lookup(0x1234u, 0x5678u) == 0);
}

static void test_verification(void) {
    const xhci_profile_t *p = xhci_profile_lookup(0x1022u, 0x15B8u);
    assert(p != 0);
    assert(xhci_profile_verify(p, 1u, 0x0120u, 0) == XHCI_PROFILE_OK);
    assert((xhci_profile_verify(p, 2u, 0x0110u, 0) & XHCI_PROFILE_VERIFY_PORTS) != 0u);
    assert((xhci_profile_verify(p, 1u, 0x0110u, 0) & XHCI_PROFILE_VERIFY_HCI) != 0u);
    assert((xhci_profile_verify(p, 1u, 0x0120u, 1) & XHCI_PROFILE_VERIFY_USB3) != 0u);
    assert(xhci_profile_verify(0, 99u, 0xffffu, 1) == XHCI_PROFILE_OK);
}

static void test_priority(void) {
    assert(xhci_profile_port_priority(2, 0) == 0);
    assert(xhci_profile_port_priority(3, 0) == 1);
    assert(xhci_profile_port_priority(0, 0) == 2);
    assert(xhci_profile_port_priority(2, 1) == 4);
    assert(xhci_profile_port_priority(3, 1) == 5);
}

int main(void) {
    test_lookup_and_data();
    test_verification();
    test_priority();
    return 0;
}
