/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>
#include <unistd.h>
#include <osv/debug.hh>
#include <osv/printf.hh>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/clock.hh>
#include <osv/itimer.hh>
#include <osv/signal.hh>

using namespace osv::clock::literals;

itimer::itimer(int signum, const char *name)
    : _alarm_thread(new sched::thread([&] { work(); },
                    sched::thread::attr().name(name)))
    , _signum(signum)
{
}

void itimer::cancel_this_thread()
{
    if(_owner_thread == sched::thread::current()) {
        WITH_LOCK(_mutex) {
            if(_owner_thread == sched::thread::current()) {
                cancel();
            }
        }
    }
}

int itimer::set(const struct itimerval *new_value,
    struct itimerval *old_value)
{
    if (!new_value)
        return EINVAL;

    WITH_LOCK(_mutex) {
        if (old_value) {
            get_interval(&old_value->it_interval);
            get_value(&old_value->it_value);
        }
        cancel();
        if (new_value->it_value.tv_sec || new_value->it_value.tv_usec) {
            set_interval(&new_value->it_interval);
            set_value(&new_value->it_value);
        }
     }
    return 0;
}

int itimer::get(struct itimerval *curr_value)
{
    WITH_LOCK(_mutex) {
        get_interval(&curr_value->it_interval);
        get_value(&curr_value->it_value);
    }
    return 0;
}

void itimer::work()
{
    sched::timer tmr(*sched::thread::current());
    while (true) {
        WITH_LOCK(_mutex) {
            if (_due != _no_alarm) {
                tmr.set(_due);
                _cond.wait(_mutex, &tmr);
                if (tmr.expired()) {
                    if (_interval != decltype(_interval)::zero()) {
                        auto now = osv::clock::uptime::now();
                        _due = now + _interval;
                    } else {
                        _due = _no_alarm;
                    }
                    osv::send_signal(_owner_thread, _signum);
                } else {
                    tmr.cancel();
                }
            } else {
                _cond.wait(_mutex);
            }
        }
    }
}

void itimer::cancel()
{
    _due = _no_alarm;
    _interval = decltype(_interval)::zero();
    _owner_thread = nullptr;
    _cond.wake_one();
}

void itimer::set_value(const struct timeval *tv)
{
    auto now = osv::clock::uptime::now();

    if (!_started) {
        osv::block_signals(_alarm_thread);
        _alarm_thread->start();
        _started = true;
    }
    _due = now + tv->tv_sec * 1_s + tv->tv_usec * 1_us;
    _owner_thread = sched::thread::current();
    _cond.wake_one();
}

void itimer::set_interval(const struct timeval *tv)
{
    _interval = tv->tv_sec * 1_s + tv->tv_usec * 1_us;
}

void itimer::get_value(struct timeval *tv)
{
    if (_due == _no_alarm) {
        tv->tv_sec = tv->tv_usec = 0;
    } else {
        auto now = osv::clock::uptime::now();
        fill_tv(_due - now, tv);
    }
}

void itimer::get_interval(struct timeval *tv)
{
    fill_tv(_interval, tv);
}
