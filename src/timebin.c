/*****
 *
 * Description: Time Binning System Implementation
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

#include "timebin.h"
#include "mem.h"
#include "util.h"
#include <string.h>
#include <strings.h>

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
 * Parse time bin duration string (e.g., "1m", "5m", "60m")
 ****/
int parseTimeBinDuration(const char *str, uint32_t *seconds)
{
    char *endptr;
    long value;

    if (!str || !seconds) {
        return FALSE;
    }

    value = strtol(str, &endptr, 10);
    if (value <= 0) {
        return FALSE;
    }

    /* Check suffix */
    if (*endptr == 'm' || *endptr == 'M') {
        *seconds = (uint32_t)(value * 60);
    } else if (*endptr == 's' || *endptr == 'S' || *endptr == '\0') {
        *seconds = (uint32_t)value;
    } else if (*endptr == 'h' || *endptr == 'H') {
        *seconds = (uint32_t)(value * 3600);
    } else {
        return FALSE;
    }

    return TRUE;
}

/****
 * Format time bin duration as string
 ****/
const char *formatTimeBinDuration(uint32_t seconds)
{
    static char buf[32];

    if (seconds % 3600 == 0) {
        snprintf(buf, sizeof(buf), "%uh", seconds / 3600);
    } else if (seconds % 60 == 0) {
        snprintf(buf, sizeof(buf), "%um", seconds / 60);
    } else {
        snprintf(buf, sizeof(buf), "%us", seconds);
    }

    return buf;
}

/****
 * Get bin start time for a given event time
 ****/
time_t getBinForTime(time_t event_time, uint32_t bin_seconds)
{
    return (event_time / bin_seconds) * bin_seconds;
}

/****
 * Create a new time bin
 ****/
TimeBin_t *createTimeBin(time_t start_time, uint32_t bin_seconds, uint32_t dimension)
{
    TimeBin_t *bin;
    size_t heatmap_size;

    bin = (TimeBin_t *)XMALLOC(sizeof(TimeBin_t));
    if (!bin) {
        return NULL;
    }

    memset(bin, 0, sizeof(TimeBin_t));

    bin->bin_start = start_time;
    bin->bin_end = start_time + bin_seconds;
    bin->dimension = dimension;

    /* Allocate heatmap array */
    heatmap_size = dimension * dimension * sizeof(uint32_t);
    bin->heatmap = (uint32_t *)XMALLOC((int)heatmap_size);
    if (!bin->heatmap) {
        XFREE(bin);
        return NULL;
    }

    memset(bin->heatmap, 0, heatmap_size);

#ifdef DEBUG
    if (config->debug >= 2) {
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&start_time));
        fprintf(stderr, "DEBUG - Created time bin: %s (%ux%u)\n",
                time_str, dimension, dimension);
    }
#endif

    return bin;
}

/****
 * Destroy a time bin
 ****/
void destroyTimeBin(TimeBin_t *bin)
{
    if (!bin) {
        return;
    }

    if (bin->heatmap) {
        XFREE(bin->heatmap);
    }

    XFREE(bin);
}

/****
 * Reset a time bin for reuse
 ****/
void resetTimeBin(TimeBin_t *bin)
{
    if (!bin) {
        return;
    }

    size_t heatmap_size = bin->dimension * bin->dimension * sizeof(uint32_t);
    memset(bin->heatmap, 0, heatmap_size);

    bin->event_count = 0;
    bin->unique_ips = 0;
    bin->max_intensity = 0;
}

/****
 * Add event to time bin at specific coordinates
 ****/
int addEventToBin(TimeBin_t *bin, uint32_t x, uint32_t y)
{
    uint32_t idx;

    if (!bin || !bin->heatmap) {
        return FALSE;
    }

    /* Check bounds */
    if (x >= bin->dimension || y >= bin->dimension) {
        return FALSE;
    }

    /* Calculate index into heatmap */
    idx = y * bin->dimension + x;

    /* Increment hit count */
    bin->heatmap[idx]++;

    /* Update statistics */
    bin->event_count++;
    if (bin->heatmap[idx] > bin->max_intensity) {
        bin->max_intensity = bin->heatmap[idx];
    }

    return TRUE;
}

/****
 * Finalize bin (compute final statistics)
 ****/
int finalizeBin(TimeBin_t *bin)
{
    uint32_t i, total_points;

    if (!bin || !bin->heatmap) {
        return FALSE;
    }

    /* Count unique IP locations (non-zero cells) */
    total_points = bin->dimension * bin->dimension;
    bin->unique_ips = 0;

    for (i = 0; i < total_points; i++) {
        if (bin->heatmap[i] > 0) {
            bin->unique_ips++;
        }
    }

#ifdef DEBUG
    if (config->debug >= 1) {
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&bin->bin_start));
        fprintf(stderr, "DEBUG - Finalized bin %s: events=%u, unique_ips=%u, max_intensity=%u\n",
                time_str, bin->event_count, bin->unique_ips, bin->max_intensity);
    }
#endif

    return TRUE;
}

/****
 * Create time bin manager
 ****/
TimeBinManager_t *createTimeBinManager(TimeBinConfig_t *config_in)
{
    TimeBinManager_t *manager;

    if (!config_in) {
        return NULL;
    }

    manager = (TimeBinManager_t *)XMALLOC(sizeof(TimeBinManager_t));
    if (!manager) {
        return NULL;
    }

    memset(manager, 0, sizeof(TimeBinManager_t));
    memcpy(&manager->config, config_in, sizeof(TimeBinConfig_t));

    manager->current_bin = NULL;
    manager->next_bin_start = 0;
    manager->total_bins = 0;
    manager->bins_written = 0;

    /* Initialize decay cache */
    manager->cache_capacity = DECAY_CACHE_MAX_ENTRIES;
    manager->cache_size = 0;
    manager->decay_cache = (DecayCacheEntry_t *)XMALLOC(
        (int)(sizeof(DecayCacheEntry_t) * manager->cache_capacity));

    if (!manager->decay_cache) {
        XFREE(manager);
        return NULL;
    }

    memset(manager->decay_cache, 0,
           sizeof(DecayCacheEntry_t) * manager->cache_capacity);

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Created time bin manager: bin_size=%s, order=%u, decay=%us\n",
                formatTimeBinDuration(config_in->bin_seconds),
                config_in->hilbert_order,
                config_in->decay_seconds);
    }
#endif

    return manager;
}

/****
 * Destroy time bin manager
 ****/
void destroyTimeBinManager(TimeBinManager_t *manager)
{
    if (!manager) {
        return;
    }

    if (manager->current_bin) {
        destroyTimeBin(manager->current_bin);
    }

    if (manager->decay_cache) {
        XFREE(manager->decay_cache);
    }

    XFREE(manager);
}

/****
 * Process an event - add to current bin or create new bin
 ****/
int processEvent(TimeBinManager_t *manager, time_t event_time, uint32_t x, uint32_t y)
{
    time_t bin_start;

    if (!manager) {
        return FALSE;
    }

    /* Calculate which bin this event belongs to */
    bin_start = getBinForTime(event_time, manager->config.bin_seconds);

    /* Check if we need a new bin */
    if (!manager->current_bin || bin_start != manager->current_bin->bin_start) {
        /* Finalize and write current bin if it exists */
        if (manager->current_bin) {
            finalizeBin(manager->current_bin);
            /* TODO: Write bin to output file */
            manager->bins_written++;
            destroyTimeBin(manager->current_bin);
        }

        /* Create new bin */
        manager->current_bin = createTimeBin(bin_start, manager->config.bin_seconds,
                                            manager->config.dimension);
        if (!manager->current_bin) {
            return FALSE;
        }

        manager->total_bins++;
    }

    /* Update decay cache with this coordinate */
    updateDecayCache(manager, x, y, event_time, 1);

    /* Add event to current bin */
    return addEventToBin(manager->current_bin, x, y);
}

/****
 * Update decay cache with coordinate activity
 ****/
int updateDecayCache(TimeBinManager_t *manager, uint32_t x, uint32_t y,
                     time_t event_time, uint32_t intensity)
{
    uint32_t coord_key, i;
    int found = FALSE;

    if (!manager || !manager->decay_cache) {
        return FALSE;
    }

    /* Create coordinate key: combine x and y into single uint32_t */
    coord_key = (x << 16) | y;

    /* Search for existing entry */
    for (i = 0; i < manager->cache_size; i++) {
        if (manager->decay_cache[i].coord_key == coord_key) {
            /* Update existing entry */
            manager->decay_cache[i].last_seen = event_time;
            manager->decay_cache[i].intensity += intensity;
            found = TRUE;
            break;
        }
    }

    /* Add new entry if not found and space available */
    if (!found && manager->cache_size < manager->cache_capacity) {
        manager->decay_cache[manager->cache_size].coord_key = coord_key;
        manager->decay_cache[manager->cache_size].last_seen = event_time;
        manager->decay_cache[manager->cache_size].intensity = intensity;
        manager->cache_size++;
    }

    return TRUE;
}

/****
 * Apply decay cache to heatmap - show fading IPs
 ****/
void applyDecayToHeatmap(TimeBinManager_t *manager, TimeBin_t *bin)
{
    uint32_t i, x, y, idx;
    time_t age;
    float decay_factor;

    if (!manager || !bin || !manager->decay_cache) {
        return;
    }

    /* Apply each cached coordinate to the heatmap with decay */
    for (i = 0; i < manager->cache_size; i++) {
        /* Calculate age of this coordinate */
        age = bin->bin_start - manager->decay_cache[i].last_seen;

        /* Skip if too old (beyond decay period) */
        if (age > (time_t)manager->config.decay_seconds || age < 0) {
            continue;
        }

        /* Calculate decay factor (1.0 = fresh, 0.0 = fully decayed) */
        decay_factor = 1.0f - ((float)age / (float)manager->config.decay_seconds);

        /* Extract x,y from coord_key */
        x = (manager->decay_cache[i].coord_key >> 16) & 0xFFFF;
        y = manager->decay_cache[i].coord_key & 0xFFFF;

        /* Check bounds */
        if (x >= bin->dimension || y >= bin->dimension) {
            continue;
        }

        /* Calculate index and add decayed intensity */
        idx = y * bin->dimension + x;

        /* Add decayed intensity to heatmap (minimum 1 to keep visible) */
        uint32_t decayed_intensity = (uint32_t)(
            (float)manager->decay_cache[i].intensity * decay_factor);

        if (decayed_intensity < 1 && decay_factor > 0.0f) {
            decayed_intensity = 1;  /* Keep at least 1 for visibility */
        }

        bin->heatmap[idx] += decayed_intensity;

        /* Update max intensity if needed */
        if (bin->heatmap[idx] > bin->max_intensity) {
            bin->max_intensity = bin->heatmap[idx];
        }
    }
}

/****
 * Clean expired entries from decay cache
 ****/
void cleanExpiredCacheEntries(TimeBinManager_t *manager, time_t current_time)
{
    uint32_t i, write_idx;

    if (!manager || !manager->decay_cache) {
        return;
    }

    write_idx = 0;

    /* Compact array by removing expired entries */
    for (i = 0; i < manager->cache_size; i++) {
        time_t age = current_time - manager->decay_cache[i].last_seen;

        /* Keep entry if still within decay period */
        if (age <= (time_t)manager->config.decay_seconds && age >= 0) {
            if (write_idx != i) {
                manager->decay_cache[write_idx] = manager->decay_cache[i];
            }
            write_idx++;
        }
    }

    manager->cache_size = write_idx;

#ifdef DEBUG
    if (config->debug >= 2) {
        fprintf(stderr, "DEBUG - Cleaned decay cache: %u entries remain\n",
                manager->cache_size);
    }
#endif
}
