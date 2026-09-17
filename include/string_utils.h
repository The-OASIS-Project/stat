/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s).
 *
 * String Utilities - Safe bounded string copy primitives.
 *
 * Vendored subset-snapshot of DAWN's common/include/utils/string_utils.h:
 * STAT needs only the bounded-copy primitives (safe_strncpy/safe_strscpy),
 * so DAWN's JSON/URL/sentence helpers are intentionally omitted. The function
 * bodies are carried verbatim so behavior matches DAWN's; if DAWN hardens
 * safe_strncpy, sync it here. Uses a project-neutral guard (like STAT's
 * logging.h -> OASIS_LOGGING_H) rather than DAWN's, since a subset reusing
 * DAWN's guard would #ifdef-out DAWN's full header if the two were ever
 * co-included with this one seen first.
 */

#ifndef OASIS_STRING_UTILS_H
#define OASIS_STRING_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Safe bounded string copy with guaranteed null-termination.
 *
 * Unlike strncpy, this always null-terminates the destination and does not
 * pad the tail with zeros. Portable replacement for strlcpy; the canonical
 * explicit-size bounded copy for the project. Prefer safe_strscpy() (below)
 * when the destination is a fixed-size array — it derives the size and rejects
 * a pointer destination at compile time.
 *
 * NULL-safe: a NULL dest (or size 0) is a no-op; a NULL src yields an empty
 * dest. Thread-safe (modifies only the dest buffer).
 *
 * @param dest Destination buffer (may be NULL).
 * @param src Source string, null-terminated (may be NULL).
 * @param size Capacity of the destination buffer (copies at most size-1).
 * @return The length of @p src (strlcpy semantics): a return value >= @p size
 *         means the copy was TRUNCATED. Returns 0 for a NULL/empty src or a
 *         no-op call. Callers that don't care may ignore it.
 */
static inline size_t safe_strncpy(char *dest, const char *src, size_t size) {
   if (dest == NULL || size == 0) {
      return 0;
   }
   if (src == NULL) {
      dest[0] = '\0';
      return 0;
   }
   size_t srclen = strlen(src);
   size_t copylen = srclen < size ? srclen : size - 1;
   memcpy(dest, src, copylen);
   dest[copylen] = '\0';
   return srclen;
}

/*
 * safe_strscpy(dst, src) — the preferred bounded copy for a fixed-size array.
 *
 * Derives the capacity from sizeof(dst) so the size can't be mis-passed, and
 * rejects a POINTER destination at compile time (a pointer would otherwise copy
 * only sizeof(pointer)-1 bytes). Returns strlcpy semantics like safe_strncpy:
 * a return >= sizeof(dst) means truncation.
 *
 * Use safe_strncpy() directly when the destination is a pointer with a known
 * capacity (a size parameter), where sizeof(dst) would be wrong.
 *
 * Two implementations give the same guarantee:
 *  - C: a macro using the GCC/Clang array-detection idiom (typeof +
 *    __builtin_types_compatible_p). OASIS_MUST_BE_ARRAY expands to 0 for an array
 *    and to an ill-formed (negative-width bitfield) type otherwise, failing the
 *    build. OASIS_-prefixed (not reserved __names) to stay collision-safe in this
 *    widely-included header.
 *  - C++: an array-reference template (char (&)[N]) — a pointer won't bind, so it
 *    yields the same compile-time rejection without the GNU builtins, which the
 *    C++ front end does not accept.
 */
#ifdef __cplusplus
} /* extern "C" — a function template cannot have C language linkage */
template<size_t N> static inline size_t safe_strscpy(char (&dst)[N], const char *src) {
   return safe_strncpy(dst, src, N);
}
extern "C" {
#else
#define OASIS_SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#define OASIS_MUST_BE_ARRAY(a) \
   (sizeof(struct { int _dummy[1 - 2 * !!(OASIS_SAME_TYPE((a), &(a)[0]))]; }) * 0)
#define safe_strscpy(dst, src) safe_strncpy((dst), (src), sizeof(dst) + OASIS_MUST_BE_ARRAY(dst))
#endif

#ifdef __cplusplus
}
#endif

#endif /* OASIS_STRING_UTILS_H */
