See https://bugs.launchpad.net/bugs/1518430
"liberty: ~busy loop on epoll_wait being called with zero timeout"

So, until someone fixes this CPU toaster, hack epoll_wait() and select()
to limit the rate of calls with zero timeouts

It's an horrible hack, WfM, YMMV, please-dont-ask.

I did:

    make build
    sudo make apply-host
    sudo make apply-lxcs
    sudo make show-applied
