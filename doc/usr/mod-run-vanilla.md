---
layout: documentation
title: Documentation - Basic SIIT Run
---

[Documentation](doc-index.html) > [Runs](doc-index.html#runs) > SIIT

# SIIT Run

## Index

1. [Introduction](#introduction)
2. [Sample Network](#sample-network)
3. [Jool](#jool)
4. [Testing](#testing)
5. [Stopping Jool](#stopping-jool)
6. [Further reading](#further-reading)

## Introduction

This document explains how to run Jool in [SIIT mode](intro-nat64.html#siit-traditional). Follow the link for more details on what to expect.

Software-wise, only a [successful install of Jool’s kernel module](mod-install.html) is required. The userspace application is out of the scope of this document on purpose.

In case you're wondering, you can follow along these tutorials using virtual machines or alternate interface types just fine (Jool is not married to physical "_ethX_" interfaces).

## Sample Network

You don't need all the nodes shown in the diagram to follow along; you can get away with only _A_, _T_ and _V_; the rest are very similar to _A_ and _V_ and are shown for illustrative purposes only.

![Figure 1 - Sample Network](images/network/vanilla.svg)

We will pretend I have address block 198.51.100.8/29 to distribute among my IPv6 nodes.

Jool requires _T_ to be Linux. The rest can be anything you want, so long as it implements the network protocol it's connected to. Also, you are free to configure the networks using any manager you want.

For the sake of simplicity however, the examples below assume every node is Linux and everything is being configured statically using the well-known `ip` command (and friends). Depending on your distro, your mileage might vary on how to get the network manager out of the way (assuming that's what you want). Just to clarify, the point of `service network-manager stop` is to claim control over your interface addresses and routes (otherwise the `ip` commands might be ineffectual).

Also to simplify, routing will be reduced to default all unknown traffic towards _T_. Note that there is nothing martian about anyone's configuration otherwise.

This is nodes _A_ through _E_:

{% highlight bash %}
user@A:~# service network-manager stop
user@A:~# /sbin/ip link set eth0 up
user@A:~# # Replace ".8" depending on which node you're on.
user@A:~# /sbin/ip addr add 2001:db8::198.51.100.8/120 dev eth0
user@A:~# /sbin/ip route add default via 2001:db8::198.51.100.1
{% endhighlight %}

Nodes _V_ through _Z_:

{% highlight bash %}
user@V:~# service network-manager stop
user@V:~# /sbin/ip link set eth0 up
user@V:~# # Replace ".16" depending on which node you're on.
user@V:~# /sbin/ip addr add 192.0.2.16/24 dev eth0
user@V:~# /sbin/ip route add default via 192.0.2.1
{% endhighlight %}

Node _T_:

{% highlight bash %}
user@T:~# service network-manager stop
user@T:~# 
user@T:~# /sbin/ip link set eth0 up
user@T:~# /sbin/ip addr add 2001:db8::198.51.100.1/120 dev eth0
user@T:~# 
user@T:~# /sbin/ip link set eth1 up
user@T:~# /sbin/ip addr add 192.0.2.1/24 dev eth1
{% endhighlight %}

Because we haven't turned _T_ into a translator yet, nodes _A_ through _E_ still cannot interact with _V_ through _Z_, but you might want to make sure _T_ can ping everyone before continuing.

Next, enable forwarding on _T_.

{% highlight bash %}
user@T:~# sysctl -w net.ipv4.conf.all.forwarding=1
user@T:~# sysctl -w net.ipv6.conf.all.forwarding=1
{% endhighlight %}

> If you omit these sysctls in kernels 3.5 and below, everything will seem to work, but Linux will drop some important ICMP traffic. Omitting the syscls in kernels 3.6 and above doesn't actually yield known adverse consequences.
> 
> Whether this inconsistency is a bug in older or newer kernels [is a rather philosophical topic](https://github.com/NICMx/NAT64/issues/170#issuecomment-141507174).
> 
> On the other hand, Jool 4.0 will almost certainly require forwarding, so you might as well start preparing your scripts.

Often you also need to [get rid of offloads in the translating machine](misc-offloading.html) by means of `ethtool`:

{% highlight bash %}
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

> If you need to turn offloads off and forego some of the above instructions, you will perceive extremey low performance.

## Jool

This is the insertion syntax:

	user@T:~# /sbin/modprobe jool_siit \
		[pool6=<IPv6 prefix>] \
		[blacklist=<IPv4 prefixes>] \
		[pool6791=<IPv4 prefixes>] \
		[disabled]

These are the arguments:

- `pool6` (short for "IPv6 pool") is the prefix the translation mechanism will be appending and removing from the addresses of the packets.  
This is optional because you might want to use the EAM table instead.
- `blacklist` represents IPv4 addresses Jool will **not** translate _using the pool6 prefix_ (ie. this does not affect EAMT translation).  
You can insert up to five comma-separated `blacklist` prefixes during a modprobe. If you need more, use the [userspace application](usr-flags-blacklist.html).
- `pool6791` is a secondary IPv4 pool used for something [slightly more cryptic](misc-rfc6791.html). You might rather want to read its explanation _after_ you've nailed the basics from this walkthrough.  
If this pool is empty, Jool will fall back to use this own node's natural source address towards the destination node.  
You can insert up to five comma-separated `pool6791` prefixes during a modprobe. If you need more, use the [userspace application](usr-flags-pool6791.html).
- `disabled` starts Jool inactive. If you're using the userspace application, you can use it to ensure you're done configuring before your traffic starts getting translated. The EAM walkthrough exemplifies its use.  
If not present, Jool starts translating traffic right away.

The following suffices for our sample network.

	user@T:~# /sbin/modprobe jool_siit pool6=2001:db8::/96

That means the IPv6 representation of any IPv4 address is going to be `2001:db8::<IPv4 address>`. See below for examples.

## Testing

If something doesn't work, try the [FAQ](misc-faq.html).

Try to ping _A_ from _V_ like this:

{% highlight bash %}
user@V:~$ ping 198.51.100.8
PING 198.51.100.8 (198.51.100.8) 56(84) bytes of data.
64 bytes from 198.51.100.8: icmp_seq=1 ttl=63 time=7.45 ms
64 bytes from 198.51.100.8: icmp_seq=2 ttl=63 time=1.64 ms
64 bytes from 198.51.100.8: icmp_seq=3 ttl=63 time=4.22 ms
64 bytes from 198.51.100.8: icmp_seq=4 ttl=63 time=2.32 ms
^C
--- 198.51.100.8 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3006ms
rtt min/avg/max/mdev = 1.649/3.914/7.450/2.249 ms
{% endhighlight %}

Then ping _V_ from _A_:

{% highlight bash %}
user@A:~$ ping6 2001:db8::192.0.2.16
PING 2001:db8::192.0.2.16(2001:db8::c000:210) 56 data bytes
64 bytes from 2001:db8::c000:210: icmp_seq=1 ttl=63 time=3.57 ms
64 bytes from 2001:db8::c000:210: icmp_seq=2 ttl=63 time=10.5 ms
64 bytes from 2001:db8::c000:210: icmp_seq=3 ttl=63 time=1.38 ms
64 bytes from 2001:db8::c000:210: icmp_seq=4 ttl=63 time=2.63 ms
^C
--- 2001:db8::192.0.2.16 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3003ms
rtt min/avg/max/mdev = 1.384/4.529/10.522/3.546 ms
{% endhighlight %}

How about hooking up a server in _X_ and access it from _D_:

![Figure 1 - IPv6 TCP from an IPv4 node](images/run-vanilla-firefox-4to6.png)

Then maybe another one in _C_ and request from _W_:

![Figure 2 - IPv4 TCP from an IPv6 node](images/run-vanilla-firefox-6to4.png)

## Stopping Jool

To shut down Jool, revert the modprobe using the `-r` flag:

{% highlight bash %}
user@T:~# /sbin/modprobe -r jool_siit
{% endhighlight %}

## Further reading

Here are some logical follow-ups if you want to read more:

- The [`pool6791` argument](usr-flags-pool6791.html) and its [gimmic](misc-rfc6791.html).
- Please consider the [MTU issues](misc-mtu.html) before releasing.
- If you care about EAM, head to the [second run](mod-run-eam.html).
- If you care about stateful NAT64, head to the [third run](mod-run-stateful.html).
- The [DNS64 document](op-dns64.html) will tell you how to make the prefix-address hack transparent to users.

