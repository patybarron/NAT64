#ifndef _JOOL_MOD_ADDRESS_H
#define _JOOL_MOD_ADDRESS_H

#include <linux/string.h>
#include "nat64/common/types.h"


union transport_addr {
	struct ipv6_transport_addr addr6;
	struct ipv4_transport_addr addr4;
};

int prefix6_parse(char *str, struct ipv6_prefix *result);

/**
 * @{
 * Returns "true" if the first parameter is the same as the second one, even if they are pointers
 * to different places in memory.
 *
 * @param a struct you want to compare to "b".
 * @param b struct you want to compare to "a".
 * @return (*addr_1) === (*addr_2), with null checks as appropriate.
 */
bool addr4_equals(const struct in_addr *a, const struct in_addr *b);
bool addr6_equals(const struct in6_addr *a, const struct in6_addr *b);
bool ipv4_transport_addr_equals(const struct ipv4_transport_addr *a,
		const struct ipv4_transport_addr *b);
bool ipv6_transport_addr_equals(const struct ipv6_transport_addr *a,
		const struct ipv6_transport_addr *b);
bool prefix6_equals(const struct ipv6_prefix *a, const struct ipv6_prefix *b);
bool prefix4_equals(const struct ipv4_prefix *a, const struct ipv4_prefix *b);
/**
 * @}
 */

bool prefix4_contains(const struct ipv4_prefix *prefix, const struct in_addr *addr);
bool prefix6_contains(const struct ipv6_prefix *prefix, const struct in6_addr *addr);

bool prefix4_intersects(const struct ipv4_prefix *p1, const struct ipv4_prefix *p2);

__u64 prefix4_get_addr_count(struct ipv4_prefix *prefix);

int prefix4_validate(struct ipv4_prefix *prefix);
int prefix6_validate(struct ipv6_prefix *prefix);

__u32 addr4_get_bit(struct in_addr *addr, unsigned int pos);
void addr4_set_bit(struct in_addr *addr, unsigned int pos, bool value);
__u32 addr6_get_bit(struct in6_addr *addr, unsigned int pos);
void addr6_set_bit(struct in6_addr *addr, unsigned int pos, bool value);

/**
 * The kernel has a ipv6_addr_cmp(), but not a ipv4_addr_cmp().
 * Of course, that is because in_addrs are, to most intents and purposes, 32-bit integer values.
 * But the absence of ipv4_addr_cmp() does makes things look asymmetric.
 * So, booya.
 *
 * @return positive if a2 is bigger, negative if a1 is bigger, zero it they're equal.
 */
static inline int ipv4_addr_cmp(const struct in_addr *a1, const struct in_addr *a2)
{
	return memcmp(a1, a2, sizeof(struct in_addr));
}

#endif /* _JOOL_MOD_ADDRESS_H */
