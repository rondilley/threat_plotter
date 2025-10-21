/*****
 *
 * Description: GeoIP Lookup Headers
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

#ifndef GEOIP_DOT_H
#define GEOIP_DOT_H

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
#include <time.h>

/****
 *
 * defines
 *
 ****/

/* Timezone offset bounds (UTC-12 to UTC+14) */
#define TIMEZONE_OFFSET_MIN -12
#define TIMEZONE_OFFSET_MAX 14
#define TIMEZONE_OFFSET_RANGE (TIMEZONE_OFFSET_MAX - TIMEZONE_OFFSET_MIN + 1)  /* 27 timezones */

/* GeoIP cache settings */
#define GEOIP_CACHE_SIZE_DEFAULT 100000
#define GEOIP_CACHE_TTL_DEFAULT 3600  /* 1 hour */

/****
 *
 * typedefs & structs
 *
 ****/

/**
 * Geographic location data
 */
typedef struct {
    float latitude;
    float longitude;
    char country_code[4];       /* ISO 3166-1 alpha-2 code */
    char country_name[64];
    int timezone_offset;        /* Hours offset from UTC (-12 to +14) */
    char timezone_name[64];     /* IANA timezone name (e.g., "America/New_York") */
    uint8_t valid;              /* Whether lookup succeeded */
} GeoLocation_t;

/**
 * GeoIP cache entry
 */
typedef struct {
    uint32_t ip;                /* IPv4 address (network byte order) */
    GeoLocation_t location;     /* Cached location data */
    time_t cached_time;         /* When this entry was cached */
    uint32_t hit_count;         /* Number of cache hits */
} GeoIPCacheEntry_t;

/**
 * ASN (Autonomous System Number) data
 */
typedef struct {
    uint32_t asn;               /* AS Number (e.g., 15169 for Google) */
    char asn_org[128];          /* Organization name (e.g., "GOOGLE") */
    uint8_t valid;              /* Whether lookup succeeded */
} ASNInfo_t;

/**
 * ASN cache entry
 */
typedef struct {
    uint32_t ip;                /* IPv4 address (network byte order) */
    ASNInfo_t asn_info;         /* Cached ASN data */
    time_t cached_time;         /* When this entry was cached */
    uint32_t hit_count;         /* Number of cache hits */
} ASNCacheEntry_t;

/****
 *
 * function prototypes
 *
 ****/

/* Initialization and cleanup */
int initGeoIP(const char *db_path);
int initASN(const char *db_path);
void deInitGeoIP(void);
void deInitASN(void);
int isGeoIPAvailable(void);
int isASNAvailable(void);

/* IP lookup functions */
GeoLocation_t *lookupGeoIP(uint32_t ipv4);
ASNInfo_t *lookupASN(uint32_t ipv4);
int getTimezoneOffset(uint32_t ipv4);
const char *getTimezoneLabel(int offset);

/* Fallback functions (when GeoIP DB not available) */
GeoLocation_t *fallbackGeoIP(uint32_t ipv4);
int fallbackTimezoneFromIP(uint32_t ipv4);

/* Cache management */
void clearGeoIPCache(void);
void clearASNCache(void);
void printGeoIPCacheStats(void);
void printASNCacheStats(void);

/* Utility functions */
int parseTimezoneOffset(const char *tz_name);
void formatIPAddress(uint32_t ipv4, char *buf, size_t buf_size);

#endif /* GEOIP_DOT_H */
