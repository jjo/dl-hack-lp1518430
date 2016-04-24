#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <assert.h>


static inline long int _timeval_to_usec(const struct timeval *tv)
{
    return tv->tv_sec * 1000000 + tv->tv_usec;
}

static inline long int _timeval_from_usec(struct timeval *tv, long int usec)
{
    tv->tv_sec = usec / 1000000;
    tv->tv_usec = usec % 1000000;
    return usec;
}


static inline long int _timeval_diff(struct timeval *result,
                                     const struct timeval *x,
                                     const struct timeval *y)
{
    long int result_usec;
    result_usec= _timeval_to_usec(y) - _timeval_to_usec(x);
    if (result) {
        _timeval_from_usec(result, result_usec);
    }
    return result_usec;
}

#define _EPOLL_WAIT_HACK_EXE "python2.7"

/* Values above this timeout [ms] value are passed along unchanged */
#define _EPOLL_WAIT_TIMEOUT_MIN_OK   50

/* Target this hacked timeout [ms] (final value depends
 * on time since last call */
#define _EPOLL_WAIT_TIMEOUT_TARGET (1<<9)  /* 512 */

/* Fuzz these lower bits [usec], should be ~5% of above times 1000 */
#define FUZZ_MASK ((1<<13)-1)

static int HACK_ON = 0;
static int FUZZ_PID = 0;

static void _epoll_wait_hack_init(void) __attribute__((constructor));

/* init function, called before main() */
static void _epoll_wait_hack_init(void)
{
    char buf[256];
    int n;
    n = readlink("/proc/self/exe", buf, sizeof buf);
    buf[n] = 0;
    /* ONLY activate hack for _EPOLL_WAIT_HACK_EXE binary,
       compare that above string ends with _EPOLL_WAIT_HACK_EXE */
    if (strncmp(_EPOLL_WAIT_HACK_EXE,
                buf + n - strlen( _EPOLL_WAIT_HACK_EXE),
                strlen(_EPOLL_WAIT_HACK_EXE)) == 0) {
        HACK_ON = 1;
        FUZZ_PID = getpid() & FUZZ_MASK;
    }
    if (getenv("EPOLL_WAIT_HACK_DEBUG")) {
        fprintf(stderr, "EPOLL_WAIT_HACK_ON=%d\n", HACK_ON);
    }
}


/* return new timeout value [usec], copy it also to new_timeout_tv if not NULL */
static inline long int _epoll_wait_hack_timeout(
    const struct timeval *cur_timeout_tv,
    struct timeval *new_timeout_tv)
{
    static struct timeval TV0 = { 0, 0 };
    static struct timeval TV1 = { 0, 0 };
    long int since_last_usec, timeout_usec, new_timeout_usec;

    timeout_usec = _timeval_to_usec(cur_timeout_tv);
    if ( timeout_usec > _EPOLL_WAIT_TIMEOUT_MIN_OK * 1000) {
        if (new_timeout_tv)
            *new_timeout_tv = *cur_timeout_tv;
        return timeout_usec;
    }
    gettimeofday(&TV1, NULL);
    /* Get the time diff against previous call */
    since_last_usec = _timeval_diff(NULL, &TV0, &TV1);
    if (since_last_usec < 0)
        since_last_usec = 0;

    if (getenv("EPOLL_WAIT_HACK_DEBUG")) {
        fprintf(stderr, "since_last_usec=%ld\n", since_last_usec);
    }
    /* Policy: 'low' timeout allowance is proportional to the
     * last recorded time with timeout==0, ie TARGET - since_last_usec
     * (kinda poor man's TBF)
     */
    new_timeout_usec = _EPOLL_WAIT_TIMEOUT_TARGET * 1000 - since_last_usec;

    /* add a bit of fuzz to avoid syncronization, xor low bits against portions of TV1 */
    new_timeout_usec ^= (TV1.tv_usec & FUZZ_MASK) ^ (TV1.tv_usec >> 16 & FUZZ_MASK) ^ FUZZ_PID;

    /* if new_timeout_usec is lower than original, keep the latter
       in both cases, copy to new_timeout_tv if not NULL */
    if (new_timeout_usec <= timeout_usec) {
        if (new_timeout_tv) {
            *new_timeout_tv = *cur_timeout_tv;
        }
        new_timeout_usec = timeout_usec;
    } else {
        if (new_timeout_tv) {
            _timeval_from_usec(new_timeout_tv, new_timeout_usec);
        }
    }

    if (getenv("EPOLL_WAIT_HACK_DEBUG")) {
        fprintf(stderr, "*HACK* timeout0=%ld timeout1=%ld diff=%ld\n",
                timeout_usec,
                new_timeout_usec,
                new_timeout_usec - timeout_usec);
    }
    /* save for next since_last_usec calculation */
    TV0 = TV1;
    return new_timeout_usec;
}

/* HACK: provide "select" and "epoll_wait" syscalls, which'll get hacked
   timeouts for high rate of low-timeout calls, only for selected binary 
   (see HACK_ON) */

typedef int (*select_f_type)(
    int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout);

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    select_f_type orig_select;
    orig_select = (select_f_type)dlsym(RTLD_NEXT,"select");
    if (HACK_ON)
        _epoll_wait_hack_timeout(timeout, timeout);
    return orig_select(nfds, readfds, writefds, exceptfds, timeout);
}

typedef int (*epoll_wait_f_type)(
    int epfd, struct epoll_event *events, int maxevents, int timeout);

int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout_msec)
{
    epoll_wait_f_type orig_epoll_wait;
    orig_epoll_wait = (epoll_wait_f_type)dlsym(RTLD_NEXT,"epoll_wait");
    if (HACK_ON) {
        struct timeval timeout_tv;
        _timeval_from_usec(&timeout_tv, timeout_msec * 1000);
        timeout_msec = _epoll_wait_hack_timeout(&timeout_tv, NULL) / 1000;
    }
    return orig_epoll_wait(epfd, events, maxevents, timeout_msec);
}
/* vim: et si sw=4 ts=4 */
