/*	$NetBSD: sink_40.h,v 1.1 2024/02/18 20:57:44 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#ifndef GENERIC_SINK_40_H
#define GENERIC_SINK_40_H 1

typedef struct dns_rdata_sink_t {
	dns_rdatacommon_t common;
	isc_mem_t *mctx;
	uint8_t meaning;
	uint8_t coding;
	uint8_t subcoding;
	uint16_t datalen;
	unsigned char *data;
} dns_rdata_sink_t;

#endif /* GENERIC_SINK_40_H */
