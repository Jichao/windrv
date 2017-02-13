#include <ntddk.h>
#include "stringutils.h"

wchar_t *jj_wcstok_s(wchar_t * s1, const wchar_t * s2, wchar_t ** ptr)
{
	wchar_t *p;

	if (s1 == NULL)
		s1 = *ptr;
	while (*s1 && wcschr(s2, *s1))
		s1++;
	if (!*s1) {
		*ptr = s1;
		return NULL;
	}
	for (p = s1; *s1 && !wcschr(s2, *s1); s1++)
		continue;
	if (*s1)
		*s1++ = L'\0';
	*ptr = s1;
	return p;
}
