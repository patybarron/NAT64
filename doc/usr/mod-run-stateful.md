---
layout: documentation
title: Documentation - Stateful NAT64 Run
---

[Documentation](doc-index.html) > [Runs](doc-index.html#runs) > Stateful NAT64

# Stateful Run

## Index

1. [Introduction](#introduction)
2. [Sample Network](#sample-network)
3. [Jool](#jool)
4. [Testing](#testing)
5. [Stopping Jool](#stopping-jool)
6. [Further reading](#further-reading)

## Introduction

This document explains how to run Jool in [Stateful NAT64 mode](intro-nat64.html#stateful-nat64).

Software-wise, only a [successful install of the kernel module](mod-install.html) is required. The userspace application is not needed in this basic run.

## Sample Network

![Figure 1 - Sample Network](images/network/stateful.svg)

All the remarks in the first document's [Sample Network section](mod-run-vanilla.html#sample-network) apply here.

Nodes _A_ through _E_:

{% highlight bash %}
user@A:~# service network-manager stop
user@A:~# /sbin/ip link set eth0 up
user@A:~# # Replace "::8" depending on which node you're on.
user@A:~# /sbin/ip address add 2001:db8::8/96 dev eth0
user@A:~# /sbin/ip route add default via 2001:db8::1
{% endhighlight %}

Nodes _V_ through _Z_:

{% highlight bash %}
user@V:~# service network-manager stop
user@V:~# /sbin/ip link set eth0 up
user@V:~# # Replace ".16" depending on which node you're on.
user@V:~# /sbin/ip address add 203.0.113.16/24 dev eth0
{% endhighlight %}

Notice these nodes do not need a default route. This is a consequence of them being in the same network as the NAT64; 203.0.113.2 will be masking the IPv6 nodes, so _V_ through _Z_ think they're talking directly with _T_.

Node _T_:

{% highlight bash %}
user@T:~# service network-manager stop
user@T:~# 
user@T:~# /sbin/ip link set eth0 up
user@T:~# /sbin/ip address add 2001:db8::1/96 dev eth0
user@T:~# 
user@T:~# /sbin/ip link set eth1 up
user@T:~# /sbin/ip address add 203.0.113.1/24 dev eth1
user@T:~# /sbin/ip address add 203.0.113.2/24 dev eth1
user@T:~# 
user@T:~# sysctl -w net.ipv4.conf.all.forwarding=1
user@T:~# sysctl -w net.ipv6.conf.all.forwarding=1
user@T:~# ethtool --offload eth0 tso off
user@T:~# ethtool --offload eth0 ufo off
user@T:~# ethtool --offload eth0 gso off
user@T:~# ethtool --offload eth0 gro off
user@T:~# ethtool --offload eth0 lro off
user@T:~# ethtool --offload eth1 tso off
user@T:~# ethtool --offload eth1 ufo off
user@T:~# ethtool --offload eth1 gso off
user@T:~# ethtool --offload eth1 gro off
user@T:~# ethtool --offload eth1 lro off
{% endhighlight %}

Stateful mode is special in that the NAT64 needs at least two separate IPv4 addresses:

- One or more addresses used for local traffic (ie. to and from _T_). In the configuration above, this is 203.0.113.1.
- One or more addresses used for NAT64 translation. Linux needs to be aware of these because it needs to ARP reply them. This one is 203.0.113.2.

The need for this separation _is a Jool quirk_ and we're still researching ways to remove it.

The translation addresses need less priority so _T_ doesn't use them for local traffic by accident. One way to achieve this is to simply add the NAT64 addresses after the node addresses.

Remember you might want to cross-ping _T_ vs everything before continuing.

## Jool

This is the insertion syntax:

	user@T:~# /sbin/modprobe jool \
		[pool6=<IPv6 prefix>] \
		[pool4=<IPv4 prefixes>] \
		[disabled]

- `pool6` has the same meaning as in SIIT Jool.
- `pool4` is the subset of the node's addresses which will be used for translation (the prefix length defaults to /32).
- `disabled` has the same meaning as in SIIT Jool.

EAM and `pool6791` do not make sense in stateful mode, and as such are unavailable.

The result looks like this:

	user@T:~# /sbin/modprobe jool pool6=64:ff9b::/96 pool4=203.0.113.2

Jool will listen on address `203.0.113.2` and append and remove prefix `64:ff9b::/96`.

## Testing

If something doesn't work, try the [FAQ](misc-faq.html).

Test by sending requests from the IPv6 network:

{% highlight bash %}
user@C:~$ ping6 64:ff9b::203.0.113.16
PING 64:ff9b::192.0.2.16(64:ff9b::c000:210) 56 data bytes
64 bytes from 64:ff9b::cb00:7110: icmp_seq=1 ttl=63 time=1.13 ms
64 bytes from 64:ff9b::cb00:7110: icmp_seq=2 ttl=63 time=4.48 ms
64 bytes from 64:ff9b::cb00:7110: icmp_seq=3 ttl=63 time=15.6 ms
64 bytes from 64:ff9b::cb00:7110: icmp_seq=4 ttl=63 time=4.89 ms
^C
--- 64:ff9b::203.0.113.16 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3004ms
rtt min/avg/max/mdev = 1.136/6.528/15.603/5.438 ms
{% endhighlight %}

![Figure 1 - IPv4 TCP from an IPv6 node](images/run-stateful-firefox-4to6.png)

See the further reading below to see how to enable IPv4 nodes to start communication.

## Stopping Jool

To shut down Jool, revert the modprobe using the `-r` flag:

{% highlight bash %}
user@T:~# /sbin/modprobe -r jool
{% endhighlight %}

## Further Reading

1. An IPv4 "outside" node cannot start communication because it "sees" the IPv6 network as an IPv4 private network behind a NAT. To remedy this, Jool enables you to configure "port forwarding". See [here](op-static-bindings.html) if you're interested.
2. There's a discussion on the [IPv4 pool](op-pool4.html).
3. The [DNS64 document](op-dns64.html) will tell you how to make the prefix-address-hack transparent to users.
4. Please consider the [MTU issues](misc-mtu.html) before releasing.
5. There's also an [alternate stateful run](mod-run-alternate.html). Perhaps it can help you see things from a better perspective.

