/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch-cpu.hh"
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/mmu.hh>
#include <osv/irqlock.hh>
#include <osv/interrupt.hh>

void page_fault(exception_frame *ef)
{
    sched::exception_guard g;
    auto addr = processor::read_cr2();
    if (fixup_fault(ef)) {
        return;
    }
    auto pc = reinterpret_cast<void*>(ef->rip);
    if (!pc) {
        abort("trying to execute null pointer");
    }
    // The following code may sleep. So let's verify the fault did not happen
    // when preemption was disabled, or interrupts were disabled.
    assert(sched::preemptable());
    assert(ef->rflags & processor::rflags_if);

    // And since we may sleep, make sure interrupts are enabled.
    DROP_LOCK(irq_lock) { // irq_lock is acquired by HW
        sched::inplace_arch_fpu fpu;
        fpu.save();
        mmu::vm_fault(addr, ef);
        fpu.restore();
    }
}

namespace mmu {

void flush_tlb_local() {
    // TODO: we can use page_table_root instead of read_cr3(), can be faster
    // when shadow page tables are used.
    processor::write_cr3(processor::read_cr3());
}

// tlb_flush() does TLB flush on *all* processors, not returning before all
// processors confirm flushing their TLB. This is slow, but necessary for
// correctness so that, for example, after mprotect() returns, no thread on
// no cpu can write to the protected page.
mutex tlb_flush_mutex;
sched::thread_handle tlb_flush_waiter;
std::atomic<int> tlb_flush_pendingconfirms;

inter_processor_interrupt tlb_flush_ipi{[] {
        mmu::flush_tlb_local();
        if (tlb_flush_pendingconfirms.fetch_add(-1) == 1) {
            tlb_flush_waiter.wake();
        }
}};

void flush_tlb_all()
{
    mmu::flush_tlb_local();
    if (sched::cpus.size() <= 1)
        return;
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter.reset(*sched::thread::current());
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
    tlb_flush_waiter.clear();
}

static pt_element page_table_root;

void switch_to_runtime_page_tables()
{
    processor::write_cr3(page_table_root.next_pt_addr());
}

pt_element *get_root_pt(uintptr_t virt __attribute__((unused))) {
    return &page_table_root;
}

bool hw_ptep::change_perm(unsigned int perm)
{
    arch_pt_element pte = static_cast<arch_pt_element>(this->read());
    unsigned int old = (pte.valid() ? perm_read : 0) |
        (pte.writable() ? perm_write : 0) |
        (!pte.nx() ? perm_exec : 0);
    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    pte.set_valid(perm);
    pte.set_writable(perm & perm_write);
    pte.set_nx(!(perm & perm_exec));
    this->write(pte);

    return old & ~perm;
}

pt_element make_empty_pte() { return arch_pt_element(); }

pt_element make_pte(phys addr, bool large, unsigned perm)
{
    arch_pt_element pte;
    pte.set_valid(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_user(true);
    pte.set_accessed(true);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);
    pte.set_nx(!(perm & perm_exec));
    return pte;
}

pt_element make_normal_pte(phys addr, unsigned perm)
{
    return make_pte(addr, false, perm);
}

pt_element make_large_pte(phys addr, unsigned perm)
{
    return make_pte(addr, true, perm);
}

enum {
    page_fault_prot  = 1ul << 0,
    page_fault_write = 1ul << 1,
    page_fault_user  = 1ul << 2,
    page_fault_rsvd  = 1ul << 3,
    page_fault_insn  = 1ul << 4,
};

bool is_page_fault_insn(unsigned int error_code) {
    return error_code & page_fault_insn;
}

bool is_page_fault_write(unsigned int error_code) {
    return error_code & page_fault_write;
}

}
