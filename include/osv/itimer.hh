/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_ITIMER_HH_
#define OSV_ITIMER_HH_

#include <osv/error.h>
#include <osv/sched.hh>

class itimer {
public:
    explicit itimer(int signum, const char *name);
    void cancel_this_thread();
    int set(const struct itimerval *new_value,
        struct itimerval *old_value);
    int get(struct itimerval *curr_value);

private:
    void work();

    // Following functions doesn't take mutex, caller has responsibility
    // to take it
    void cancel();
    void set_interval(const struct timeval *tv);
    void set_value(const struct timeval *tv);
    void get_interval(struct timeval *tv);
    void get_value(struct timeval *tv);

    mutex _mutex;
    condvar _cond;
    sched::thread *_alarm_thread;
    sched::thread *_owner_thread = nullptr;
    const osv::clock::uptime::time_point _no_alarm {};
    osv::clock::uptime::time_point _due = _no_alarm;
    std::chrono::nanoseconds _interval;
    int _signum;
    bool _started = false;
};

#endif /* OSV_ITIMER_HH_ */
