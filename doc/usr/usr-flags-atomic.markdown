---
layout: documentation
title: Documentation - Flags > Atomic Fragments
---

[Documentation](doc-index.html) > [Userspace Application](doc-index.html#userspace-application) > [Flags](usr-flags.html) > [\--global](usr-flags-global.html) > Atomic Fragments

# Atomic Fragments

## Index

1. [Overview](#overview)
2. [Flags](#flags)
	1. [`--allow-atomic-fragments`](#atomicfragments)
	2. [`--setDF`](#setdf)
	3. [`--genFH`](#genfh)
	4. [`--genID`](#genid)
	5. [`--boostMTU`](#boostmtu)

## Overview

"Atomic fragments" are IPv6 packets which are not fragmented but still contain a (redundant) [Fragment Header](https://tools.ietf.org/html/rfc2460#section-4.5). They are a hack in the NAT64 specification that intends to leverage the difference between the IPv4 minimum MTU (68) and the IPv6 minimum MTU (1280).

Atomic fragments are known to have [security implications](https://tools.ietf.org/html/rfc6946) and there is [official ongoing effort to deprecate them](https://tools.ietf.org/html/draft-ietf-6man-deprecate-atomfrag-generation-00). Even RFC 6145 (ie. SIIT's core document) warns about [issues regarding the hack](http://tools.ietf.org/html/rfc6145#section-6).

From Jool's perspective, there are also technical drawbacks to allowing atomic fragments. The Linux kernel is particularly lacking when it comes to recognizing redundant fragment headers, so if Jool is generating one, Linux might fragment the packet in a funny way:

[![Figure 1 - what could possibly go wrong?](images/atomic-double-frag.png)](obj/atomic-double-frag.pcapng)

(Jool 3.2 and below used to avoid this by not deferring fragmentation to the kernel, but this introduced other-subtler issues.)

As a consequence, Jool 3.3's default configuration **disables** atomic fragments. You should most likely **never** change this. The options described later in this document all have to do with atomic fragments and are now considerered **deprecated**. In fact, we intend to wipe them out as soon as (and if) [draft-ietf-6man-deprecate-atomfrag-generation](https://tools.ietf.org/html/draft-ietf-6man-deprecate-atomfrag-generation-00) is upgraded to RFC status.

Let it be known that we fully condone the deprecation of atomic fragments.

## Flags

### `--allow-atomic-fragments`

- Type: Boolean
- Default: OFF
- Modes: Both (SIIT and Stateful)
- Translation direction: Both (IPv4 to IPv6 and IPv6 to IPv4)
- Source: [RFC 6145, mainly section 6](http://tools.ietf.org/html/rfc6145#section-6). Being deprecated at [deprecate-atomfrag-generation](https://tools.ietf.org/html/draft-ietf-6man-deprecate-atomfrag-generation-00).

This is a short version of all the following flags.

This:

{% highlight bash %}
$(jool) --allow-atomic-fragments true
{% endhighlight %}

is the same as

{% highlight bash %}
$(jool) --setDF true
$(jool) --genFH true
$(jool) --genID false
$(jool) --boostMTU false
{% endhighlight %}

This is the default behaviour requested by [RFC 6145](http://tools.ietf.org/html/rfc6145), and the IETF is hopefully going to deprecate it in the future. It is _not_ Jool's default and we do _not_ recommend it.

Also this:

{% highlight bash %}
$(jool) --allow-atomic-fragments false
{% endhighlight %}

is the same as

{% highlight bash %}
$(jool) --setDF false
$(jool) --genFH false
$(jool) --genID true
$(jool) --boostMTU true
{% endhighlight %}

This is an alternate mode defined both by RFC 6145 and [draft-ietf-6man-deprecate-atomfrag-generation](https://tools.ietf.org/html/draft-ietf-6man-deprecate-atomfrag-generation-00). The latter mandates this behaviour and is Jool 3.3's default.

Also:

The separation of the four flags exists for historic reasons only; our interpretation of the RFC used to be wrong. You should probably never manage them individually. It doesn't make sense to set `--setDF` as false but `--setFH` as true, for example. The relationship between `--setDF` and `--boostMTU` is also particularly delicate; see below for details.

### `--setDF`

- Name: DF flag always on
- Type: Boolean
- Default: OFF
- Modes: Both (SIIT and Stateful)
- Translation direction: IPv6 to IPv4

The logic is best described in pseudocode form:

		If the incoming packet has a fragment header:
			the outgoing packet's DF flag will be false.
		otherwise:
			if --setDF is true
				the outgoing packet's DF flag will be true.
			otherwise:
				if outgoing packet's length > 1260
					the outgoing packet's DF flag will be true.
				otherwise:
					the outgoing packet's DF flag will be false.

<a href="http://tools.ietf.org/html/rfc6145#section-6" target="_blank">Section 6 of RFC 6145</a> describes the rationale.

Also see [`--boostMTU`](#boostmtu) for an important gotcha.

### `--genFH`

- Name: Generate IPv6 Fragment Header
- Type: Boolean
- Default: OFF
- Modes: Both (SIIT and Stateful)
- Translation direction: IPv4 to IPv6

If this is ON, Jool will always generate an "IPv6 Fragment Header" if the incoming IPv4 Packet does not set the DF flag.

If this is OFF, then Jool will not generate the "IPv6 Fragment Header" whether the Flag of the incoming IPv4 Packet is set or not set, unless the incoming packet is a fragment, the "IPv6 Fragment Header" will be generated.

This is the flag that causes Linux to flip out when it needs to fragment. It's broken, so activate at your own risk.

### `--genID`

- Name: Generate IPv4 identification
- Type: Boolean
- Default: ON
- Modes: Both (SIIT and Stateful)
- Translation direction: IPv6 to IPv4

All IPv4 packets contain an Identification field. IPv6 packets only contain an Identification field if they have a Fragment header.

If the incoming IPv6 packet has a fragment header, the <a href="http://en.wikipedia.org/wiki/IPv4#Header" target="_blank">IPv4 header</a>'s Identification field is _always_ copied from the low-order bits of the IPv6 fragment header's Identification value.

Otherwise:

- If `--genID` is OFF, the IPv4 header's Identification fields are set to zero.
- If `--genID` is ON, the IPv4 headers' Identification fields are set randomly.

### `--boostMTU`

- Name: Decrease MTU failure rate
- Type: Boolean
- Default: ON
- Modes: Both (SIIT and Stateful)
- Translation direction: IPv4 to IPv6 (ICMP errors only)

When a packet is too big for a link's MTU, routers generate <a href="http://tools.ietf.org/html/rfc4443#section-3.2" target="_blank">Packet too Big</a> ICMP errors on IPv6 and <a href="http://tools.ietf.org/html/rfc792" target="_blank">Fragmentation Needed</a> ICMP errors on IPv4. These error types are roughly equivalent, so Jool translates _Packet too Bigs_ into _Fragmentation Neededs_ and vice-versa.

These ICMP errors are supposed to contain the offending MTU so the emitter can resize and resend its packets accordingly.

The minimum MTU for IPv6 is 1280. The minimum MTU for IPv4 is 68. Therefore, Jool can find itself wanting to report an illegal MTU while translating a _Fragmentation Needed_ (v4) into a _Packet too Big_ (v6).

- If `--boostMTU` is ON, the minimum IPv6 MTU Jool will ever report is 1280.
- If `--boostMTU` is OFF, Jool will not try to mangle MTUs.

In reality, Jool still has to mangle the MTU values to account for the difference between the IPv4 header's basic length (20) and the IPv6 header's (40). An IPv6 packet can be 20 bytes larger than the IPv4 MTU because it's going to lose 20 bytes when its IPv6 header is replaced by an IPv4 header.

Here's the full algorithm:

		IPv6_error.MTU = IPv4_error.MTU + 20
		if --boostMTU == true AND IPv6_error.MTU < 1280
			IPv6_error.MTU = 1280

<a href="http://tools.ietf.org/html/rfc6145#section-6" target="_blank">Section 6 of RFC 6145</a> describes the rationale.

Notice, if `--setDF` and `--boostMTU` are both ON and there's an IPv4 link with MTU &lt; 1260, you have an endless loop similar to the [MTU hassle](misc-mtu.html):

1. IPv6 sender transmits an IPv6 packet sized 1280.
2. Jool translates it into an IPv4 packet sized 1260 with DF=1.
3. IPv4 router with outbound interface with MTU &lt; 1260 generates _ICMPv6 Frag Needed_ with MTU=1000 (or whatever).
4. Jool translates it to ICMPv6 _Packet Too Big_ with MTU=1280.
5. Goto 1.

Thanks to Tore Anderson for noticing (and mostly writing) this quirk.
