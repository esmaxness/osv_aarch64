/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch/x64/atomic.h"

#include "signal.hh"
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <osv/debug.hh>
#include <osv/printf.hh>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/power.hh>
#include <osv/clock.hh>
#include <api/setjmp.h>
#include <osv/stubbing.hh>

using namespace osv::clock::literals;

namespace osv {

// we can't use have __thread sigset because of the constructor
__thread __attribute__((aligned(sizeof(sigset))))
    char thread_blocked_sigmask[sizeof(sigset)];
__thread __attribute__((aligned(sizeof(sigset))))
    char thread_pending_sigmask[sizeof(sigset)];

unsigned long default_sig_ignored = 1 << SIGCONT | 1 << SIGCHLD |
                                    1 << SIGWINCH | 1 << SIGURG;
struct sigaction signal_actions[nsignals];

sigset* from_libc(sigset_t* s)
{
    return reinterpret_cast<sigset*>(s);
}

const sigset* from_libc(const sigset_t* s)
{
    return reinterpret_cast<const sigset*>(s);
}

sigset* thread_blocked_signals()
{
    return reinterpret_cast<sigset*>(thread_blocked_sigmask);
}

sigset* thread_blocked_signals(sched::thread *t)
{
    if (t != sched::thread::current()) {
        return t->remote_thread_local_ptr<sigset>(&thread_blocked_sigmask);
    } else {
        return thread_blocked_signals();
    }
}

sigset* thread_pending_signals()
{
    return reinterpret_cast<sigset*>(thread_pending_sigmask);
}

sigset* thread_pending_signals(sched::thread *t)
{
    if (t != sched::thread::current()) {
        return t->remote_thread_local_ptr<sigset>(&thread_pending_sigmask);
    } else {
        return thread_pending_signals();
    }
}

inline bool is_sig_dfl(const struct sigaction &sa) {
    return sa.sa_handler == SIG_DFL;
}

inline bool is_sig_ign(const struct sigaction &sa) {
    return sa.sa_handler == SIG_IGN;
}

inline bool is_sig_dfl_ign(int sig) {
    return default_sig_ignored & (1 << sig);
}

inline bool is_sig_pending(int sig) {
    return thread_pending_signals()->mask.test(sig);
}

inline bool is_sig_pending(sched::thread *t, int sig) {
    return thread_pending_signals(t)->mask.test(sig);
}

inline bool is_sig_blocked(int sig) {
    return thread_blocked_signals()->mask.test(sig);
}

inline bool is_sig_blocked(sched::thread *t, int sig) {
    return thread_blocked_signals(t)->mask.test(sig);
}

typedef std::list<sched::thread *> thread_list;
static std::array<thread_list, nsignals> waiters;
mutex waiters_mutex;

sched::thread *get_first_signal_waiter(int signo)
{
    SCOPE_LOCK(waiters_mutex);

    if (waiters[signo].empty())
        return nullptr;

    return waiters[signo].first();
}

void wait_for_signal(int signo)
{
    SCOPE_LOCK(waiters_mutex);
    waiters[signo].push_front(sched::thread::current());
}

void unwait_for_signal(sched::thread *t, int signo)
{
    SCOPE_LOCK(waiters_mutex);
    waiters[signo].remove(t);
}

bool is_waiting_for_signal(sched::thread *t, int signo)
{
    SCOPE_LOCK(waiters_mutex);
    for (auto t2 : waiters[signo]) {
        if (t2 == t)
            return true;
    }
    return false;
}

void unwait_for_signal(int signo)
{
    unwait_for_signal(sched::thread::current(), signo);
}

void __attribute__((constructor)) signals_register_thread_notifier()
{
    sched::thread::register_exit_notifier(
        [](sched::thread *t) {
            sigset *set = thread_blocked_signals(t);
            if (!set->mask.any()) { return; }
            for (int i = 0; i < nsignals; i++) {
                if (set->mask.test(i)) {
                    unwait_for_signal(t, i);
                }
            }
        }
    );
}

/* perform the action associated with a signal */
void complete_signal(sched::thread *t, int sig)
{
    if (t == sched::thread::current()) {
        assert(is_sig_pending(sig));
        thread_pending_signals()->mask.reset(sig);
    } else {
        assert(is_sig_pending(t, sig));
        thread_pending_signals(t)->mask.reset(sig);
    }

    if (is_sig_dfl(signal_actions[sig])) {
        if (is_sig_dfl_ign(sig)) {
            /* do nothing */
            return;
        } else {
            abort("received signal %d (\"%s\"). Aborting.\n",
                  sig, strsignal(sig));
        }
    }

    if (is_sig_ign(signal_actions[sig])) {
        /* do nothing */
        return;
    }

    // User-defined signal handler. Run it in a new thread. This isn't
    // very Unix-like behavior.
    const auto sa = signal_actions[sig];
    auto newt = new sched::thread([=] {
            if (sa.sa_flags & SA_RESETHAND) {
                signal_actions[sig].sa_flags = 0;
                signal_actions[sig].sa_handler = SIG_DFL;
            }
            if (sa.sa_flags & SA_SIGINFO) {
                // FIXME: proper second (siginfo) and third (context) arguments (See example in call_signal_handler)
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                if (sa.sa_flags & SA_RESETHAND) {
                    signal_actions[sig].sa_flags = 0;
                    signal_actions[sig].sa_handler = SIG_DFL;
                }
                sa.sa_handler(sig);
            }
        }, sched::thread::attr().detached().stack(65536).name("signal_handler"));
    t->interrupted(true);
    newt->start();
}

/* send a signal to a thread; t = 0 sends to any thread */
void send_signal(sched::thread *t, int sig)
{
    bool t_waiting = false;

    if (!t) { /* need to find a suitable thread */
        t = get_first_signal_waiter(sig);
        if (t) {
            t_waiting = true;
        } else {
            sched::with_all_threads([&](sched::thread &next) {
                    if (!is_sig_blocked(&next, sig)) {
                        t = &next;
                    }
            });
            if (!t) {
                /* quite uncommon occurrence: all threads blocked this.
                 * We should set it as pending globally, for the next
                 * thread which unblocks it or sigwaits for it;
                 * instead, we set it as pending for current.
                 */
                debug("send_signal: signal %d is blocked globally.\n", sig);
                t = sched::thread::current();
            }
        }
    } else {    /* check the specific thread */
        if (t != sched::thread::current() &&
            is_waiting_for_signal(t, sig)) {
            t_waiting = true;
        }
    }

    if (t == sched::thread::current()) {
        thread_pending_signals()->mask.set(sig);
    } else {
        thread_pending_signals(t)->mask.set(sig);
    }

    if (t_waiting) {
        t->wake();
    } else if (!is_sig_blocked(t, sig)) {
        complete_signal(t, sig);
    } else if (t == sched::thread::current()) {
        thread_pending_signals()->mask.set(sig);
    } else {
        thread_pending_signals(t)->mask.set(sig);
    }
}

void generate_signal(siginfo_t &siginfo, exception_frame* ef)
{
    int sig = siginfo.si_signo;

    if (is_sig_dfl(signal_actions[sig])) {
        if (!is_sig_dfl_ign(sig)) {
            abort("generated signal %d (\"%s\"): aborting.\n",
                  sig, strsignal(sig));

        } else if (!is_sig_ign(signal_actions[sig])) {
            arch::build_signal_frame(ef, siginfo, signal_actions[sig]);
        }
    }
}

void handle_mmap_fault(ulong addr, int sig, exception_frame* ef)
{
    siginfo_t si;
    si.si_signo = sig;
    si.si_addr = reinterpret_cast<void*>(addr);
    generate_signal(si, ef);
}

int sigwait_pred(const sigset *set)
{
    auto temp = thread_pending_signals()->mask & set->mask;
    unsigned long mask = temp.to_ulong();

    if (mask) {
        int found = a_ctz_64(mask);
        return found;
    } else {
        return 0;
    }
}

}

using namespace osv;

int sigemptyset(sigset_t* sigset)
{
    from_libc(sigset)->mask.reset();
    return 0;
}

int sigfillset(sigset_t *sigset)
{
    from_libc(sigset)->mask.set();
    return 0;
}

int sigaddset(sigset_t *sigset, int signum)
{
    from_libc(sigset)->mask.set(signum);
    return 0;
}

int sigdelset(sigset_t *sigset, int signum)
{
    from_libc(sigset)->mask.reset(signum);
    return 0;
}

int sigismember(const sigset_t *sigset, int signum)
{
    return from_libc(sigset)->mask.test(signum);
}

void sigprocmask_block(int sig)
{
    /* block an unblocked signal */
    thread_blocked_signals()->mask.set(sig);
}

void sigprocmask_unblock(int sig)
{
    /* unblock a blocked signal */
    if (is_sig_pending(sig)) {
        complete_signal(sched::thread::current(), sig);
    }
    thread_blocked_signals()->mask.reset(sig);
}

int sigprocmask(int how, const sigset_t* _set, sigset_t* _oldset)
{
    auto set = from_libc(_set);
    auto oldset = from_libc(_oldset);
    if (oldset) {
        *oldset = *thread_blocked_signals();
    }
    if (set) {
        for (int i = 0; i < nsignals; i++) {
            switch (how) {
            case SIG_BLOCK:
                if (!is_sig_blocked(i) && set->mask.test(i))
                    sigprocmask_block(i);
                break;
            case SIG_UNBLOCK:
                if (is_sig_blocked(i) && set->mask.test(i))
                    sigprocmask_unblock(i);
                break;
            case SIG_SETMASK:
                if (set->mask.test(i)) {
                    if (!is_sig_blocked(i))
                        sigprocmask_block(i);
                } else {
                    if (is_sig_blocked(i))
                        sigprocmask_unblock(i);
                }
                break;
            default:
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    // FIXME: We do not support any sa_flags besides SA_SIGINFO.
    if (signum < 0 || signum >= nsignals) {
        errno = EINVAL;
        return -1;
    }
    if (oldact) {
        *oldact = signal_actions[signum];
    }
    if (act) {
        signal_actions[signum] = *act;
    }
    return 0;
}

// using signal() is not recommended (use sigaction instead!), but some
// programs like to call to do simple things, like ignoring a certain signal.
static sighandler_t signal(int signum, sighandler_t handler, int sa_flags)
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    act.sa_flags = sa_flags;
    struct sigaction old;
    if (sigaction(signum, &act, &old) < 0) {
        return SIG_ERR;
    }
    if (old.sa_flags & SA_SIGINFO) {
        // TODO: Is there anything sane to do here?
        return nullptr;
    } else {
        return old.sa_handler;
    }
}

sighandler_t signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESTART);
}

extern "C"
sighandler_t __sysv_signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESETHAND | SA_NODEFER);
}

// using sigignore() and friends is not recommended as it is obsolete System V
// APIs. Nevertheless, some programs use it.
int sigignore(int signum)
{
    struct sigaction act;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    return sigaction(signum, &act, nullptr);
}

int sigwait(const sigset_t *_set, int *sig)
{
    int found;
    if (!_set) {
        errno = EINVAL;
        return -1;
    }
    const osv::sigset set = *(from_libc(_set));
    found = sigwait_pred(&set);
    if (found) {
        thread_pending_signals()->mask.reset(found);
        *sig = found;
        return 0;
    }

    for (int i = 0; i < nsignals; i++) {
        if (set.mask.test(i)) {
            wait_for_signal(i);
        }
    }
    sched::thread::wait_until([set, &found] {
            return found = sigwait_pred(&set);
    });

    thread_pending_signals()->mask.reset(found);
    *sig = found;

    for (int i = 0; i < nsignals; i++) {
        if (set.mask.test(i)) {
            unwait_for_signal(i);
        }
    }
    return 0;
}

// Partially-Linux-compatible support for kill(2).
// Note that this is different from our generate_signal() - the latter is only
// suitable for delivering SIGFPE and SIGSEGV to the same thread that called
// this function.
//
// Handling kill(2)/signal(2) exactly like Linux, where one of the existing
// threads runs the signal handler, is difficult in OSv because it requires
// tracking of when we're in kernel code (to delay the signal handling until
// it returns to "user" code), and also to interrupt sleeping kernel code and
// have it return sooner.
// Instead, we provide a simple "approximation" of the signal handling -
// on each kill(), a *new* thread is created to run the signal handler code.
//
// This approximation will work in programs that do not care about the signal
// being delivered to a specific thread, and that do not intend that the
// signal should interrupt a system call (e.g., sleep() or hung read()).
// FIXME: think if our handling of nested signals is ok (right now while
// handling a signal, we can get another one of the same signal and start
// another handler thread. We should probably block this signal while
// handling it.

int kill(pid_t pid, int sig)
{
    // OSv only implements one process, whose pid is getpid().
    // Sending a signal to pid 0 or -1 is also fine, as it will also send a
    // signal to the same single process.
    if (pid != getpid() && pid != 0 && pid != -1) {
        errno = ESRCH;
        return -1;
    }
    if (sig == 0) {
        // kill() with signal 0 doesn't cause an actual signal 0, just
        // testing the pid.
        return 0;
    }
    if (sig < 0 || sig >= nsignals) {
        errno = EINVAL;
        return -1;
    }

    /* send the signal to any thread */
    send_signal(nullptr, sig);
    return 0;
}

// Our alarm() implementation has one system-wide alarm-thread, which waits
// for the single timer (or instructions to change the timer) and sends
// SIGALRM when the timer expires.
// alarm() is an archaic Unix API and didn't age well. It should should never
// be used in new programs.

class itimer {
public:
    explicit itimer(int signum, const char *name);
    void cancel_this_thread();
    int set(const struct itimerval *new_value,
        struct itimerval *old_value);
    int get(struct itimerval *curr_value);

private:
    void work();

    // Fllowing functions doesn't take mutex, caller has responsibility
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

static itimer itimer_real(SIGALRM, "itimer-real");
static itimer itimer_virt(SIGVTALRM, "itimer-virt");

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
                    send_signal(_owner_thread, _signum);
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

void cancel_this_thread_alarm()
{
    itimer_real.cancel_this_thread();
    itimer_virt.cancel_this_thread();
}

unsigned int alarm(unsigned int seconds)
{
    unsigned int ret;
    struct itimerval old_value{}, new_value{};

    new_value.it_value.tv_sec = seconds;

    setitimer(ITIMER_REAL, &new_value, &old_value);

    ret = old_value.it_value.tv_sec;

    if ((ret == 0 && old_value.it_value.tv_usec) ||
        old_value.it_value.tv_usec >= 500000)
        ret++;

    return ret;
}

extern "C" int setitimer(int which, const struct itimerval *new_value,
    struct itimerval *old_value)
{
    switch (which) {
    case ITIMER_REAL:
        return itimer_real.set(new_value, old_value);
    case ITIMER_VIRTUAL:
        return itimer_virt.set(new_value, old_value);
    default:
        return EINVAL;
    }
}

extern "C" int getitimer(int which, struct itimerval *curr_value)
{
    switch (which) {
    case ITIMER_REAL:
        return itimer_real.get(curr_value);
    case ITIMER_VIRTUAL:
        return itimer_virt.get(curr_value);
    default:
        return EINVAL;
    }
}

int sigaltstack(const stack_t *ss, stack_t *oss)
{
    WARN_STUBBED();
    return 0;
}

extern "C" int __sigsetjmp(sigjmp_buf env, int savemask)
{
    WARN_STUBBED();
    return 0;
}
