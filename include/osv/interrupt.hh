/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* this is an abstract class for interrupts, archs derive from this one.
 *
 * ID is an interrupt identifier.
 * For x64 this means the gsi, for ARM64 this is the GIC interrupt ID.
 *
 * H is the interrupt handler routine.
 *
 * A in the interrupt ack routine.
 *   The ack routine is supposed to read the interrupt status and
 *   return true if the interrupt is pending. This is necessary for
 *   shared irqs.
 *
 * Derived classes are supposed to register in the constructor,
 * and deregister in the destructor.
 */

#ifndef INTERRUPT_HH
#define INTERRUPT_HH

#include <functional>

class interrupt {
public:
    interrupt(unsigned id, std::function<void ()> h,
              std::function<bool ()> a = []() { return true; })
        : interrupt_id(id), handler(h), ack(a) {};

    virtual ~interrupt() {};

    unsigned get_id() { return interrupt_id; }
    std::function<void (void)> get_handler() { return handler; }
    std::function<bool (void)> get_ack() { return ack; }

protected:
    unsigned interrupt_id;
    std::function<void (void)> handler;
    std::function<bool (void) > ack;
};

#include "arch-interrupt.hh"

#endif /* INTERRUPT_HH */
