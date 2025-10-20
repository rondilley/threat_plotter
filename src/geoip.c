/*****
 *
 * Description: GeoIP Lookup Implementation
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

/****
 *
 * includes
 *
 ****/

#include "geoip.h"
#include "mem.h"
#include "util.h"
#include "hash.h"
#include <maxminddb.h>
#include <string.h>
#include <arpa/inet.h>

/****
 *
 * local variables
 *
 ****/

PRIVATE MMDB_s mmdb;
PRIVATE int geoip_initialized = FALSE;
PRIVATE struct hash_s *geoip_cache = NULL;

/* Cache statistics */
PRIVATE uint32_t cache_hits = 0;
PRIVATE uint32_t cache_misses = 0;
PRIVATE uint32_t lookup_success = 0;
PRIVATE uint32_t lookup_failures = 0;

/****
 *
 * external variables
 *
 ****/

extern Config_t *config;

/****
 *
 * functions
 *
 ****/

/****
 * Initialize GeoIP lookup system
 ****/
int initGeoIP(const char *db_path)
{
    int status;

    if (geoip_initialized) {
        fprintf(stderr, "WARN - GeoIP already initialized\n");
        return TRUE;
    }

    /* Open MaxMind database */
    status = MMDB_open(db_path, MMDB_MODE_MMAP, &mmdb);
    if (status != MMDB_SUCCESS) {
        fprintf(stderr, "ERR - Cannot open GeoIP database %s: %s\n",
                db_path, MMDB_strerror(status));
        return FALSE;
    }

    /* Initialize cache */
    geoip_cache = initHash(GEOIP_CACHE_SIZE_DEFAULT);
    if (geoip_cache == NULL) {
        fprintf(stderr, "ERR - Cannot initialize GeoIP cache\n");
        MMDB_close(&mmdb);
        return FALSE;
    }

    geoip_initialized = TRUE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - GeoIP initialized: %s (type=%s)\n",
                db_path, mmdb.metadata.database_type);
    }
#endif

    return TRUE;
}

/****
 * Clean up GeoIP resources
 ****/
void deInitGeoIP(void)
{
    if (!geoip_initialized) {
        return;
    }

    if (geoip_cache != NULL) {
        freeHash(geoip_cache);
        geoip_cache = NULL;
    }

    MMDB_close(&mmdb);
    geoip_initialized = FALSE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - GeoIP deinitialized (hits=%u, misses=%u, success=%u, fail=%u)\n",
                cache_hits, cache_misses, lookup_success, lookup_failures);
    }
#endif
}

/****
 * Check if GeoIP is available
 ****/
int isGeoIPAvailable(void)
{
    return geoip_initialized;
}

/****
 * Parse timezone offset from IANA timezone name
 * This is a simplified approximation - real implementation would need full TZ database
 ****/
int parseTimezoneOffset(const char *tz_name)
{
    /* Common timezone mappings */
    struct {
        const char *prefix;
        int offset;
    } tz_map[] = {
        {"Pacific/Midway", -11},
        {"Pacific/Honolulu", -10},
        {"America/Anchorage", -9},
        {"America/Los_Angeles", -8},
        {"America/Denver", -7},
        {"America/Chicago", -6},
        {"America/New_York", -5},
        {"America/Halifax", -4},
        {"America/St_Johns", -3},
        {"America/Sao_Paulo", -3},
        {"Atlantic/South_Georgia", -2},
        {"Atlantic/Azores", -1},
        {"Europe/London", 0},
        {"Europe/Paris", 1},
        {"Europe/Athens", 2},
        {"Europe/Moscow", 3},
        {"Asia/Dubai", 4},
        {"Asia/Karachi", 5},
        {"Asia/Dhaka", 6},
        {"Asia/Bangkok", 7},
        {"Asia/Shanghai", 8},
        {"Asia/Tokyo", 9},
        {"Australia/Sydney", 10},
        {"Pacific/Noumea", 11},
        {"Pacific/Auckland", 12},
        {NULL, 0}
    };

    for (int i = 0; tz_map[i].prefix != NULL; i++) {
        if (strstr(tz_name, tz_map[i].prefix) == tz_name) {
            return tz_map[i].offset;
        }
    }

    /* Default to UTC if unknown */
    return 0;
}

/****
 * Lookup geographic location for IP address
 ****/
GeoLocation_t *lookupGeoIP(uint32_t ipv4)
{
    GeoIPCacheEntry_t *cached;
    MMDB_lookup_result_s lookup_result;
    MMDB_entry_data_s entry_data;
    int gai_error, mmdb_error;
    char ip_str[INET_ADDRSTRLEN];
    char cache_key[16];  /* Key for hash table (IP as string) */
    struct sockaddr_in sa;

    if (!geoip_initialized) {
        lookup_failures++;
        return fallbackGeoIP(ipv4);
    }

    /* Create hash key from IP */
    snprintf(cache_key, sizeof(cache_key), "%u", ipv4);

    /* Check cache first */
    cached = (GeoIPCacheEntry_t *)getHashData(geoip_cache, cache_key, (int)strlen(cache_key));
    if (cached != NULL) {
        cache_hits++;
        cached->hit_count++;
        return &cached->location;
    }

    cache_misses++;

    /* Convert IP to string for lookup */
    sa.sin_addr.s_addr = htonl(ipv4);
    inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof(ip_str));

    /* Perform MaxMind lookup */
    lookup_result = MMDB_lookup_string(&mmdb, ip_str, &gai_error, &mmdb_error);

    if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !lookup_result.found_entry) {
        lookup_failures++;
        return fallbackGeoIP(ipv4);
    }

    /* Allocate new cache entry */
    cached = (GeoIPCacheEntry_t *)XMALLOC(sizeof(GeoIPCacheEntry_t));
    if (cached == NULL) {
        lookup_failures++;
        return fallbackGeoIP(ipv4);
    }

    memset(cached, 0, sizeof(GeoIPCacheEntry_t));
    cached->ip = ipv4;
    cached->cached_time = time(NULL);
    cached->hit_count = 1;
    cached->location.valid = TRUE;

    /* Extract latitude */
    if (MMDB_get_value(&lookup_result.entry, &entry_data, "location", "latitude", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
        cached->location.latitude = (float)entry_data.double_value;
    }

    /* Extract longitude */
    if (MMDB_get_value(&lookup_result.entry, &entry_data, "location", "longitude", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
        cached->location.longitude = (float)entry_data.double_value;
    }

    /* Extract country code */
    if (MMDB_get_value(&lookup_result.entry, &entry_data, "country", "iso_code", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(cached->location.country_code, sizeof(cached->location.country_code),
                 "%.*s", (int)entry_data.data_size, entry_data.utf8_string);
    }

    /* Extract country name */
    if (MMDB_get_value(&lookup_result.entry, &entry_data, "country", "names", "en", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(cached->location.country_name, sizeof(cached->location.country_name),
                 "%.*s", (int)entry_data.data_size, entry_data.utf8_string);
    }

    /* Extract timezone */
    if (MMDB_get_value(&lookup_result.entry, &entry_data, "location", "time_zone", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(cached->location.timezone_name, sizeof(cached->location.timezone_name),
                 "%.*s", (int)entry_data.data_size, entry_data.utf8_string);
        cached->location.timezone_offset = parseTimezoneOffset(cached->location.timezone_name);
    } else {
        /* No timezone in database, use fallback */
        cached->location.timezone_offset = fallbackTimezoneFromIP(ipv4);
        snprintf(cached->location.timezone_name, sizeof(cached->location.timezone_name),
                 "UTC%+d", cached->location.timezone_offset);
    }

    /* Add to cache */
    if (addUniqueHashRec(geoip_cache, cache_key, (int)strlen(cache_key), cached) == NULL) {
        XFREE(cached);
        lookup_failures++;
        return fallbackGeoIP(ipv4);
    }

    lookup_success++;

#ifdef DEBUG
    if (config->debug >= 5) {
        fprintf(stderr, "DEBUG - GeoIP lookup: %s -> %s (UTC%+d) [%.2f, %.2f]\n",
                ip_str, cached->location.country_code, cached->location.timezone_offset,
                cached->location.latitude, cached->location.longitude);
    }
#endif

    return &cached->location;
}

/****
 * Get timezone offset for IP address
 ****/
int getTimezoneOffset(uint32_t ipv4)
{
    GeoLocation_t *loc = lookupGeoIP(ipv4);
    if (loc != NULL && loc->valid) {
        return loc->timezone_offset;
    }
    return 0;  /* Default to UTC */
}

/****
 * Get timezone label for offset
 ****/
const char *getTimezoneLabel(int offset)
{
    static char label[16];
    if (offset == 0) {
        return "UTC";
    }
    snprintf(label, sizeof(label), "UTC%+d", offset);
    return label;
}

/****
 * Fallback GeoIP when database not available or lookup fails
 * Uses simple heuristics based on IP address ranges
 ****/
GeoLocation_t *fallbackGeoIP(uint32_t ipv4)
{
    static GeoLocation_t fallback;

    memset(&fallback, 0, sizeof(fallback));
    fallback.valid = FALSE;
    fallback.timezone_offset = fallbackTimezoneFromIP(ipv4);
    snprintf(fallback.timezone_name, sizeof(fallback.timezone_name),
             "UTC%+d", fallback.timezone_offset);
    strncpy(fallback.country_code, "??", sizeof(fallback.country_code) - 1);
    fallback.country_code[sizeof(fallback.country_code) - 1] = '\0';
    strncpy(fallback.country_name, "Unknown", sizeof(fallback.country_name) - 1);
    fallback.country_name[sizeof(fallback.country_name) - 1] = '\0';

    return &fallback;
}

/****
 * Fallback timezone estimation from IP address structure
 * Very rough approximation based on RIR allocations
 ****/
int fallbackTimezoneFromIP(uint32_t ipv4)
{
    uint8_t first_octet = (uint8_t)((ipv4 >> 24) & 0xFF);

    /* Very rough approximations based on RIR allocations:
     * ARIN (North America): -5 to -8
     * RIPE (Europe): 0 to +2
     * APNIC (Asia-Pacific): +8 to +10
     * LACNIC (Latin America): -3 to -5
     * AFRINIC (Africa): 0 to +2
     */

    /* This is a placeholder - real allocation is much more complex */
    if (first_octet >= 1 && first_octet <= 126) {
        /* Mix of allocations */
        return (first_octet % 24) - 12;  /* Spread across timezones */
    }

    return 0;  /* Default to UTC */
}

/****
 * Clear GeoIP cache
 ****/
void clearGeoIPCache(void)
{
    if (geoip_cache != NULL) {
        freeHash(geoip_cache);
        geoip_cache = initHash(GEOIP_CACHE_SIZE_DEFAULT);
    }
    cache_hits = 0;
    cache_misses = 0;
}

/****
 * Print cache statistics
 ****/
void printGeoIPCacheStats(void)
{
    uint32_t total = cache_hits + cache_misses;
    float hit_rate = total > 0 ? (float)cache_hits / (float)total * 100.0f : 0.0f;

    fprintf(stderr, "\n=== GeoIP Cache Statistics ===\n");
    fprintf(stderr, "Cache hits:          %u\n", cache_hits);
    fprintf(stderr, "Cache misses:        %u\n", cache_misses);
    fprintf(stderr, "Hit rate:            %.2f%%\n", hit_rate);
    fprintf(stderr, "Lookup successes:    %u\n", lookup_success);
    fprintf(stderr, "Lookup failures:     %u\n", lookup_failures);
    if (geoip_cache != NULL) {
        fprintf(stderr, "Cached entries:      %u\n", geoip_cache->totalRecords);
    }
    fprintf(stderr, "==============================\n\n");
}

/****
 * Format IP address as string
 ****/
void formatIPAddress(uint32_t ipv4, char *buf, size_t buf_size)
{
    struct in_addr addr;
    addr.s_addr = htonl(ipv4);
    inet_ntop(AF_INET, &addr, buf, (socklen_t)buf_size);
}
