---
layout: documentation
title: Documentation - Userspace Applications Installation
---

[Documentation](doc-index.html) > [Installation](doc-index.html#installation) > Userspace Application

# Userspace Applications Installation

## Introduction

Jool is four things:

1. Two <a href="https://en.wikipedia.org/wiki/Loadable_kernel_module" target="_blank">kernel modules</a> you can hook up to Linux. One of them is the SIIT implementation and the other one is the Stateful NAT64. They have their own [installation document](mod-install.html).
2. Two <a href="https://en.wikipedia.org/wiki/User_space" target="_blank">userspace</a> applications which can be used to configure each module.

This document explains how to obtain the binaries of the userspace application.

## If you downloaded the [official release](download.html)

{% highlight bash %}
user@node:~/Jool$ cd usr
user@node:~/Jool/usr$ ./configure # You need libnl-3 to run this; see below.
user@node:~/Jool/usr$ make
user@node:~/Jool/usr# make install
{% endhighlight %}

Done; you should now be able to type `jool --help` or `jool_siit --help` and get some garbage. Go to [Flags](usr-flags.html) for more verbose documentation.

## If you downloaded from the <a href="https://github.com/NICMx/NAT64" target="_blank">Github repository</a>

The repository does not keep track of the configuration script, so you have to generate it yourself. You need autoconf 2.68 or superior to do that.

{% highlight bash %}
user@node:~# apt-get install autoconf
{% endhighlight %}

Then just add a call to `autogen.sh` to the normal installation procedure:

{% highlight bash %}
Jool$ cd usr
Jool/usr$ ./autogen.sh # You need autoconf 2.68 or superior to run this.
Jool/usr$ ./configure # You need libnl-3 to run this; see below.
Jool/usr$ make
Jool/usr# make install
{% endhighlight %}

Done; you should now be able to type `jool --help` or `jool_siit --help` and get some garbage. Go to [Flags](usr-flags.html) for more verbose documentation.

## libnl-3

<a href="http://www.carisma.slowglass.com/~tgr/libnl/" target="_blank">This</a> is libnl-3's official website is as of 2014-07-31, in case you want to compile it yourself.

If your distribution package-manages it though, <a href="https://github.com/NICMx/NAT64/issues/103" target="_blank">you might really want to exploit the feature rather than compiling the framework</a>:

{% highlight bash %}
user@node:~# apt-get install libnl-3-dev
{% endhighlight %}

