/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/power.hh>
#include <osv/debug.hh>
#include <osv/interrupt.hh>
#include <arch.hh>
#include <arch-power.hh>

#include "psci.hh"
#include <string.h>

namespace osv {

static inter_processor_interrupt smp_stop_cpu_ipi { IPI_SMP_STOP, [] {
    while (true) {
        arch::halt_no_interrupts();
    }
}};

void arch_halt(void)
{
    smp_stop_cpu_ipi.send_allbutself();

    while (true) {
        arch::halt_no_interrupts();
    }
}

void arch_poweroff(void)
{
    int ret = psci::_psci.system_off();
    debug_early("power: poweroff failed: ");
    debug_early(strerror(ret));
    debug_early("\n");
    halt();
}

void arch_reboot(void)
{
    int ret = psci::_psci.system_reset();
    debug_early("power: reboot failed: ");
    debug_early(strerror(ret));
    debug_early("\n");
}

} /* namespace osv */
