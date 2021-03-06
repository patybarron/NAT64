#ifndef _JOOL_MOD_SEND_PACKET_H
#define _JOOL_MOD_SEND_PACKET_H

/**
 * @file
 * Functions to artificially send homemade packets through the interfaces. Basically, you initialize
 * sk_buffs and this worries about putting them on the network.
 *
 * We need this because the kernel assumes that when a packet enters a module, a packet featuring
 * the same layer-3 protocol exits the module. So we can't just morph IPv4 packets into IPv6 ones
 * and vice-versa; we need to ask the kernel to drop the original packets and send new ones on our
 * own.
 *
 * @author Alberto Leiva
 */

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>

#include "nat64/mod/common/types.h"
#include "nat64/mod/common/packet.h"

#define NF_IP_PRI_JOOL (NF_IP_PRI_NAT_DST + 25)
#define NF_IP6_PRI_JOOL (NF_IP6_PRI_NAT_DST + 25)


/**
 * Puts "out_skb" on the network.
 * You need to have routed out_skb first (see route.h).
 *
 * Note that this function inherits from ip_local_out() and ip6_local_out() the annoying side
 * effect of freeing "out_skb", EVEN IF IT COULD NOT BE SENT.
 *
 * "in_skb" is used to hack fragmentation needed ICMP errors if necessary.
 */
verdict sendpkt_send(struct packet *in, struct packet *out);


#endif /* _JOOL_MOD_SEND_PACKET_H */
