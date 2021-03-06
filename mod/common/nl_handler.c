#include "nat64/mod/common/nl_handler.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/version.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

#include "nat64/common/config.h"
#include "nat64/common/constants.h"
#include "nat64/mod/common/config.h"
#include "nat64/mod/common/nl_buffer.h"
#include "nat64/mod/common/pool6.h"
#include "nat64/mod/common/types.h"
#include "nat64/mod/stateful/bib_db.h"
#include "nat64/mod/stateful/session_db.h"
#include "nat64/mod/stateful/static_routes.h"
#ifdef STATEFUL
	#include "nat64/mod/stateful/pool4.h"
#else
	#include "nat64/mod/stateless/pool4.h"
#endif
#include "nat64/mod/stateless/rfc6791.h"
#include "nat64/mod/stateless/eam.h"
#ifdef BENCHMARK
#include "nat64/mod/common/log_time.h"
#endif



/**
 * Socket the userspace application will speak to.
 */
static struct sock *nl_socket;

/**
 * A lock, used to avoid sync issues when receiving messages from userspace.
 */
static DEFINE_MUTEX(config_mutex);


/**
 * Use this when data_len is known to be smaller than NLBUFFER_SIZE. When this might not be the
 * case, use the netlink buffer instead (nl_buffer.h).
 */
static int respond_single_msg(struct nlmsghdr *nl_hdr_in, int type, void *payload, int payload_len)
{
	struct sk_buff *skb_out;
	struct nlmsghdr *nl_hdr_out;
	int res;

	skb_out = nlmsg_new(NLMSG_ALIGN(payload_len), GFP_ATOMIC);
	if (!skb_out) {
		log_err("Failed to allocate a response skb to the user.");
		return -ENOMEM;
	}

	nl_hdr_out = nlmsg_put(skb_out,
			0, /* src_pid (0 = kernel) */
			nl_hdr_in->nlmsg_seq, /* seq */
			type, /* type */
			payload_len, /* payload len */
			0); /* flags */
	memcpy(nlmsg_data(nl_hdr_out), payload, payload_len);
	/* NETLINK_CB(skb_out).dst_group = 0; */

	res = nlmsg_unicast(nl_socket, skb_out, nl_hdr_in->nlmsg_pid);
	if (res < 0) {
		log_err("Error code %d while returning response to the user.", res);
		return res;
	}

	return 0;
}

static int respond_setcfg(struct nlmsghdr *nl_hdr_in, void *payload, int payload_len)
{
	return respond_single_msg(nl_hdr_in, MSG_SETCFG, payload, payload_len);
}

/**
 * @note "ACK messages also use the message type NLMSG_ERROR and payload format but the error code
 * is set to 0." (http://www.infradead.org/~tgr/libnl/doc/core.html#core_msg_ack).
 */
static int respond_error(struct nlmsghdr *nl_hdr_in, int error)
{
	struct nlmsgerr payload = { abs(error), *nl_hdr_in };
	return respond_single_msg(nl_hdr_in, NLMSG_ERROR, &payload, sizeof(payload));
}

/*
static int respond_ack(struct nlmsghdr *nl_hdr_in)
{
	return respond_error(nl_hdr_in, 0);
}
*/

static int verify_superpriv(void)
{
	if (!capable(CAP_NET_ADMIN)) {
		log_err("Administrative privileges required.");
		return -EPERM;
	}

	return 0;
}

static int validate_version(struct request_hdr *hdr)
{
	if (hdr->magic[0] != 'j' || hdr->magic[1] != 'o')
		goto magic_fail;
	if (hdr->magic[2] != 'o' || hdr->magic[3] != 'l')
		goto magic_fail;

	switch (hdr->type) {
	case 's':
		if (nat64_is_stateful()) {
			log_err("You're speaking to NAT64 Jool using "
					"the SIIT Jool application.");
			return -EINVAL;
		}
		break;
	case 'n':
		if (nat64_is_stateless()) {
			log_err("You're speaking to SIIT Jool using "
					"the NAT64 Jool application.");
			return -EINVAL;
		}
		break;
	default:
		goto magic_fail;
	}

	if (jool_version() == hdr->version)
		return 0;

	log_err("Version mismatch. The kernel module is %u.%u.%u.%u, "
			"but the userspace application is %u.%u.%u.%u. "
			"Please update Jool's %s.",
			JOOL_VERSION_MAJOR, JOOL_VERSION_MINOR,
			JOOL_VERSION_REV, JOOL_VERSION_DEV,
			hdr->version >> 24, (hdr->version >> 16) & 0xFFU,
			(hdr->version >> 8) & 0xFFU, hdr->version & 0xFFU,
			(jool_version() > hdr->version)
				? "userspace application"
				: "kernel module");
	return -EINVAL;

magic_fail:
	log_err("It appears you're trying to speak to Jool using some other "
			"Netlink client or an older userspace application. "
			"If the latter is true, please update your userspace "
			"application.");
	return -EINVAL;
}

static int pool6_entry_to_userspace(struct ipv6_prefix *prefix, void *arg)
{
	struct nl_buffer *buffer = (struct nl_buffer *) arg;
	return nlbuffer_write(buffer, prefix, sizeof(*prefix));
}

static int handle_pool6_display(struct nlmsghdr *nl_hdr, union request_pool6 *request)
{
	struct nl_buffer *buffer;
	struct ipv6_prefix *prefix;
	int error;

	log_debug("Sending IPv6 pool to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	prefix = request->display.prefix_set ? &request->display.prefix : NULL;
	error = pool6_for_each(pool6_entry_to_userspace, buffer, prefix);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_pool6_config(struct nlmsghdr *nl_hdr, struct request_hdr *jool_hdr,
		union request_pool6 *request)
{
	__u64 count;
	int error;

	switch (jool_hdr->operation) {
	case OP_DISPLAY:
		return handle_pool6_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning IPv6 prefix count.");
		error = pool6_count(&count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	case OP_ADD:
	case OP_UPDATE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Adding a prefix to the IPv6 pool.");

		return respond_error(nl_hdr, pool6_add(&request->add.prefix));

	case OP_REMOVE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Removing a prefix from the IPv6 pool.");
		error = pool6_remove(&request->remove.prefix);
		if (error)
			return respond_error(nl_hdr, error);

		if (nat64_is_stateful() && !request->flush.quick)
			error = sessiondb_delete_by_prefix6(&request->remove.prefix);

		return respond_error(nl_hdr, error);

	case OP_FLUSH:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Flushing the IPv6 pool...");
		error = pool6_flush();
		if (error)
			return respond_error(nl_hdr, error);

		if (nat64_is_stateful() && !request->flush.quick)
			error = sessiondb_flush();

		return respond_error(nl_hdr, error);

	default:
		log_err("Unknown operation: %d", jool_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

static int pool4_to_usr(struct ipv4_prefix *prefix, void *arg)
{
	return nlbuffer_write(arg, prefix, sizeof(*prefix));
}

static int handle_pool4_display(struct nlmsghdr *nl_hdr, union request_pool4 *request)
{
	struct nl_buffer *buffer;
	struct ipv4_prefix *prefix;
	int error;

	log_debug("Sending IPv4 pool to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	prefix = request->display.prefix_set ? &request->display.prefix : NULL;
	error = pool4_for_each(pool4_to_usr, buffer, prefix);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_pool4_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		union request_pool4 *request)
{
	__u64 count;
	int error;

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		return handle_pool4_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning IPv4 address count.");
		error = pool4_count(&count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	case OP_ADD:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Adding an address to the IPv4 pool.");
		return respond_error(nl_hdr, pool4_add(&request->add.addrs));

	case OP_REMOVE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Removing an address from the IPv4 pool.");

		error = pool4_remove(&request->remove.addrs);
		if (error)
			return respond_error(nl_hdr, error);

		if (nat64_is_stateful() && !request->remove.quick) {
			error = sessiondb_delete_by_prefix4(&request->remove.addrs);
			if (error)
				return respond_error(nl_hdr, error);
			error = bibdb_delete_by_prefix4(&request->remove.addrs);
		}

		return respond_error(nl_hdr, error);

	case OP_FLUSH:
		if (verify_superpriv()) {
			return respond_error(nl_hdr, -EPERM);
		}

		log_debug("Flushing the IPv4 pool...");
		error = pool4_flush();
		if (error)
			return respond_error(nl_hdr, error);

		if (nat64_is_stateful() && !request->flush.quick) {
			error = sessiondb_flush();
			if (error)
				return respond_error(nl_hdr, error);
			error = bibdb_flush();
		}

		return respond_error(nl_hdr, error);

	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

static int bib_entry_to_userspace(struct bib_entry *entry, void *arg)
{
	struct nl_buffer *buffer = (struct nl_buffer *) arg;
	struct bib_entry_usr entry_usr;

	entry_usr.addr4 = entry->ipv4;
	entry_usr.addr6 = entry->ipv6;
	entry_usr.is_static = entry->is_static;

	return nlbuffer_write(buffer, &entry_usr, sizeof(entry_usr));
}

static int handle_bib_display(struct nlmsghdr *nl_hdr, struct request_bib *request)
{
	struct nl_buffer *buffer;
	struct ipv4_transport_addr *addr4;
	int error;

	log_debug("Sending BIB to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	addr4 = request->display.addr4_set ? &request->display.addr4 : NULL;
	error = bibdb_iterate_by_ipv4(request->l4_proto, bib_entry_to_userspace, buffer, addr4);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_bib_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		struct request_bib *request)
{
	__u64 count;
	int error;

	if (nat64_is_stateless()) {
		log_err("SIIT doesn't have BIBs.");
		return -EINVAL;
	}

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		return handle_bib_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning BIB count.");
		error = bibdb_count(request->l4_proto, &count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	case OP_ADD:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Adding BIB entry.");
		return respond_error(nl_hdr, add_static_route(request));

	case OP_REMOVE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Removing BIB entry.");
		return respond_error(nl_hdr, delete_static_route(request));

	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

static int session_entry_to_userspace(struct session_entry *entry, void *arg)
{
	struct nl_buffer *buffer = (struct nl_buffer *) arg;
	struct session_entry_usr entry_usr;
	unsigned long dying_time;
	int error;

	error = sessiondb_get_timeout(entry, &dying_time);
	if (error)
		return error;
	dying_time += entry->update_time;

	entry_usr.remote6 = entry->remote6;
	entry_usr.local6 = entry->local6;
	entry_usr.local4 = entry->local4;
	entry_usr.remote4 = entry->remote4;
	entry_usr.state = entry->state;
	entry_usr.dying_time = (dying_time > jiffies) ? jiffies_to_msecs(dying_time - jiffies) : 0;

	return nlbuffer_write(buffer, &entry_usr, sizeof(entry_usr));
}

static int handle_session_display(struct nlmsghdr *nl_hdr, struct request_session *request)
{
	struct nl_buffer *buffer;
	struct ipv4_transport_addr *remote4 = NULL;
	struct ipv4_transport_addr *local4 = NULL;
	int error;

	log_debug("Sending session table to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	if (request->display.connection_set) {
		remote4 = &request->display.remote4;
		local4 = &request->display.local4;
	}
	error = sessiondb_iterate_by_ipv4(request->l4_proto, session_entry_to_userspace, buffer,
			remote4, local4);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_session_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		struct request_session *request)
{
	__u64 count;
	int error;

	if (nat64_is_stateless()) {
		log_err("SIIT doesn't have session tables.");
		return -EINVAL;
	}

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		return handle_session_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning session count.");
		error = sessiondb_count(request->l4_proto, &count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

static int eam_entry_to_userspace(struct eam_entry *entry, void *arg)
{
	struct nl_buffer *buffer = (struct nl_buffer *) arg;
	struct eam_entry_usr entry_usr;

	entry_usr.pref4 = entry->pref4;
	entry_usr.pref6 = entry->pref6;

	return nlbuffer_write(buffer, &entry_usr, sizeof(entry_usr));
}

static int handle_eamt_display(struct nlmsghdr *nl_hdr, union request_eamt *request)
{
	struct nl_buffer *buffer;
	struct ipv4_prefix *prefix4;
	int error;

	log_debug("Sending EAMT to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	prefix4 = request->display.prefix4_set ? &request->display.prefix4 : NULL;
	error = eamt_for_each(eam_entry_to_userspace, buffer, prefix4);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_eamt_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		union request_eamt *request)
{
	__u64 count;
	int error;

	if (nat64_is_stateful()) {
		log_err("Stateful NAT64 doesn't have an EAMT.");
		return -EINVAL;
	}

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		return handle_eamt_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning EAMT count.");
		error = eamt_count(&count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	case OP_ADD:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Adding EAMT entry.");
		return respond_error(nl_hdr, eamt_add(&request->add.prefix6, &request->add.prefix4));

	case OP_REMOVE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Removing EAMT entry.");
		return respond_error(nl_hdr, eamt_remove(
				request->remove.prefix6_set ? &request->remove.prefix6 : NULL,
				request->remove.prefix4_set ? &request->remove.prefix4 : NULL));

	case OP_FLUSH:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		return respond_error(nl_hdr, eamt_flush());
	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

static int handle_pool6791_display(struct nlmsghdr *nl_hdr, union request_pool4 *request)
{
	struct nl_buffer *buffer;
	struct ipv4_prefix *prefix;
	int error;

	log_debug("Sending RFC6791 pool to userspace.");

	buffer = nlbuffer_create(nl_socket, nl_hdr);
	if (!buffer)
		return respond_error(nl_hdr, -ENOMEM);

	prefix = request->display.prefix_set ? &request->display.prefix : NULL;
	error = rfc6791_for_each(pool4_to_usr, buffer, prefix);
	error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

	kfree(buffer);
	return error;
}

static int handle_rfc6791_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		union request_pool4 *request)
{
	__u64 count;
	int error;

	if (nat64_is_stateful()) {
		log_err("RFC 6791 does not apply to Stateful NAT64.");
		return -EINVAL;
	}

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		return handle_pool6791_display(nl_hdr, request);

	case OP_COUNT:
		log_debug("Returning IPv4 address count.");
		error = rfc6791_count(&count);
		if (error)
			return respond_error(nl_hdr, error);
		return respond_setcfg(nl_hdr, &count, sizeof(count));

	case OP_ADD:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Adding an address to the IPv4 pool.");
		return respond_error(nl_hdr, rfc6791_add(&request->add.addrs));

	case OP_REMOVE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Removing an address from the IPv4 pool.");

		error = rfc6791_remove(&request->remove.addrs);
		if (error)
			return respond_error(nl_hdr, error);

		return respond_error(nl_hdr, error);

	case OP_FLUSH:
		if (verify_superpriv()) {
			return respond_error(nl_hdr, -EPERM);
		}

		log_debug("Flushing the IPv4 pool...");
		error = rfc6791_flush();
		if (error)
			return respond_error(nl_hdr, error);

		return respond_error(nl_hdr, error);

	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
}

#ifdef BENCHMARK
static int logtime_entry_to_userspace(struct log_node *node, void *arg)
{
	struct nl_buffer *buffer = (struct nl_buffer *) arg;
	struct logtime_entry_usr entry_usr;

	entry_usr.time = node->time;

	return nlbuffer_write(buffer, &entry_usr, sizeof(entry_usr));
}

#endif

static int handle_logtime_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		struct request_logtime *request)
{
#ifdef BENCHMARK
	struct nl_buffer *buffer;
	int error;

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		log_debug("Sending logs time to userspace.");

		buffer = nlbuffer_create(nl_socket, nl_hdr);
		if (!buffer)
			return respond_error(nl_hdr, -ENOMEM);

		error = logtime_iterate_and_delete(request->l3_proto, request->l4_proto,
				logtime_entry_to_userspace, buffer);
		error = (error >= 0) ? nlbuffer_close(buffer, error) : respond_error(nl_hdr, error);

		kfree(buffer);
		return error;
	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		return respond_error(nl_hdr, -EINVAL);
	}
#else
	log_err("Benchmark was not enabled during compilation.");
	return -EINVAL;
#endif
}

static bool ensure_bytes(size_t actual, size_t expected)
{
	if (actual != expected) {
		log_err("Expected a %zu-byte integer, got %zu bytes.", expected, actual);
		return false;
	}
	return true;
}

#ifdef STATEFUL

static bool assign_timeout(void *value, unsigned int min, __u64 *field)
{
	/*
	 * TODO (fine) this max is somewhat arbitrary. We do have a maximum,
	 * but I don't recall what or why it was. I do remember it's bigger than this.
	 */
	const __u32 MAX_U32 = 0xFFFFFFFFU;
	__u64 value64 = *((__u64 *) value);

	if (value64 < 1000 * min) {
		log_err("The timeout must be at least %u seconds.", min);
		return false;
	}
	if (value64 > MAX_U32) {
		log_err("Expected a timeout less than %u seconds", MAX_U32 / 1000);
		return false;
	}

	*field = msecs_to_jiffies(value64);
	return true;
}

#endif

static int be16_compare(const void *a, const void *b)
{
	return *(__u16 *)b - *(__u16 *)a;
}

static void be16_swap(void *a, void *b, int size)
{
	__u16 t = *(__u16 *)a;
	*(__u16 *)a = *(__u16 *)b;
	*(__u16 *)b = t;
}

static int update_plateaus(struct global_config *config, size_t size, void *value)
{
	__u16 *list = value;
	unsigned int count = size / 2;
	unsigned int i, j;

	if (count == 0) {
		log_err("The MTU list received from userspace is empty.");
		return -EINVAL;
	}
	if (size % 2 == 1) {
		log_err("Expected an array of 16-bit integers; got an uneven number of bytes.");
		return -EINVAL;
	}

	/* Sort descending. */
	sort(list, count, sizeof(*list), be16_compare, be16_swap);

	/* Remove zeroes and duplicates. */
	for (i = 0, j = 1; j < count; j++) {
		if (list[j] == 0)
			break;
		if (list[i] != list[j]) {
			i++;
			list[i] = list[j];
		}
	}

	if (list[0] == 0) {
		log_err("The MTU list contains nothing but zeroes.");
		return -EINVAL;
	}

	count = i + 1;
	size = count * sizeof(*list);

	/* Update. */
	config->mtu_plateaus = kmalloc(size, GFP_KERNEL);
	if (!config->mtu_plateaus) {
		log_err("Could not allocate the kernel's MTU plateaus list.");
		return -ENOMEM;
	}
	memcpy(config->mtu_plateaus, list, size);
	config->mtu_plateau_count = count;

	return 0;
}

static int handle_global_update(enum global_type type, size_t size, unsigned char *value)
{
	struct global_config *config;
	bool timer_needs_update = false;
	enum session_timer_type timer_type;
	int error;

	config = kmalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;
	config->mtu_plateaus = NULL;

	error = config_clone(config);
	if (error)
		goto fail;

	switch (type) {
#ifdef STATEFUL
	case MAX_PKTS:
		if (!ensure_bytes(size, 8))
			goto einval;
		config->max_stored_pkts = *((__u64 *) value);
		break;
	case SRC_ICMP6ERRS_BETTER:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->src_icmp6errs_better = *((__u8 *) value);
		break;
	case BIB_LOGGING:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->bib_logging = *((__u8 *) value);
		break;
	case SESSION_LOGGING:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->session_logging = *((__u8 *) value);
		break;

	case UDP_TIMEOUT:
		if (!ensure_bytes(size, 8))
			goto einval;
		if (!assign_timeout(value, UDP_MIN, &config->ttl.udp))
			goto einval;
		timer_needs_update = true;
		timer_type = SESSIONTIMER_UDP;
		break;
	case ICMP_TIMEOUT:
		if (!ensure_bytes(size, 8))
			goto einval;
		if (!assign_timeout(value, 0, &config->ttl.icmp))
			goto einval;
		timer_needs_update = true;
		timer_type = SESSIONTIMER_ICMP;
		break;
	case TCP_EST_TIMEOUT:
		if (!ensure_bytes(size, 8))
			goto einval;
		if (!assign_timeout(value, TCP_EST, &config->ttl.tcp_est))
			goto einval;
		timer_needs_update = true;
		timer_type = SESSIONTIMER_EST;
		break;
	case TCP_TRANS_TIMEOUT:
		if (!ensure_bytes(size, 8))
			goto einval;
		if (!assign_timeout(value, TCP_TRANS, &config->ttl.tcp_trans))
			goto einval;
		timer_needs_update = true;
		timer_type = SESSIONTIMER_TRANS;
		break;
	case FRAGMENT_TIMEOUT:
		if (!ensure_bytes(size, 8))
			goto einval;
		if (!assign_timeout(value, FRAGMENT_MIN, &config->ttl.frag))
			goto einval;
		break;
	case DROP_BY_ADDR:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->drop_by_addr = *((__u8 *) value);
		break;
	case DROP_ICMP6_INFO:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->drop_icmp6_info = *((__u8 *) value);
		break;
	case DROP_EXTERNAL_TCP:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->drop_external_tcp = *((__u8 *) value);
		break;
#else
	case COMPUTE_UDP_CSUM_ZERO:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->compute_udp_csum_zero = *((__u8 *) value);
		break;
	case RANDOMIZE_RFC6791:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->randomize_error_addresses = *((__u8 *) value);
		break;
#endif
	case RESET_TCLASS:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->reset_traffic_class = *((__u8 *) value);
		break;
	case RESET_TOS:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->reset_tos = *((__u8 *) value);
		break;
	case NEW_TOS:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->new_tos = *((__u8 *) value);
		break;
	case DF_ALWAYS_ON:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->atomic_frags.df_always_on = *((__u8 *) value);
		break;
	case BUILD_IPV6_FH:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->atomic_frags.build_ipv6_fh = *((__u8 *) value);
		break;
	case BUILD_IPV4_ID:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->atomic_frags.build_ipv4_id = *((__u8 *) value);
		break;
	case LOWER_MTU_FAIL:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->atomic_frags.lower_mtu_fail = *((__u8 *) value);
		break;
	case MTU_PLATEAUS:
		if (is_error(update_plateaus(config, size, value)))
			goto einval;
		break;
	case DISABLE:
		config->is_disable = (__u8) true;
		break;
	case ENABLE:
		config->is_disable = (__u8) false;
		break;
	case ATOMIC_FRAGMENTS:
		if (!ensure_bytes(size, 1))
			goto einval;
		config->atomic_frags.df_always_on = *((__u8 *) value);
		config->atomic_frags.build_ipv6_fh = *((__u8 *) value);
		config->atomic_frags.build_ipv4_id = !(*((__u8 *) value));
		config->atomic_frags.lower_mtu_fail = !(*((__u8 *) value));
		break;
	default:
		log_err("Unknown config type: %u", type);
		goto einval;
	}

	error = config_set(config);
	if (error)
		goto fail;

	if (timer_needs_update)
		error = sessiondb_update_timer(timer_type);

	return error;

einval:
	error = -EINVAL;
	/* Fall through. */

fail:
	kfree(config->mtu_plateaus);
	kfree(config);
	return error;
}

static int handle_global_config(struct nlmsghdr *nl_hdr, struct request_hdr *nat64_hdr,
		union request_global *request)
{
	struct global_config response = { .mtu_plateaus = NULL };
	unsigned char *buffer;
	size_t buffer_len;
	int error;

	switch (nat64_hdr->operation) {
	case OP_DISPLAY:
		log_debug("Returning 'Global' options.");

		error = config_clone(&response);
		if (error)
			goto end;

		error = serialize_global_config(&response, &buffer, &buffer_len);
		if (error)
			goto end;

		error = respond_setcfg(nl_hdr, buffer, buffer_len);
		kfree(buffer);
		break;

	case OP_UPDATE:
		if (verify_superpriv())
			return respond_error(nl_hdr, -EPERM);

		log_debug("Updating 'Global' options.");

		buffer = (unsigned char *) (request + 1);
		buffer_len = nat64_hdr->length - sizeof(*nat64_hdr) - sizeof(*request);

		error = handle_global_update(request->update.type, buffer_len, buffer);
		break;

	default:
		log_err("Unknown operation: %d", nat64_hdr->operation);
		error = -EINVAL;
	}

end:
	return respond_error(nl_hdr, error);
}

/**
 * Gets called by "netlink_rcv_skb" when the userspace application wants to interact with us.
 *
 * @param skb packet received from userspace.
 * @param nlh message's metadata.
 * @return result status.
 */
static int handle_netlink_message(struct sk_buff *skb_in, struct nlmsghdr *nl_hdr)
{
	struct request_hdr *nat64_hdr;
	void *request;
	int error;

	if (nl_hdr->nlmsg_type != MSG_TYPE_JOOL) {
		log_debug("Expecting %#x but got %#x.", MSG_TYPE_JOOL, nl_hdr->nlmsg_type);
		return -EINVAL;
	}

	nat64_hdr = NLMSG_DATA(nl_hdr);
	request = nat64_hdr + 1;

	error = validate_version(nat64_hdr);
	if (error)
		return respond_error(nl_hdr, error);

	switch (nat64_hdr->mode) {
	case MODE_POOL6:
		return handle_pool6_config(nl_hdr, nat64_hdr, request);
	case MODE_POOL4:
	case MODE_BLACKLIST:
		return handle_pool4_config(nl_hdr, nat64_hdr, request);
	case MODE_BIB:
		return handle_bib_config(nl_hdr, nat64_hdr, request);
	case MODE_SESSION:
		return handle_session_config(nl_hdr, nat64_hdr, request);
	case MODE_EAMT:
		return handle_eamt_config(nl_hdr, nat64_hdr, request);
	case MODE_RFC6791:
		return handle_rfc6791_config(nl_hdr, nat64_hdr, request);
	case MODE_LOGTIME:
		return handle_logtime_config(nl_hdr, nat64_hdr, request);
	case MODE_GLOBAL:
		return handle_global_config(nl_hdr, nat64_hdr, request);
	}

	log_err("Unknown configuration mode: %d", nat64_hdr->mode);
	return respond_error(nl_hdr, -EINVAL);
}

/**
 * Gets called by Netlink when the userspace application wants to interact with us.
 *
 * @param skb packet received from userspace.
 */
static void receive_from_userspace(struct sk_buff *skb)
{
	log_debug("Message arrived.");
	mutex_lock(&config_mutex);
	netlink_rcv_skb(skb, &handle_netlink_message);
	mutex_unlock(&config_mutex);
}

int nlhandler_init(void)
{
	/*
	 * The function changed between Linux 3.5.7 and 3.6, and then again from 3.6.11 to 3.7.
	 *
	 * If you're reading the kernel's Git history, that appears to be the commit
	 * a31f2d17b331db970259e875b7223d3aba7e3821 (v3.6-rc1~125^2~337) and then again in
	 * 9f00d9776bc5beb92e8bfc884a7e96ddc5589e2e (v3.7-rc1~145^2~194).
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	nl_socket = netlink_kernel_create(&init_net, NETLINK_USERSOCK, 0, receive_from_userspace,
			NULL, THIS_MODULE);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
	struct netlink_kernel_cfg nl_cfg = { .input  = receive_from_userspace };
	nl_socket = netlink_kernel_create(&init_net, NETLINK_USERSOCK, THIS_MODULE, &nl_cfg);
#else
	struct netlink_kernel_cfg nl_cfg = { .input  = receive_from_userspace };
	nl_socket = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &nl_cfg);
#endif

	if (!nl_socket) {
		log_err("Creation of netlink socket failed.");
		return -EINVAL;
	}
	log_debug("Netlink socket created.");

	return 0;
}

void nlhandler_destroy(void)
{
	netlink_kernel_release(nl_socket);
}

int serialize_global_config(struct global_config *config, unsigned char **buffer_out,
		size_t *buffer_len_out)
{
	unsigned char *buffer;
	struct global_config *tmp;
	size_t mtus_len;
	bool disabled;

	mtus_len = config->mtu_plateau_count * sizeof(*config->mtu_plateaus);

	buffer = kmalloc(sizeof(*config) + mtus_len, GFP_KERNEL);
	if (!buffer) {
		log_debug("Could not allocate the configuration structure.");
		return -ENOMEM;
	}

	memcpy(buffer, config, sizeof(*config));
	memcpy(buffer + sizeof(*config), config->mtu_plateaus, mtus_len);
	tmp = (struct global_config *) buffer;

#ifdef STATEFUL
	tmp->ttl.udp = jiffies_to_msecs(config->ttl.udp);
	tmp->ttl.tcp_est = jiffies_to_msecs(config->ttl.tcp_est);
	tmp->ttl.tcp_trans = jiffies_to_msecs(config->ttl.tcp_trans);
	tmp->ttl.icmp = jiffies_to_msecs(config->ttl.icmp);
	tmp->ttl.frag = jiffies_to_msecs(config->ttl.frag);
	disabled = config->is_disable || pool6_is_empty() || pool4_is_empty();
#else
	disabled = config->is_disable || (pool6_is_empty() && eamt_is_empty());
#endif
	tmp->jool_status = !disabled;

	*buffer_out = buffer;
	*buffer_len_out = sizeof(*config) + mtus_len;
	return 0;
}

int deserialize_global_config(void *buffer, __u16 buffer_len, struct global_config *target_out)
{
	size_t mtus_len;

	memcpy(target_out, buffer, sizeof(*target_out));

	target_out->mtu_plateaus = NULL;
	if (target_out->mtu_plateau_count) {
		mtus_len = target_out->mtu_plateau_count * sizeof(*target_out->mtu_plateaus);
		target_out->mtu_plateaus = kmalloc(mtus_len, GFP_ATOMIC);
		if (!target_out->mtu_plateaus) {
			log_debug("Could not allocate the config's plateaus.");
			return -ENOMEM;
		}
		memcpy(target_out->mtu_plateaus, buffer + sizeof(*target_out), mtus_len);
	}

#ifdef STATEFUL
	target_out->ttl.udp = msecs_to_jiffies(target_out->ttl.udp);
	target_out->ttl.tcp_est = msecs_to_jiffies(target_out->ttl.tcp_est);
	target_out->ttl.tcp_trans = msecs_to_jiffies(target_out->ttl.tcp_trans);
	target_out->ttl.icmp = msecs_to_jiffies(target_out->ttl.icmp);
	target_out->ttl.frag = msecs_to_jiffies(target_out->ttl.frag);
#endif

	return 0;
}
