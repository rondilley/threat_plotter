/*****
 *
 * Description: Hilbert Curve Engine Implementation
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

#include "hilbert.h"
#include "mem.h"
#include "util.h"
#include <string.h>

/****
 *
 * local variables
 *
 ****/

PRIVATE int hilbert_initialized = FALSE;
PRIVATE HilbertConfig_t hilbert_config;

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
 * Validate Hilbert curve order
 ****/
int isValidOrder(uint8_t order)
{
    return (order >= HILBERT_ORDER_MIN && order <= HILBERT_ORDER_MAX);
}

/****
 * Get dimension for given order
 ****/
uint32_t getDimension(uint8_t order)
{
    if (!isValidOrder(order)) {
        return 0;
    }
    return (uint32_t)(1 << order);  /* 2^order */
}

/****
 * Get total points for given order
 ****/
uint64_t getTotalPoints(uint8_t order)
{
    if (!isValidOrder(order)) {
        return 0;
    }
    uint64_t dim = getDimension(order);
    return dim * dim;
}

/****
 * Initialize Hilbert curve engine
 ****/
int initHilbert(uint8_t order)
{
    if (!isValidOrder(order)) {
        fprintf(stderr, "ERR - Invalid Hilbert order: %u (must be %u-%u)\n",
                order, HILBERT_ORDER_MIN, HILBERT_ORDER_MAX);
        return FALSE;
    }

    hilbert_config.order = order;
    hilbert_config.dimension = getDimension(order);
    hilbert_config.total_points = getTotalPoints(order);

    hilbert_initialized = TRUE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Hilbert curve initialized: order=%u, dimension=%ux%u, points=%lu\n",
                hilbert_config.order, hilbert_config.dimension, hilbert_config.dimension,
                hilbert_config.total_points);
    }
#endif

    return TRUE;
}

/****
 * Deinitialize Hilbert curve engine
 ****/
void deInitHilbert(void)
{
    /* Report CIDR cache statistics before cleanup */
#ifdef DEBUG
    if (config->debug >= 1 && cidr_cache_initialized && (cidr_cache_hits + cidr_cache_misses) > 0) {
        uint32_t total = cidr_cache_hits + cidr_cache_misses;
        float hit_rate = (float)cidr_cache_hits / total * 100.0f;
        fprintf(stderr, "DEBUG - CIDR cache stats: hits=%u, misses=%u, hit_rate=%.2f%%\n",
                cidr_cache_hits, cidr_cache_misses, hit_rate);
    }
#endif

    /* Free CIDR mapping if allocated */
    freeCIDRMapping();

    hilbert_initialized = FALSE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Hilbert curve deinitialized\n");
    }
#endif
}

/****
 * Get current Hilbert configuration
 ****/
HilbertConfig_t *getHilbertConfig(void)
{
    if (!hilbert_initialized) {
        return NULL;
    }
    return &hilbert_config;
}

/****
 * MurmurHash3 32-bit implementation
 *
 * Fast hash function for distributing IPs across Hilbert curve
 ****/
uint32_t murmurhash3_32(const void *key, int len, uint32_t seed)
{
    const uint8_t *data = (const uint8_t *)key;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    /* Body */
    const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);
    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];

        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> (32 - 15));
        k1 *= c2;

        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> (32 - 13));
        h1 = h1 * 5 + 0xe6546b64;
    }

    /* Tail */
    const uint8_t *tail = (const uint8_t *)(data + nblocks * 4);
    uint32_t k1 = 0;

    switch (len & 3) {
    case 3:
        k1 ^= tail[2] << 16;
        /* fall through */
    case 2:
        k1 ^= tail[1] << 8;
        /* fall through */
    case 1:
        k1 ^= tail[0];
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> (32 - 15));
        k1 *= c2;
        h1 ^= k1;
    }

    /* Finalization */
    h1 ^= (uint32_t)len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;

    return h1;
}

/****
 * Rotate/flip a quadrant appropriately
 ****/
PRIVATE void rot(uint32_t n, uint32_t *x, uint32_t *y, uint32_t rx, uint32_t ry)
{
    if (ry == 0) {
        if (rx == 1) {
            *x = n - 1 - *x;
            *y = n - 1 - *y;
        }

        /* Swap x and y */
        uint32_t t = *x;
        *x = *y;
        *y = t;
    }
}

/****
 * Convert (x,y) to Hilbert curve index
 *
 * Algorithm from: https://en.wikipedia.org/wiki/Hilbert_curve
 ****/
uint64_t hilbertXYToIndex(uint32_t x, uint32_t y, uint8_t order)
{
    uint64_t d = 0;
    uint32_t n = getDimension(order);

    for (uint32_t s = n / 2; s > 0; s /= 2) {
        uint32_t rx = (x & s) > 0;
        uint32_t ry = (y & s) > 0;
        d += s * s * ((3 * rx) ^ ry);
        rot(s, &x, &y, rx, ry);
    }

    return d;
}

/****
 * Convert Hilbert curve index to (x,y) coordinates
 ****/
void hilbertIndexToXY(uint64_t index, uint8_t order, uint32_t *x, uint32_t *y)
{
    uint32_t n = getDimension(order);
    uint64_t d = index;
    *x = *y = 0;

    for (uint32_t s = 1; s < n; s *= 2) {
        uint32_t rx = 1 & (d / 2);
        uint32_t ry = 1 & (d ^ rx);
        rot(s, x, y, rx, ry);
        *x += s * rx;
        *y += s * ry;
        d /= 4;
    }
}

/****
 * Check if IPv4 address is non-routable (RFC1918, reserved, etc.)
 *
 * Returns TRUE for:
 * - RFC1918 private addresses (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16)
 * - Loopback (127.0.0.0/8)
 * - Link-local (169.254.0.0/16)
 * - Multicast (224.0.0.0/4)
 * - Reserved (240.0.0.0/4)
 * - Limited broadcast (255.255.255.255/32)
 * - Current network (0.0.0.0/8)
 * - Carrier-grade NAT (100.64.0.0/10)
 * - IPv6 to IPv4 relay (192.88.99.0/24)
 * - Documentation/TEST-NET (192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24)
 * - Benchmarking (198.18.0.0/15)
 ****/
int isNonRoutableIP(uint32_t ipv4)
{
    /* Extract octets (network byte order: big-endian) */
    uint8_t octet1 = (uint8_t)((ipv4 >> 24) & 0xFF);
    uint8_t octet2 = (uint8_t)((ipv4 >> 16) & 0xFF);
    uint8_t octet3 = (uint8_t)((ipv4 >> 8) & 0xFF);

    /* 0.0.0.0/8 - Current network (RFC 1122) */
    if (octet1 == 0) {
        return TRUE;
    }

    /* 10.0.0.0/8 - RFC1918 Private */
    if (octet1 == 10) {
        return TRUE;
    }

    /* 100.64.0.0/10 - Carrier-grade NAT (RFC 6598) */
    if (octet1 == 100 && (octet2 >= 64 && octet2 <= 127)) {
        return TRUE;
    }

    /* 127.0.0.0/8 - Loopback (RFC 1122) */
    if (octet1 == 127) {
        return TRUE;
    }

    /* 169.254.0.0/16 - Link-local (RFC 3927) */
    if (octet1 == 169 && octet2 == 254) {
        return TRUE;
    }

    /* 172.16.0.0/12 - RFC1918 Private */
    if (octet1 == 172 && (octet2 >= 16 && octet2 <= 31)) {
        return TRUE;
    }

    /* 192.0.0.0/24 - IETF Protocol Assignments (RFC 6890) */
    if (octet1 == 192 && octet2 == 0 && octet3 == 0) {
        return TRUE;
    }

    /* 192.0.2.0/24 - TEST-NET-1 Documentation (RFC 5737) */
    if (octet1 == 192 && octet2 == 0 && octet3 == 2) {
        return TRUE;
    }

    /* 192.88.99.0/24 - IPv6 to IPv4 relay (RFC 7526) */
    if (octet1 == 192 && octet2 == 88 && octet3 == 99) {
        return TRUE;
    }

    /* 192.168.0.0/16 - RFC1918 Private */
    if (octet1 == 192 && octet2 == 168) {
        return TRUE;
    }

    /* 198.18.0.0/15 - Benchmarking (RFC 2544) */
    if (octet1 == 198 && (octet2 == 18 || octet2 == 19)) {
        return TRUE;
    }

    /* 198.51.100.0/24 - TEST-NET-2 Documentation (RFC 5737) */
    if (octet1 == 198 && octet2 == 51 && octet3 == 100) {
        return TRUE;
    }

    /* 203.0.113.0/24 - TEST-NET-3 Documentation (RFC 5737) */
    if (octet1 == 203 && octet2 == 0 && octet3 == 113) {
        return TRUE;
    }

    /* 224.0.0.0/4 - Multicast (RFC 5771) */
    if (octet1 >= 224 && octet1 <= 239) {
        return TRUE;
    }

    /* 240.0.0.0/4 - Reserved (RFC 1112) */
    if (octet1 >= 240) {
        return TRUE;
    }

    return FALSE;
}

/****
 *
 * CIDR Mapping Functionality
 *
 ****/

typedef struct {
    uint32_t network;        /* Network address */
    uint32_t mask;           /* Pre-calculated bitmask for fast comparison */
    uint8_t prefix_len;      /* CIDR prefix length (for sorting/debugging) */
    int timezone_offset;     /* Timezone for this CIDR */
    uint32_t x_start;        /* Starting X coordinate */
    uint32_t x_end;          /* Ending X coordinate */
} CIDRMapEntry_t;

PRIVATE CIDRMapEntry_t *cidr_map = NULL;
PRIVATE uint32_t cidr_map_count = 0;
PRIVATE uint32_t cidr_map_capacity = 0;

/****
 * Comparison function for sorting CIDR entries
 * Sort by prefix length (longest first for most specific match), then by network address
 ****/
PRIVATE int compareCIDREntries(const void *a, const void *b)
{
    const CIDRMapEntry_t *entry_a = (const CIDRMapEntry_t *)a;
    const CIDRMapEntry_t *entry_b = (const CIDRMapEntry_t *)b;

    /* Sort by prefix length DESC (longer prefixes = more specific, checked first) */
    if (entry_a->prefix_len != entry_b->prefix_len) {
        return entry_b->prefix_len - entry_a->prefix_len;
    }

    /* Within same prefix length, sort by network address */
    if (entry_a->network < entry_b->network) {
        return -1;
    } else if (entry_a->network > entry_b->network) {
        return 1;
    }
    return 0;
}

/****
 * Load CIDR mapping file
 ****/
int loadCIDRMapping(const char *filename)
{
    FILE *fp;
    char line[256];
    uint32_t line_num = 0;
    uint32_t entries_loaded = 0;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERR - Cannot open CIDR mapping file: %s\n", filename);
        return FALSE;
    }

    /* Allocate initial capacity */
    cidr_map_capacity = 4096;
    cidr_map = (CIDRMapEntry_t *)XMALLOC((int)(sizeof(CIDRMapEntry_t) * cidr_map_capacity));
    if (cidr_map == NULL) {
        fprintf(stderr, "ERR - Cannot allocate CIDR mapping array\n");
        fclose(fp);
        return FALSE;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        /* Parse: NETWORK/PREFIX TIMEZONE X_START X_END */
        uint32_t oct1, oct2, oct3, oct4, prefix;
        int tz;
        uint32_t x_start, x_end;

        int parsed = sscanf(line, "%u.%u.%u.%u/%u %d %u %u",
                           &oct1, &oct2, &oct3, &oct4, &prefix,
                           &tz, &x_start, &x_end);

        if (parsed != 8) {
            fprintf(stderr, "WARN - Invalid CIDR mapping line %u: %s", line_num, line);
            continue;
        }

        /* Grow array if needed */
        if (cidr_map_count >= cidr_map_capacity) {
            uint32_t new_capacity;
            size_t new_size;

            /* Check for overflow before doubling */
            if (cidr_map_capacity > UINT32_MAX / 2) {
                fprintf(stderr, "ERR - CIDR map capacity exceeds maximum\n");
                fclose(fp);
                return FALSE;
            }

            new_capacity = cidr_map_capacity * 2;

            /* Check for overflow in size calculation */
            new_size = sizeof(CIDRMapEntry_t) * new_capacity;
            if (new_size / sizeof(CIDRMapEntry_t) != new_capacity) {
                fprintf(stderr, "ERR - CIDR map size calculation overflow\n");
                fclose(fp);
                return FALSE;
            }

            cidr_map_capacity = new_capacity;
            cidr_map = (CIDRMapEntry_t *)XREALLOC(cidr_map, (int)new_size);
            if (cidr_map == NULL) {
                fprintf(stderr, "ERR - Cannot grow CIDR mapping array\n");
                fclose(fp);
                return FALSE;
            }
        }

        /* Store entry with pre-calculated bitmask for fast lookups */
        cidr_map[cidr_map_count].network = (oct1 << 24) | (oct2 << 16) | (oct3 << 8) | oct4;
        cidr_map[cidr_map_count].prefix_len = (uint8_t)prefix;
        /* Pre-calculate bitmask: /24 = 0xFFFFFF00, /16 = 0xFFFF0000, etc. */
        cidr_map[cidr_map_count].mask = (prefix == 0) ? 0 : ~((1U << (32 - prefix)) - 1);
        cidr_map[cidr_map_count].timezone_offset = tz;
        cidr_map[cidr_map_count].x_start = x_start;
        cidr_map[cidr_map_count].x_end = x_end;
        cidr_map_count++;
        entries_loaded++;
    }

    fclose(fp);

    /* PERFORMANCE OPTIMIZATION: Sort CIDR array by prefix length (DESC) and network
     * This allows faster lookups by checking most specific matches first
     * Also enables future binary search optimization
     */
    if (cidr_map_count > 0) {
        qsort(cidr_map, cidr_map_count, sizeof(CIDRMapEntry_t), compareCIDREntries);
    }

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - CIDR mapping loaded and sorted: %u entries from %s\n",
                entries_loaded, filename);
    }
#endif

    return TRUE;
}

/****
 * Free CIDR mapping
 ****/
void freeCIDRMapping(void)
{
    if (cidr_map != NULL) {
        XFREE(cidr_map);
        cidr_map = NULL;
    }
    cidr_map_count = 0;
    cidr_map_capacity = 0;
}

/****
 * Simple LRU cache for IP->CIDR lookups
 * Attack traffic often comes in bursts from same IPs, so caching helps significantly
 ****/
#define CIDR_CACHE_SIZE 256  /* Power of 2 for fast modulo */
typedef struct {
    uint32_t ip;
    CIDRMapEntry_t *entry;
    uint32_t access_count;
} CIDRCacheEntry_t;

PRIVATE CIDRCacheEntry_t cidr_cache[CIDR_CACHE_SIZE];
PRIVATE int cidr_cache_initialized = FALSE;
PRIVATE uint32_t cidr_cache_hits = 0;
PRIVATE uint32_t cidr_cache_misses = 0;

/****
 * Find CIDR mapping for IP address (optimized with cache)
 * PERFORMANCE: Now searches sorted array (most specific first) with LRU cache
 ****/
PRIVATE CIDRMapEntry_t *findCIDRMapping(uint32_t ipv4)
{
    uint32_t cache_idx;
    CIDRMapEntry_t *result;

    /* Initialize cache on first use */
    if (!cidr_cache_initialized) {
        memset(cidr_cache, 0, sizeof(cidr_cache));
        cidr_cache_initialized = TRUE;
    }

    /* Check cache first - use IP's lower bits for fast indexing */
    cache_idx = ipv4 & (CIDR_CACHE_SIZE - 1);

    if (cidr_cache[cache_idx].ip == ipv4 && cidr_cache[cache_idx].entry != NULL) {
        cidr_cache[cache_idx].access_count++;
        cidr_cache_hits++;
        return cidr_cache[cache_idx].entry;
    }

    cidr_cache_misses++;

    /* Cache miss - search sorted array using pre-calculated masks
     * Since array is sorted by prefix length (longest first), we find
     * the most specific match first, which is correct for CIDR semantics
     * PERFORMANCE: Using pre-calculated masks (boolean AND) is much faster
     * than recalculating masks with bit shifts for every comparison
     */
    result = NULL;
    for (uint32_t i = 0; i < cidr_map_count; i++) {
        if ((ipv4 & cidr_map[i].mask) == cidr_map[i].network) {
            result = &cidr_map[i];
            break;  /* Found most specific match, stop searching */
        }
    }

    /* Update cache with new result (even if NULL to avoid repeated misses) */
    cidr_cache[cache_idx].ip = ipv4;
    cidr_cache[cache_idx].entry = result;
    cidr_cache[cache_idx].access_count = 1;

    return result;
}

/****
 * Map IPv4 address to Hilbert curve index using CIDR mapping
 ****/
uint64_t ipToHilbertIndex(uint32_t ipv4, uint8_t order)
{
    HilbertCoord_t coord = ipToHilbert(ipv4, order);
    return hilbertXYToIndex(coord.x, coord.y, order);
}

/****
 * Map IPv4 address to Hilbert curve coordinates using CIDR mapping
 ****/
HilbertCoord_t ipToHilbert(uint32_t ipv4, uint8_t order)
{
    HilbertCoord_t coord;
    uint32_t dimension = getDimension(order);

    /* If CIDR mapping is loaded, use it */
    if (cidr_map != NULL && cidr_map_count > 0) {
        CIDRMapEntry_t *entry = findCIDRMapping(ipv4);

        if (entry != NULL) {
            /* Found mapping - use timezone band with CIDR clustering */
            uint32_t tz_band_width = entry->x_end - entry->x_start;

            if (tz_band_width == 0) {
                tz_band_width = 1;
            }

            /* Extract IP octets for clustering */
            uint8_t oct1 = (uint8_t)((ipv4 >> 24) & 0xFF);
            uint8_t oct2 = (ipv4 >> 16) & 0xFF;
            uint8_t oct3 = (ipv4 >> 8) & 0xFF;
            uint8_t oct4 = ipv4 & 0xFF;

            /* Use /16 network for coarse positioning within timezone band */
            uint32_t network_16 = (oct1 << 8) | oct2;  /* First 16 bits */

            /* X position within timezone band based on /16 network */
            uint32_t x_offset = (network_16 * tz_band_width) / 65536;
            coord.x = entry->x_start + x_offset;

            /* Ensure within bounds */
            if (coord.x >= entry->x_end) {
                coord.x = entry->x_end > 0 ? entry->x_end - 1 : 0;
            }

            /* Y position based on full IP for vertical clustering */
            /* Spread across full Y dimension for vertical distribution */
            uint32_t ip_hash = (oct3 << 8) | oct4;  /* Last 16 bits */
            coord.y = (ip_hash * dimension) / 65536;

            coord.order = order;

#ifdef DEBUG
            if (config->debug >= 5) {
                fprintf(stderr, "DEBUG - IP %u.%u.%u.%u -> TZ=%+d, X=%u (band:%u-%u), Y=%u\n",
                        oct1, oct2, oct3, oct4, entry->timezone_offset,
                        coord.x, entry->x_start, entry->x_end, coord.y);
            }
#endif

            return coord;
        }
    }

    /* Direct Hilbert mapping preserves spatial locality for CIDR clustering
     * Scale the FULL 32-bit IP proportionally to the Hilbert curve space
     * This ensures adjacent IPs map to nearby coordinates, making CIDR blocks visible
     *
     * CRITICAL: Never discard bits via shifting - always use the full IP value
     * For order 12: 2^24 points vs 2^32 IPs = 256:1 ratio
     * Scaling formula: index = (ip * total_points) / 2^32
     */
    uint64_t total_points = getTotalPoints(order);
    uint64_t index;

    /* Scale 32-bit IP space proportionally across Hilbert curve
     * Using 64-bit arithmetic to avoid overflow:
     * index = (ipv4 * total_points) >> 32
     *
     * This distributes ALL 32 bits across the curve space:
     * - IP 0.0.0.0 (0x00000000) → index 0
     * - IP 255.255.255.255 (0xFFFFFFFF) → index (total_points - 1)
     * - Adjacent IPs map to nearby indices (Hilbert preserves locality)
     */
    index = ((uint64_t)ipv4 * total_points) >> 32;

    /* Ensure index is within bounds (should always be true with above formula) */
    if (index >= total_points) {
        index = total_points - 1;
    }

    hilbertIndexToXY(index, order, &coord.x, &coord.y);
    coord.order = order;

    return coord;
}
