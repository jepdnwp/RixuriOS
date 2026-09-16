# xHCI Reset Endpoint TSP reference

The Intel xHCI specification PDF was consulted at:
https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf

For the implementation decision, Linux xHCI transaction-error recovery references the Reset Endpoint command with the Transfer State Preserve bit set (TSP=1), preserving transfer-ring/dequeue state for a soft reset. Supporting discussion:
https://lkml.indiana.edu/2308.3/02486.html

RixuriOS now sets bit 9 (`XHCI_TRB_TSP`) in its EP0 Reset Endpoint command before re-emitting the failed control TD at the same software ring position.

This note is engineering provenance, not physical success evidence. Physical post-fix enumeration still requires a new raw target log.
