all: build

build: epoll_hack.so

test: build
	strace -e epoll_wait,select -f env EPOLL_WAIT_HACK_DEBUG=1 LD_PRELOAD=$$PWD/epoll_hack.so python epoll_ex.py

%.so: %.c
	gcc -shared -fPIC $< -o $(@) -ldl

must-be-root:
	[ $(shell id -u) = 0 ]

install: must-be-root build
	install epoll_hack.so $(ROOT)/usr/local/lib
    
uninstall: must-be-root
	rm -fv $(ROOT)/usr/local/lib/epoll_hack.so

os_upstart = keystone neutron-metadata-agent neutron-vpn-agent neutron-metering-agent neutron-lbaas-agent neutron-ns-metadata-proxy neutron-openvswitch-agent nova-api-os-compute nova-api-metadata nova-conductor nova-novncproxy nova-scheduler nova-cert nova-consoleauth glance-api glance-registry cinder-api cinder-scheduler cinder-volume neutron-server heat-api heat-engine heat-api-cfn nova-compute swift-proxy neutron-dhcp-agent

EDIT_Y=for f in $(os_upstart); do \
		test -f /etc/init/$$f.conf || continue; \
		upstart=/etc/init/$$f.conf; \
		test -x /usr/local/lib/epoll_hack.so || exit 1; \
		grep LD_PRELOAD= $$upstart && continue; \
		echo $$f ;\
		sed -i -r "s,--exec (/usr/bin/)(\S+) -- ,--name \2 --exec /usr/bin/env -- LD_PRELOAD=/usr/local/lib/epoll_hack.so \1\2 ," $$upstart; \
		sed -i -r "s,exec (/usr/bin/swift-init) ,exec /usr/bin/env LD_PRELOAD=/usr/local/lib/epoll_hack.so \1 ," $$upstart; \
		stop $$f; pkill -f neutron-ns-metadata-proxy; test -f /etc/init/$$f.override || start $$f; done; true

EDIT_N=for f in $(os_upstart); do \
		test -f /etc/init/$$f.conf || continue; \
		upstart=/etc/init/$$f.conf; \
		grep LD_PRELOAD= $$upstart || continue; \
		echo $$f ;\
		sed -i -r "s,--name \S+ --exec /usr/bin/env -- LD_PRELOAD=/usr/local/lib/epoll_hack.so (\S+) ,--exec \1 -- ," $$upstart; \
		sed -i -r "s,exec /usr/bin/env LD_PRELOAD=/usr/local/lib/epoll_hack.so ,exec ," $$upstart; \
		stop $$f; test -f /etc/init/$$f.override || start $$f; done; true


empty:=
space:= $(empty) $(empty)
comma:= ,
os_upstart_patt = {$(subst $(space),$(comma),$(os_upstart))}
SHELL=bash

apply-lxcs: must-be-root
	lxc_svc=$$(ls -1d /var/lib/lxc/*/rootfs/etc/init/$(os_upstart_patt).conf 2>/dev/null | sed -r 's,/var/lib/lxc/([^/]+)/rootfs/etc/init/(.+).conf,\1 \2,') ;\
		echo "$$lxc_svc" | while read lxc service;do echo make install ROOT=/var/lib/lxc/$$lxc/rootfs; done | sort -u | bash -x ;\
		echo "$$lxc_svc" | while read lxc service;do echo '$(EDIT_Y)'|lxc-attach -n $$lxc -- bash ;done

unapply-lxcs: must-be-root
	echo $(os_upstart_patt)
	lxc_svc=$$(ls -1d /var/lib/lxc/*/rootfs/etc/init/$(os_upstart_patt).conf 2>/dev/null | sed -r 's,/var/lib/lxc/([^/]+)/rootfs/etc/init/(.+).conf,\1 \2,') ;\
		echo "$$lxc_svc" | while read lxc service;do echo make uninstall ROOT=/var/lib/lxc/$$lxc/rootfs; done | sort -u | bash -x ;\
		echo "$$lxc_svc" | while read lxc service;do echo '$(EDIT_N)'|lxc-attach -n $$lxc -- bash ;done

apply-host: must-be-root
	make install ROOT=/
	echo '$(EDIT_Y)'| bash -x

unapply-host: must-be-root
	make install ROOT=/
	echo '$(EDIT_N)'| bash -x

show-applied: must-be-root
	grep -l epoll_hack /proc/*/maps|egrep -o '[0-9]+'|xargs ps -opid,cgroup:80,start_time,args

clean:
	rm -f epoll_hack.so
