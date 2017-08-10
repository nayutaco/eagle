/* Copyright 2015 BitPay, Inc.
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */
//#include "picocoin-config.h"
#include <string.h>
#include "picocoin/cstr.h"

static bool ICACHE_FLASH_ATTR cstr_alloc_min_sz(cstring *s, size_t sz)
{
	sz++;		// NUL overhead

	if (s->alloc && (s->alloc >= sz))
		return true;

	unsigned int shift = 3;
	unsigned int al_sz;
	while ((al_sz = (1 << shift)) < sz)
		shift++;

	char *new_s = REALLOC(s->str, al_sz);
	if (!new_s)
		return false;

	s->str = new_s;
	s->alloc = al_sz;
	s->str[s->len] = 0;

	return true;
}

cstring *ICACHE_FLASH_ATTR cstr_new_sz(size_t sz)
{
	cstring *s = MALLOC(sizeof(cstring));
	if (!s)
		return NULL;

    BZERO(s, sizeof(cstring));
	if (!cstr_alloc_min_sz(s, sz)) {
		FREE(s);
		return NULL;
	}

	return s;
}

//cstring *cstr_new_buf(const void *buf, size_t sz)
//{
//	cstring *s = cstr_new_sz(sz);
//	if (!s)
//		return NULL;
//
//	MEMCPY(s->str, buf, sz);
//	s->len = sz;
//	s->str[s->len] = 0;
//
//	return s;
//}
//
//cstring *cstr_new(const char *init_str)
//{
//	if (!init_str || !*init_str)
//		return cstr_new_sz(0);
//
//	size_t slen = STRLEN(init_str);
//	return cstr_new_buf(init_str, slen);
//}

void ICACHE_FLASH_ATTR cstr_free(cstring *s, bool free_buf)
{
	if (!s)
		return;

	if (free_buf)
		FREE(s->str);

	MEMSET(s, 0, sizeof(*s));
	FREE(s);
}

bool ICACHE_FLASH_ATTR cstr_resize(cstring *s, size_t new_sz)
{
	// no change
	if (new_sz == s->len)
		return true;

	// truncate string
	if (new_sz <= s->len) {
		s->len = new_sz;
		s->str[s->len] = 0;
		return true;
	}

	// increase string size
	if (!cstr_alloc_min_sz(s, new_sz))
		return false;

	// contents of string tail undefined

	s->len = new_sz;
	s->str[s->len] = 0;

	return true;
}

//bool cstr_append_buf(cstring *s, const void *buf, size_t sz)
//{
//	if (!cstr_alloc_min_sz(s, s->len + sz))
//		return false;
//
//	MEMCPY(s->str + s->len, buf, sz);
//	s->len += sz;
//	s->str[s->len] = 0;
//
//	return true;
//}
//
//bool cstr_equal(const cstring *a, const cstring *b)
//{
//	if (a == b)
//		return true;
//	if (!a || !b)
//		return false;
//	if (a->len != b->len)
//		return false;
//	return (MEMCMP(a->str, b->str, a->len) == 0);
//}
//
//bool cstr_erase(cstring *s, size_t pos, ssize_t len)
//{
//	if (pos == s->len && len == 0)
//		return true;
//	if (pos >= s->len)
//		return false;
//
//	size_t old_tail = s->len - pos;
//	if ((len >= 0) && (len > old_tail))
//		return false;
//
//	MEMMOVE(&s->str[pos], &s->str[pos + len], old_tail - len);
//	s->len -= len;
//	s->str[s->len] = 0;
//
//	return true;
//}

