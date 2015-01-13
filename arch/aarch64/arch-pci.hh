/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_PCI_HH
#define ARCH_PCI_HH

#include <osv/mmio.hh>
#include "exceptions.hh"
#include "drivers/pci-device.hh"

namespace pci {

void set_pci_ecam(bool is_ecam);
bool get_pci_ecam();
void set_pci_cfg_base(u64 addr);
u64 get_pci_cfg_base();
void set_pci_io_base(u64 addr);
u64 get_pci_io_base();
void set_pci_mem_base(u64 addr);
u64 get_pci_mem_base();

/* set the pci irqmap converting phys.hi addresses to gic irq ids */
void set_pci_irqmap(u32 *bfds, int *irq_ids, int count, u32 mask);

/* dump the pci irqmap, useful for debugging */
void dump_pci_irqmap();

/* given a pci phys.hi address masked for BDF and orred with pin,
 * return irqid or -1 on failure
 */
int get_pci_irq_from_bdfp(u32 bdfp);

/* register a PCI irq handler. The irq number is looked up in the irq map. */
void register_pci_irq(pci::device &dev, void *obj, interrupt_handler h);

void outb(u8 val, u16 port);
void outw(u16 val, u16 port);
void outl(u32 val, u16 port);
u8 inb(u16 port);
u16 inw(u16 port);
u32 inl(u16 port);

} /* namespace pci */

#endif /* ARCH_PCI_HH */
