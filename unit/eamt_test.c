/*
 * address_mapping_test.c
 *
 *  Created on: Dic 2, 2014
 *      Author: dhernandez
 */

#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */
#include <linux/init.h> /* Needed for the macros */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dhernandez");
MODULE_AUTHOR("aleiva");
MODULE_DESCRIPTION("Unit tests for the EAMT module");
MODULE_ALIAS("nat64_test_address_mapping");

#include "nat64/common/str_utils.h"
#include "nat64/unit/types.h"
#include "nat64/unit/unit_test.h"
#include "../mod/stateless/eam.c"

static bool init(void)
{
	return eamt_init() ? false : true;
}

static void end(void)
{
	eamt_destroy();
}

static int __add_entry(char *addr4, __u8 len4, char *addr6, __u8 len6)
{
	struct ipv4_prefix prefix4;
	struct ipv6_prefix prefix6;

	if (str_to_addr4(addr4, &prefix4.address))
		return false;
	prefix4.len = len4;

	if (str_to_addr6(addr6, &prefix6.address))
		return false;
	prefix6.len = len6;

	return eamt_add(&prefix6, &prefix4);
}

static bool add_test(void)
{
	bool success = true;

	/* Collision tests */
	success &= assert_equals_int(0, __add_entry("1.0.0.4", 30, "1::c", 126), "hackless add");
	success &= assert_equals_int(-EEXIST, __add_entry("1.0.0.4", 30, "1::c", 126), "simple collision");
	success &= assert_equals_int(-EEXIST, __add_entry("1.0.0.6", 31, "2::c", 127), "4 is inside");
	success &= assert_equals_int(-EEXIST, __add_entry("2.0.0.4", 31, "1::e", 127), "6 is inside");
	success &= assert_equals_int(-EEXIST, __add_entry("1.0.0.0", 24, "2::", 120), "4 is outside");
	success &= assert_equals_int(-EEXIST, __add_entry("2.0.0.0", 24, "1::", 120), "6 is outside");
	success &= assert_equals_int(0, __add_entry("1.0.0.0", 30, "1::", 126), "no collision");

	/* Prefix length tests*/
	success &= assert_equals_int(-EINVAL, __add_entry("5.0.0.0", 24, "5::", 124), "bigger suffix4");
	success &= assert_equals_int(0, __add_entry("5.0.0.0", 28, "5::", 120), "bigger suffix6");
	success &= assert_equals_int(-EINVAL, __add_entry("6.0.0.0", 33, "6::", 128), "prefix4 too big");
	success &= assert_equals_int(-EINVAL, __add_entry("6.0.0.0", 32, "6::", 129), "prefix6 too big");
	success &= assert_equals_int(-EINVAL, __add_entry("7.0.0.1", 24, "7::", 120), "nonzero suffix4");
	success &= assert_equals_int(-EINVAL, __add_entry("7.0.0.0", 24, "7::1", 120), "nonzero suffix6");

	return success;
}

static bool add_entry(char *addr4, __u8 len4, char *addr6, __u8 len6)
{
	if (__add_entry(addr4, len4, addr6, len6)) {
		log_err("The call to eamt_add() failed.");
		return false;
	}

	return true;
}

static bool test(char *addr4_str, char *addr6_str)
{
	struct in_addr addr4, result4;
	struct in6_addr addr6, result6;
	int error1, error2;
	bool success = true;

	log_debug("Testing %s <-> %s...", addr4_str, addr6_str);

	if (str_to_addr4(addr4_str, &addr4))
		return false;
	if (str_to_addr6(addr6_str, &addr6))
		return false;

	error1 = eamt_get_ipv6_by_ipv4(&addr4, &result6);
	error2 = eamt_get_ipv4_by_ipv6(&addr6, &result4);
	if (error1 || error2) {
		log_err("The call to eamt_get_ipv6_by_ipv4() spew errcode %d.", error1);
		log_err("The call to eamt_get_ipv4_by_ipv6() spew errcode %d.", error2);
		return false;
	}

	success &= assert_equals_ipv6(&addr6, &result6, "IPv4 to IPv6 result");
	success &= assert_equals_ipv4(&addr4, &result4, "IPv6 to IPv4 result");
	return success;
}

static bool daniel_test(void)
{
	bool success = true;

	success &= add_entry("10.0.0.0", 30, "2001:db8::0", 126);
	success &= add_entry("10.0.0.12", 30, "2001:db8::4", 126);
	success &= add_entry("10.0.0.16", 28, "2001:db8::20", 124);
	success &= add_entry("10.0.0.254", 32, "2001:db8::111", 128);
	success &= add_entry("10.0.1.0", 24, "2001:db8::200", 120);
	if (!success)
		return false;

	success &= test("10.0.0.2", "2001:db8::2");
	success &= test("10.0.0.14", "2001:db8::6");
	success &= test("10.0.0.27", "2001:db8::2b");
	success &= test("10.0.0.254", "2001:db8::111");
	success &= test("10.0.1.15", "2001:db8::20f");

	return success;
}

static bool anderson_test(void)
{
	bool success = true;

	success &= add_entry("192.0.2.1", 32, "2001:db8:aaaa::", 128);
	success &= add_entry("192.0.2.2", 32, "2001:db8:bbbb::b", 128);
	success &= add_entry("192.0.2.16", 28, "2001:db8:cccc::", 124);
	success &= add_entry("192.0.2.128", 26, "2001:db8:dddd::", 64);
	success &= add_entry("192.0.2.192", 31, "64:ff9b::", 127);
	if (!success)
		return false;

	success &= test("192.0.2.1", "2001:db8:aaaa::");
	success &= test("192.0.2.2", "2001:db8:bbbb::b");
	success &= test("192.0.2.16", "2001:db8:cccc::");
	success &= test("192.0.2.24", "2001:db8:cccc::8");
	success &= test("192.0.2.31", "2001:db8:cccc::f");
	success &= test("192.0.2.128", "2001:db8:dddd::");
	success &= test("192.0.2.152", "2001:db8:dddd:0:6000::");
	success &= test("192.0.2.183", "2001:db8:dddd:0:dc00::");
	success &= test("192.0.2.191", "2001:db8:dddd:0:fc00::");
	success &= test("192.0.2.193", "64:ff9b::1");

	return success;
}

static bool remove_entry(char *addr4, __u8 len4, char *addr6, __u8 len6, int expected_error)
{
	struct ipv4_prefix prefix4;
	struct ipv6_prefix prefix6;

	if (!addr4 && !addr6) {
		log_err("Test syntax error, addr4 and addr6 are NULL");
	}

	if (addr4) {
		if (is_error(str_to_addr4(addr4, &prefix4.address)))
			return false;
		prefix4.len = len4;
	}

	if (addr6) {
		if (is_error(str_to_addr6(addr6, &prefix6.address)))
			return false;
		prefix6.len = len6;
	}

	return assert_equals_int(expected_error, eamt_remove(addr6 ? &prefix6 : NULL,
			addr4 ? &prefix4 : NULL), "removing EAM entry");
}

static bool remove_test(void)
{
	bool success = true;

	success &= add_entry("10.0.0.0", 24, "1::", 120);
	success &= remove_entry("10.0.0.0", 24, "1::", 120, 0);

	success &= add_entry("20.0.0.0", 25, "2::", 121);
	success &= remove_entry("30.0.0.1", 25, NULL, 0, -ESRCH);
	success &= remove_entry("20.0.0.130", 25, NULL, 0, -ESRCH);
	success &= remove_entry("20.0.0.120", 25, NULL, 0, 0);

	success &= add_entry("30.0.0.0", 24, "3::", 120);
	success &= remove_entry(NULL, 0, "3::1:0", 120, -ESRCH);
	success &= remove_entry(NULL, 0, "3::0", 120, 0);

	success &= add_entry("10.0.0.0", 24, "1::", 120);
	success &= remove_entry("10.0.1.0", 24, "1::", 120, -EINVAL);
	success &= remove_entry("10.0.0.0", 24, "1::", 120, 0);

	success &= assert_equals_u64(0, eam_table.count, "Table count");
	if (!success)
		return false;

	success &= add_entry("10.0.0.0", 24, "2001:db8::100", 120);
	success &= add_entry("10.0.1.0", 24, "2001:db8::200", 120);
	success &= add_entry("10.0.2.0", 24, "2001:db8::300", 120);
	if (!success)
		return false;

	success &= assert_equals_int(0, eamt_flush(), "flush result");
	success &= assert_equals_u64(0, eam_table.count, "Table count 2");

	return success;
}

static int address_mapping_test_init(void)
{
	START_TESTS("Address Mapping test");

	INIT_CALL_END(init(), add_test(), end(), "add function");
	INIT_CALL_END(init(), daniel_test(), end(), "Daniel's translation tests");
	INIT_CALL_END(init(), anderson_test(), end(), "Translation tests from T. Anderson's draft");
	INIT_CALL_END(init(), remove_test(), end(), "remove function");

	END_TESTS;
}

static void address_mapping_test_exit(void)
{
	/* No code. */
}

module_init(address_mapping_test_init);
module_exit(address_mapping_test_exit);
