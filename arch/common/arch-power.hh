/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_POWER_HH
#define ARCH_POWER_HH

namespace osv {

void arch_halt(void) __attribute__((noreturn));
void arch_poweroff(void) __attribute__((noreturn));
void arch_reboot(void); /* if reboot fails, this can return */

} /* namespace osv */

#endif /* ARCH_POWER_HH */
