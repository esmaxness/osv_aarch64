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

namespace pci {

void set_pci_cfg_base(u64 addr);
u64 get_pci_cfg_base();
void set_pci_io_base(u64 addr);
u64 get_pci_io_base();
void set_pci_mem_base(u64 addr);
u64 get_pci_mem_base();

/* set the pci irqmap converting slot addresses to gic irq ids */
void set_pci_irqmap(u32 *slots, int *irq_ids, int count, u32 mask);

/* dump the pci irqmap, useful for debugging */
void dump_pci_irqmap();

/* given a pci slot address, return the irq id or -1 on failure */
int get_pci_irq_from_slot(u32 slot_addr);

/* register a PCI irq handler. The irq number is looked up in the irq map. */
void register_pci_irq(u8 bus, u8 device, u8 func,
                      void *obj, interrupt_handler h);

void outb(u8 val, u16 port);
void outw(u16 val, u16 port);
void outl(u32 val, u16 port);
u8 inb(u16 port);
u16 inw(u16 port);
u32 inl(u16 port);

} /* namespace pci */

#endif /* ARCH_PCI_HH */
