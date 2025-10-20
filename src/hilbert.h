/*****
 *
 * Description: Hilbert Curve Engine Headers
 *
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 *
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****/

#ifndef HILBERT_DOT_H
#define HILBERT_DOT_H

/****
 *
 * includes
 *
 ****/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "../include/sysdep.h"

#ifndef __SYSDEP_H__
#error something is messed up
#endif

#include "../include/common.h"
#include <stdint.h>

/****
 *
 * defines
 *
 ****/

/* Hilbert curve orders (2^order x 2^order grid) */
#define HILBERT_ORDER_MIN 4
#define HILBERT_ORDER_MAX 16
#define HILBERT_ORDER_DEFAULT 12  /* 4096x4096 = 16M points */

/* Hash seed for IP distribution */
#define HILBERT_HASH_SEED 0x9747b28c

/****
 *
 * typedefs & structs
 *
 ****/

/**
 * Hilbert curve coordinate structure
 */
typedef struct {
    uint32_t x;          /* X coordinate on curve */
    uint32_t y;          /* Y coordinate on curve */
    uint8_t order;       /* Curve order (determines resolution) */
} HilbertCoord_t;

/**
 * Configuration for Hilbert curve generation
 */
typedef struct {
    uint8_t order;           /* Hilbert curve order (2^order × 2^order grid) */
    uint32_t dimension;      /* Derived: 2^order */
    uint64_t total_points;   /* Derived: dimension^2 */
} HilbertConfig_t;

/****
 *
 * function prototypes
 *
 ****/

/* Initialization and configuration */
int initHilbert(uint8_t order);
void deInitHilbert(void);
HilbertConfig_t *getHilbertConfig(void);

/* Core Hilbert curve functions */
uint64_t hilbertXYToIndex(uint32_t x, uint32_t y, uint8_t order);
void hilbertIndexToXY(uint64_t index, uint8_t order, uint32_t *x, uint32_t *y);

/* IP address to Hilbert coordinate mapping */
uint64_t ipToHilbertIndex(uint32_t ipv4, uint8_t order);
HilbertCoord_t ipToHilbert(uint32_t ipv4, uint8_t order);

/* Hash function for IP distribution */
uint32_t murmurhash3_32(const void *key, int len, uint32_t seed);

/* Utility functions */
int isValidOrder(uint8_t order);
uint32_t getDimension(uint8_t order);
uint64_t getTotalPoints(uint8_t order);

/* IP classification */
int isNonRoutableIP(uint32_t ipv4);

/* CIDR mapping functions */
int loadCIDRMapping(const char *filename);
void freeCIDRMapping(void);

#endif /* HILBERT_DOT_H */
