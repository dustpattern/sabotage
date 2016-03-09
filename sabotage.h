/**
 * sabotage.h
 *
 * NOTE: This file is included automatically by the `sabotage' preprocessor.
 *
 * This work belongs to the Public Domain. Everyone is free to use, modify,
 * republish, sell or give away this work without prior consent from anybody.
 *
 * This software is provided on an "AS IS" basis, without warranty of any kind.
 * Use at your own risk! Under no circumstances shall the author(s) or
 * contributor(s) be liable for damages resulting directly or indirectly from
 * the use or non-use of this documentation.
 */

#ifndef __SABOTAGE_H__
#define __SABOTAGE_H__

#define SABOTAGE __builtin_expect(__sabotage(__FILE__, __func__, __LINE__), 0)
extern int __sabotage(char const *, char const *, int unsigned);

#endif
