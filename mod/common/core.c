#include "nat64/mod/common/core.h"
#include "nat64/mod/common/packet.h"
#include "nat64/mod/common/stats.h"
#include "nat64/mod/common/types.h"
#include "nat64/mod/common/rfc6145/core.h"
#include "nat64/mod/common/send_packet.h"
#include "nat64/mod/common/config.h"

#ifdef STATEFUL
#include "nat64/mod/stateful/pool6.h"
#include "nat64/mod/stateful/pool4.h"
#include "nat64/mod/stateful/fragment_db.h"
#include "nat64/mod/stateful/determine_incoming_tuple.h"
#include "nat64/mod/stateful/filtering_and_updating.h"
#include "nat64/mod/stateful/compute_outgoing_tuple.h"
#include "nat64/mod/stateful/handling_hairpinning.h"
#else
#include "nat64/mod/stateless/pool4.h"
#include "nat64/mod/stateless/pool6.h"
#include "nat64/mod/stateless/eam.h"
#include "nat64/mod/stateless/rfc6791.h"
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/ipv6.h>


#ifdef STATEFUL

static unsigned int core_common(struct sk_buff *skb_in)
{
	struct sk_buff *skb_out;
	struct tuple tuple_in;
	struct tuple tuple_out;
	verdict result;

	result = determine_in_tuple(skb_in, &tuple_in);
	if (result != VER_CONTINUE)
		goto end;
	result = filtering_and_updating(skb_in, &tuple_in);
	if (result != VER_CONTINUE)
		goto end;
	result = compute_out_tuple(&tuple_in, &tuple_out, skb_in);
	if (result != VER_CONTINUE)
		goto end;
	result = translating_the_packet(&tuple_out, skb_in, &skb_out);
	if (result != VER_CONTINUE)
		goto end;

	if (is_hairpin(skb_out)) {
		result = handling_hairpinning(skb_out, &tuple_out);
		kfree_skb(skb_out);
	} else {
		result = sendpkt_send(skb_in, skb_out);
		/* send_pkt releases skb_out regardless of verdict. */
	}

	if (result != VER_CONTINUE)
		goto end;

	log_debug("Success.");
	/*
	 * The new packet was sent, so the original one can die; drop it.
	 *
	 * NF_DROP translates into an error (see nf_hook_slow()).
	 * Sending a replacing & translated version of the packet should not count as an error,
	 * so we free the incoming packet ourselves and return NF_STOLEN on success.
	 */
	kfree_skb(skb_in);
	result = VER_STOLEN;
	/* Fall through. */

end:
	return (unsigned int) result;
}

#else

static unsigned int core_common(struct sk_buff *skb_in)
{
	struct sk_buff *skb_out;
	verdict result;

	result = translating_the_packet(NULL, skb_in, &skb_out);
	if (result != VER_CONTINUE)
		goto end;
	result = sendpkt_send(skb_in, skb_out);
	if (result != VER_CONTINUE)
		goto end;

	log_debug("Success.");
	/* See the large comment above. */
	kfree_skb(skb_in);
	result = VER_STOLEN;
	/* Fall through. */

end:
	if (result == VER_ACCEPT) {
		log_debug("Returning the packet to the kernel.");
		skb_clear_cb(skb_in);
	}

	return (unsigned int) result;
}

#endif

unsigned int core_4to6(struct sk_buff *skb)
{
	struct iphdr *hdr = ip_hdr(skb);
	int error;

	if (config_get_is_disable())
		return NF_ACCEPT; /* Translation is disable; let the packet pass. */

#ifdef STATEFUL
	if (!pool4_contains(hdr->daddr) || pool6_is_empty())
		return NF_ACCEPT; /* Not meant for translation; let the kernel handle it. */
#else
	if (!pool4_contains(hdr->daddr) || (pool6_is_empty() && eamt_is_empty()) || rfc6791_is_empty())
		return NF_ACCEPT;
#endif

	log_debug("===============================================");
	log_debug("Catching IPv4 packet: %pI4->%pI4", &hdr->saddr, &hdr->daddr);

	error = skb_init_cb_ipv4(skb); /* Reminder: This function might change pointers. */
	if (error)
		return NF_DROP;

	error = validate_icmp4_csum(skb);
	if (error) {
		inc_stats(skb, IPSTATS_MIB_INHDRERRORS);
		skb_clear_cb(skb);
		return NF_DROP;
	}

	return core_common(skb);
}

unsigned int core_6to4(struct sk_buff *skb)
{
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	int error;

#ifdef STATEFUL
	verdict result;
#endif

	if (config_get_is_disable())
			return NF_ACCEPT; /* Translation is disable; let the packet pass. */

#ifdef STATEFUL
	if ((!pool6_contains(&hdr->daddr) || pool4_is_empty()))
		return NF_ACCEPT; /* Not meant for translation; let the kernel handle it. */
#else
	if (pool4_is_empty() || rfc6791_is_empty() ||
			!((eamt_contains_ipv6(&hdr->saddr) || pool6_contains(&hdr->saddr)) &&
			(eamt_contains_ipv6(&hdr->daddr) || pool6_contains(&hdr->daddr))))
		return NF_ACCEPT;
#endif

	log_debug("===============================================");
	log_debug("Catching IPv6 packet: %pI6c->%pI6c", &hdr->saddr, &hdr->daddr);

	error = skb_init_cb_ipv6(skb); /* Reminder: This function might change pointers. */
	if (error)
		return NF_DROP;

#ifdef STATEFUL
	result = fragdb_handle(&skb);
	if (result != VER_CONTINUE)
		return (unsigned int) result;
#endif

	error = validate_icmp6_csum(skb);
	if (error) {
		inc_stats(skb, IPSTATS_MIB_INHDRERRORS);
		skb_clear_cb(skb);
		return NF_DROP;
	}

	return core_common(skb);
}
