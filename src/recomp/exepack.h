#ifndef SSO_EXEPACK_H
#define SSO_EXEPACK_H
#include <stddef.h>
#include <stdint.h>
int exepack_is_packed(const uint8_t *d, size_t n);
/* 0 on success; *out is malloc'd, a plain MZ (caller frees) */
int exepack_unpack(const uint8_t *d, size_t n, uint8_t **out, size_t *out_n);
#endif
