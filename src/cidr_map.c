/*****
 *
 * Description: CIDR Allocation Mapper - Pre-computes IP to coordinate mapping
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
 * Standalone tool to scan IPv4 address space and generate CIDR-to-coordinate mapping
 *
 * This tool:
 * 1. Scans IPv4 space in /8 blocks (sampling representative IPs)
 * 2. Uses GeoIP to determine timezone for each block
 * 3. Counts /24 blocks per timezone
 * 4. Calculates proportional X-axis allocation based on density
 * 5. Generates mapping file: cidr_map.txt
 *
 ****/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <maxminddb.h>
#include <arpa/inet.h>

/****
 * Constants
 ****/

#define TIMEZONE_COUNT 27  /* UTC-12 to UTC+14 */
#define TIMEZONE_MIN -12
#define TIMEZONE_MAX 14

/* Scan granularity */
#define SCAN_BLOCK_SIZE 16  /* Sample every /16 block */

/****
 * Structures
 ****/

typedef struct {
    int timezone_offset;     /* UTC offset (-12 to +14) */
    uint32_t block_count;    /* Number of /24 blocks in this timezone */
    uint32_t x_start;        /* Starting X coordinate */
    uint32_t x_end;          /* Ending X coordinate */
} TimezoneStats_t;

typedef struct {
    uint32_t network;        /* Network address (CIDR base) */
    uint8_t prefix_len;      /* CIDR prefix length */
    int timezone_offset;     /* Timezone for this CIDR */
} CIDRMapping_t;

/****
 * Global variables
 ****/

static MMDB_s mmdb;
static TimezoneStats_t timezone_stats[TIMEZONE_COUNT];
static CIDRMapping_t *cidr_mappings = NULL;
static uint32_t cidr_mapping_count = 0;
static uint32_t cidr_mapping_capacity = 0;

/****
 *
 * Extract UTC offset from IANA timezone name
 *
 * DESCRIPTION:
 *   Maps IANA timezone strings to UTC hour offsets using common timezone prefixes.
 *
 * PARAMETERS:
 *   tz_name - IANA timezone string (e.g., "America/New_York")
 *
 * RETURNS:
 *   UTC offset in hours (-12 to +14), or 0 (UTC) if unknown
 *
 ****/
int parseTimezoneOffset(const char *tz_name)
{
    if (tz_name == NULL) return 0;

    /* Common timezone mappings */
    struct { const char *prefix; int offset; } tz_map[] = {
        {"Pacific/Midway", -11}, {"Pacific/Honolulu", -10},
        {"America/Anchorage", -9}, {"America/Los_Angeles", -8},
        {"America/Denver", -7}, {"America/Chicago", -6},
        {"America/New_York", -5}, {"America/Halifax", -4},
        {"America/St_Johns", -3}, {"America/Sao_Paulo", -3},
        {"Atlantic/South_Georgia", -2}, {"Atlantic/Azores", -1},
        {"Europe/London", 0}, {"Europe/Paris", 1}, {"Europe/Athens", 2},
        {"Europe/Moscow", 3}, {"Asia/Dubai", 4}, {"Asia/Karachi", 5},
        {"Asia/Dhaka", 6}, {"Asia/Bangkok", 7}, {"Asia/Shanghai", 8},
        {"Asia/Tokyo", 9}, {"Australia/Sydney", 10}, {"Pacific/Noumea", 11},
        {"Pacific/Auckland", 12},
        {NULL, 0}
    };

    for (int i = 0; tz_map[i].prefix != NULL; i++) {
        if (strstr(tz_name, tz_map[i].prefix) == tz_name) {
            return tz_map[i].offset;
        }
    }

    return 0;  /* Default to UTC */
}

/****
 *
 * Determine timezone for IP address via GeoIP
 *
 * DESCRIPTION:
 *   Queries MaxMind database for IP location and extracts timezone. Falls back to
 *   heuristic estimation if lookup fails.
 *
 * PARAMETERS:
 *   ipv4 - IPv4 address in host byte order
 *
 * RETURNS:
 *   UTC offset in hours (-12 to +14)
 *
 ****/
int lookupTimezone(uint32_t ipv4)
{
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr;
    MMDB_lookup_result_s result;
    MMDB_entry_data_s entry_data;
    int gai_error, mmdb_error;

    /* Convert IP to string */
    addr.s_addr = htonl(ipv4);
    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

    /* Lookup in database */
    result = MMDB_lookup_string(&mmdb, ip_str, &gai_error, &mmdb_error);

    if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !result.found_entry) {
        /* Fallback: rough estimation based on first octet */
        uint8_t first_octet = (ipv4 >> 24) & 0xFF;
        return (first_octet % 24) - 12;  /* Spread across timezones */
    }

    /* Extract timezone */
    if (MMDB_get_value(&result.entry, &entry_data, "location", "time_zone", NULL) == MMDB_SUCCESS &&
        entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        char tz_name[64];
        snprintf(tz_name, sizeof(tz_name), "%.*s",
                 (int)entry_data.data_size, entry_data.utf8_string);
        return parseTimezoneOffset(tz_name);
    }

    return 0;  /* Default to UTC */
}

/****
 *
 * Add CIDR block to mapping table
 *
 * DESCRIPTION:
 *   Appends CIDR-to-timezone mapping to global array, growing array as needed.
 *
 * PARAMETERS:
 *   network - Network address
 *   prefix_len - CIDR prefix length
 *   timezone_offset - UTC offset for this block
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   May reallocate cidr_mappings array
 *
 ****/
void addCIDRMapping(uint32_t network, uint8_t prefix_len, int timezone_offset)
{
    /* Grow array if needed */
    if (cidr_mapping_count >= cidr_mapping_capacity) {
        cidr_mapping_capacity = cidr_mapping_capacity == 0 ? 65536 : cidr_mapping_capacity * 2;
        cidr_mappings = realloc(cidr_mappings, cidr_mapping_capacity * sizeof(CIDRMapping_t));
        if (cidr_mappings == NULL) {
            fprintf(stderr, "ERR - Failed to allocate CIDR mapping array\n");
            exit(1);
        }
    }

    cidr_mappings[cidr_mapping_count].network = network;
    cidr_mappings[cidr_mapping_count].prefix_len = prefix_len;
    cidr_mappings[cidr_mapping_count].timezone_offset = timezone_offset;
    cidr_mapping_count++;
}

/****
 *
 * Sample IPv4 space to build timezone distribution
 *
 * DESCRIPTION:
 *   Samples every /16 block in routable IPv4 space, queries GeoIP timezone for each,
 *   and accumulates block counts per timezone.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Populates timezone_stats array and cidr_mappings
 *
 ****/
void scanIPv4Space(void)
{
    uint32_t total_blocks = 0;
    uint32_t blocks_scanned = 0;

    fprintf(stderr, "Scanning IPv4 address space...\n");
    fprintf(stderr, "This will sample every /16 block to determine timezone allocation\n\n");

    /* Initialize timezone stats */
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        timezone_stats[i].timezone_offset = TIMEZONE_MIN + i;
        timezone_stats[i].block_count = 0;
        timezone_stats[i].x_start = 0;
        timezone_stats[i].x_end = 0;
    }

    /* Scan every /16 block (sampling middle IP) */
    for (uint32_t octet1 = 0; octet1 < 256; octet1++) {
        fprintf(stderr, "\rScanning %u.x.x.x...", octet1);
        fflush(stderr);

        for (uint32_t octet2 = 0; octet2 < 256; octet2 += SCAN_BLOCK_SIZE) {
            /* Sample IP from middle of /16 block */
            uint32_t sample_ip = (octet1 << 24) | (octet2 << 16) | (128 << 8) | 128;

            /* Skip non-routable addresses */
            if (octet1 == 0 || octet1 == 10 || octet1 == 127 ||
                (octet1 == 172 && octet2 >= 16 && octet2 <= 31) ||
                (octet1 == 192 && octet2 == 168) ||
                octet1 >= 224) {
                continue;
            }

            /* Lookup timezone */
            int tz = lookupTimezone(sample_ip);

            /* Clamp to valid range */
            if (tz < TIMEZONE_MIN) tz = TIMEZONE_MIN;
            if (tz > TIMEZONE_MAX) tz = TIMEZONE_MAX;

            int tz_index = tz - TIMEZONE_MIN;

            /* Count /24 blocks in this /16 */
            uint32_t blocks_in_16 = 256;  /* 256 /24 blocks in a /16 */
            timezone_stats[tz_index].block_count += blocks_in_16;
            total_blocks += blocks_in_16;
            blocks_scanned++;

            /* Store CIDR mapping */
            uint32_t network = (octet1 << 24) | (octet2 << 16);
            addCIDRMapping(network, 16, tz);
        }
    }

    fprintf(stderr, "\n\nScan complete:\n");
    fprintf(stderr, "  Blocks scanned: %u /16 blocks\n", blocks_scanned);
    fprintf(stderr, "  Total /24 blocks: %u\n", total_blocks);
    fprintf(stderr, "  CIDR mappings: %u\n\n", cidr_mapping_count);
}

/****
 *
 * Compute X-axis allocation based on timezone density
 *
 * DESCRIPTION:
 *   Allocates X coordinates proportionally to number of IP blocks in each timezone,
 *   ensuring dense timezones get more horizontal space.
 *
 * PARAMETERS:
 *   hilbert_dimension - Width of visualization space
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Sets x_start and x_end for each timezone in timezone_stats
 *
 ****/
void calculateProportionalAllocation(uint32_t hilbert_dimension)
{
    uint32_t total_blocks = 0;

    /* Calculate total blocks */
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        total_blocks += timezone_stats[i].block_count;
    }

    if (total_blocks == 0) {
        fprintf(stderr, "ERR - No blocks found!\n");
        exit(1);
    }

    fprintf(stderr, "Calculating proportional X-axis allocation:\n\n");
    fprintf(stderr, "%-10s %12s %10s %12s %12s\n",
            "Timezone", "Blocks", "Percent", "X Start", "X End");
    fprintf(stderr, "%-10s %12s %10s %12s %12s\n",
            "--------", "--------", "-------", "-------", "-----");

    uint32_t x_pos = 0;
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        if (timezone_stats[i].block_count == 0) {
            timezone_stats[i].x_start = x_pos;
            timezone_stats[i].x_end = x_pos;
            continue;
        }

        /* Calculate proportional width */
        float percent = (float)timezone_stats[i].block_count / total_blocks * 100.0f;
        uint32_t width = (uint32_t)((float)hilbert_dimension * timezone_stats[i].block_count / total_blocks);

        /* Ensure minimum width of 1 for non-empty timezones */
        if (width == 0 && timezone_stats[i].block_count > 0) {
            width = 1;
        }

        timezone_stats[i].x_start = x_pos;
        timezone_stats[i].x_end = x_pos + width;
        x_pos += width;

        fprintf(stderr, "UTC%+3d    %12u %9.2f%% %12u %12u\n",
                timezone_stats[i].timezone_offset,
                timezone_stats[i].block_count,
                percent,
                timezone_stats[i].x_start,
                timezone_stats[i].x_end);
    }

    fprintf(stderr, "\n");
}

/****
 *
 * Generate CIDR mapping output file
 *
 * DESCRIPTION:
 *   Writes CIDR-to-coordinate mapping file with timezone allocation summary and
 *   per-block mappings.
 *
 * PARAMETERS:
 *   filename - Output file path
 *   hilbert_dimension - Visualization dimension
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Creates mapping file on disk
 *
 ****/
void writeMappingFile(const char *filename, uint32_t hilbert_dimension)
{
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERR - Cannot create mapping file: %s\n", filename);
        exit(1);
    }

    fprintf(fp, "# CIDR to Coordinate Mapping File\n");
    fprintf(fp, "# Generated: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# Hilbert dimension: %u\n", hilbert_dimension);
    fprintf(fp, "#\n");
    fprintf(fp, "# Format: NETWORK/PREFIX TIMEZONE X_START X_END\n");
    fprintf(fp, "#\n\n");

    /* Write timezone allocation summary */
    fprintf(fp, "# Timezone X-axis Allocation:\n");
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        if (timezone_stats[i].block_count > 0) {
            fprintf(fp, "# UTC%+3d: X[%u-%u] (%u blocks)\n",
                    timezone_stats[i].timezone_offset,
                    timezone_stats[i].x_start,
                    timezone_stats[i].x_end,
                    timezone_stats[i].block_count);
        }
    }
    fprintf(fp, "\n");

    /* Write CIDR mappings */
    for (uint32_t i = 0; i < cidr_mapping_count; i++) {
        uint32_t network = cidr_mappings[i].network;
        uint8_t prefix = cidr_mappings[i].prefix_len;
        int tz = cidr_mappings[i].timezone_offset;
        int tz_index = tz - TIMEZONE_MIN;

        fprintf(fp, "%u.%u.%u.%u/%u %d %u %u\n",
                (network >> 24) & 0xFF,
                (network >> 16) & 0xFF,
                (network >> 8) & 0xFF,
                network & 0xFF,
                prefix,
                tz,
                timezone_stats[tz_index].x_start,
                timezone_stats[tz_index].x_end);
    }

    fclose(fp);
    fprintf(stderr, "Mapping file written: %s (%u entries)\n", filename, cidr_mapping_count);
}

/****
 * Main
 ****/
int main(int argc, char **argv)
{
    const char *geoip_db = argc > 1 ? argv[1] : "GeoLite2-City.mmdb";
    const char *output_file = argc > 2 ? argv[2] : "cidr_map.txt";
    uint32_t hilbert_dimension = argc > 3 ? atoi(argv[3]) : 4096;  /* Default: order 12 */
    int status;

    fprintf(stderr, "\n");
    fprintf(stderr, "CIDR Allocation Mapper\n");
    fprintf(stderr, "======================\n\n");
    fprintf(stderr, "GeoIP database:      %s\n", geoip_db);
    fprintf(stderr, "Output file:         %s\n", output_file);
    fprintf(stderr, "Hilbert dimension:   %u (%ux%u)\n\n",
            hilbert_dimension, hilbert_dimension, hilbert_dimension);

    /* Open GeoIP database */
    status = MMDB_open(geoip_db, MMDB_MODE_MMAP, &mmdb);
    if (status != MMDB_SUCCESS) {
        fprintf(stderr, "ERR - Cannot open GeoIP database: %s\n", MMDB_strerror(status));
        return 1;
    }

    /* Scan IPv4 space */
    scanIPv4Space();

    /* Calculate proportional allocation */
    calculateProportionalAllocation(hilbert_dimension);

    /* Write mapping file */
    writeMappingFile(output_file, hilbert_dimension);

    /* Cleanup */
    MMDB_close(&mmdb);
    free(cidr_mappings);

    fprintf(stderr, "\nDone!\n\n");
    return 0;
}
