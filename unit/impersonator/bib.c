#include "nat64/mod/stateful/bib/db.h"

/*
 * In current BIB impersonator users, BIB is optional.
 * This is good, because BIB should not really be tested in unit tests other
 * than its own.
 * Therefore, these functions should never be called.
 */

void bibentry_get(struct bib_entry *bib)
{
	log_err("This function was called! The unit test is broken.");
	BUG();
}

void bibdb_return(struct bib_entry *bib)
{
	log_err("This function was called! The unit test is broken.");
	BUG();
}

bool bibdb_contains4(const struct ipv4_transport_addr *addr,
		const l4_protocol proto)
{
	log_err("This function was called! The unit test is broken.");
	BUG();
}

int pool4db_foreach_taddr4(const __u32 mark,
		int (*func)(struct ipv4_transport_addr *, void *), void *arg,
		unsigned int offset)
{
	log_err("This function was called! The unit test is broken.");
	BUG();
}