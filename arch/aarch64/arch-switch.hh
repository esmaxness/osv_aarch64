/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include <osv/barrier.hh>
#include <string.h>
#include "arch-setup.hh"

extern "C" {
void thread_main_c(sched::thread* t);
}

namespace sched {

void thread::switch_to()
{
    abort();
    /*
    asm volatile("\n"
                 "stp x29, x30, %0 \n"
                 "add x0, sp, #0   \n"
                 "adr x1, 1f       \n"
                 "stp x0, x1, %1   \n"
                 "1:               \n"
                 :
                 : "Ump"(this->fp), "Ump"(this->sp)
                 : "memory");
    */
}

void thread::switch_to_first()
{
    debug_early_entry("switch_to_first");

    barrier();
    arch_setup_tls(_tcb);
    barrier();

    s_current = this;
    current_cpu = _detached_state->_cpu;
    remote_thread_local_var(percpu_base) = _detached_state->_cpu->percpu_base;

    asm volatile("\n"
                 "ldp x29, x0, %0  \n"
                 "ldp x2, x1, %1   \n"
                 "mov sp, x2       \n"
                 "blr x1           \n"
                 :
                 : "Ump"(this->_state.fp), "Ump"(this->_state.sp)
                 : "x0", "x1", "x2", "x4", "x5", "x6", "x7", "x8",
                   "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                   "x16", "x17", "x18", "x19", "x20", "x21", "x22",
                   "x23", "x24", "x25", "x26", "x27", "x28", "x30",
                   "memory");

    debug_early("switch_to_first returning.\n");
}

void thread::init_stack()
{
    auto& stack = _attr._stack;
    if (!stack.size) {
        stack.size = 65536;
    }
    if (!stack.begin) {
        stack.begin = malloc(stack.size);
        stack.deleter = stack.default_deleter;
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    _state.fp = 0;
    _state.thread = this;
    _state.sp = stacktop;
    _state.pc = reinterpret_cast<void*>(thread_main_c);

    debug_early_u64("init_stack: _state.fp     = ", (u64)_state.fp);
    debug_early_u64("init_stack: _state.thread = ", (u64)_state.thread);
    debug_early_u64("init_stack: _state.sp     = ", (u64)_state.sp);
    debug_early_u64("init_stack: _state.pc     = ", (u64)_state.pc);
}

void thread::setup_tcb()
{
    assert(tls.size);

    void* p = malloc(sched::tls.size + sizeof(*_tcb));
    memcpy(p, sched::tls.start, sched::tls.size);
    _tcb = static_cast<thread_control_block*>(p + tls.size);
    _tcb->self = _tcb;
    _tcb->tls_base = p;
}

void thread::free_tcb()
{
    free(_tcb->tls_base);
}

void thread_main_c(thread* t)
{
    debug_early_u64("thread_main_c: thread* t=", (u64)t);

    arch::irq_enable();

#ifdef CONF_preempt
    preempt_enable();
#endif

    t->main();
    t->complete();
}

}

#endif /* ARCH_SWITCH_HH_ */
