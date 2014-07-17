/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
#include <api/sched.h>
#include <api/sys/resource.h>
#include <osv/sched.hh>

#include <osv/stubbing.hh>

/* sched_rr_get_interval writes the time quantum for SCHED_RR
 * processes to the tp timespec structure.
 * This is Linux-specific, and we don't implement this yet.
 */

int sched_rr_get_interval(pid_t pid, struct timespec * tp)
{
    WARN_STUBBED();
    errno = ENOSYS;
    return -1;
}

static int sched_get_priority_minmax(int policy, int value)
{
    policy &= ~SCHED_RESET_ON_FORK; /* ignore flag */

    switch (policy) {
    case SCHED_BATCH:
    case SCHED_IDLE:
    case SCHED_OTHER:
        return 0;
    case SCHED_FIFO:
    case SCHED_RR:
        return value;
    default:
        errno = EINVAL;
        /* error return value unfortunately overlaps with allowed
         * POSIX API allowed range. Another good reason to have
         * only rt priorities > 0.
         */
        return -1;
    }
}

int sched_get_priority_min(int policy)
{
    return sched_get_priority_minmax(policy, SCHED_PRIO_MIN);
}

int sched_get_priority_max(int policy)
{
    return sched_get_priority_minmax(policy, SCHED_PRIO_MAX);
}

static int sched_setparam_aux(sched::thread *t, int policy, int prio)
{
    policy &= ~SCHED_RESET_ON_FORK; /* ignore flag */
    int slice = 0;

    switch (policy) {
    case SCHED_OTHER:
        if (prio != 0) {
            errno = EINVAL;
            return -1;
        }
        t->set_realtime(0);
        // we need to set the non-RT priority to default, so that switching
        // from IDLE/BATCH to OTHER actually works;
        t->set_priority(1.0);
        break;
    case SCHED_BATCH:
    case SCHED_IDLE:
        if (prio != 0) {
            errno = EINVAL;
            return -1;
        }
        t->set_realtime(0);
        t->set_priority(sched::thread::priority_idle);
        break;
    case SCHED_FIFO:
        slice = -1;
    case SCHED_RR:
        if (prio < SCHED_PRIO_MIN || prio > SCHED_PRIO_MAX) {
            errno = EINVAL;
            return -1;
        }
        t->set_realtime(prio, sched::thread_realtime::duration(slice));
        // for consistency we set the non-RT priority to default here as well
        t->set_priority(1.0);
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int sched_setscheduler(pid_t pid, int policy,
                       const struct sched_param *param)
{
    sched::thread *t = sched::thread::current();

    if (pid != 0 && pid != getpid()) {
        errno = ESRCH;
        return -1;
    }
    if (!param) {
        errno = EINVAL;
        return -1;
    }

    return sched_setparam_aux(t, policy, param->sched_priority);
}

int sched_getscheduler(pid_t pid)
{
    /* XXX as we do not store the passed policy during setscheduler
     * and setparam, we have no way to distinguish IDLE from BATCH,
     * and also we do not keep the SCHED_RESET_ON_FORK flag.
     *
     * Therefore we end up returning a different value than the one
     * set. This is I think undesirable.
     */
    sched::thread *t = sched::thread::current();

    if (pid != 0 && pid != getpid()) {
        errno = ESRCH;
        return -1;
    }

    auto rt = t->get_realtime();

    if (rt._priority == 0) {
        return t->priority() != sched::thread::priority_idle ?
            SCHED_OTHER : SCHED_IDLE; /* XXX or SCHED_BATCH */
    }

    return rt._time_slice < 0 ? SCHED_FIFO : SCHED_RR;
}

int sched_setparam(pid_t pid, const struct sched_param *param)
{
    sched::thread *t = sched::thread::current();

    if (pid != 0 && pid != getpid()) {
        errno = ESRCH;
        return -1;
    }
    if (!param) {
        errno = EINVAL;
        return -1;
    }

    /* XXX since we are not storing the policy, we need to recalculate it
     * calling sched_getscheduler.
     */
    return sched_setparam_aux(t, sched_getscheduler(pid),
                              param->sched_priority);
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
    sched::thread *t = sched::thread::current();

    if (pid != 0 && pid != getpid()) {
        errno = ESRCH;
        return -1;
    }

    auto rt = t->get_realtime();
    param->sched_priority = rt._priority;
    return 0;
}
