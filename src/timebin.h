/*****
 *
 * Description: Time Binning System Headers
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

#ifndef TIMEBIN_DOT_H
#define TIMEBIN_DOT_H

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
#include "hilbert.h"
#include <stdint.h>
#include <time.h>

/****
 *
 * defines
 *
 ****/

/* Time bin durations in seconds */
#define TIMEBIN_1MIN   60
#define TIMEBIN_5MIN   (5 * 60)
#define TIMEBIN_15MIN  (15 * 60)
#define TIMEBIN_30MIN  (30 * 60)
#define TIMEBIN_60MIN  (60 * 60)

#define TIMEBIN_DEFAULT TIMEBIN_1MIN

/* Decay cache defaults */
#define DECAY_CACHE_DURATION_DEFAULT (3 * 60 * 60)  /* 3 hour default */
#define DECAY_CACHE_MAX_ENTRIES 65536  /* Max cached coordinates */

/****
 *
 * typedefs & structs
 *
 ****/

/**
 * Decay cache entry - tracks when a coordinate was last seen
 */
typedef struct {
    uint32_t coord_key;      /* Combined x,y coordinate as key */
    time_t last_seen;        /* Last time this coordinate had activity */
    uint32_t intensity;      /* Peak intensity at this coordinate */
} DecayCacheEntry_t;

/**
 * Time bin configuration
 */
typedef struct {
    uint32_t bin_seconds;    /* Size of time bin in seconds */
    time_t start_time;       /* Start of collection window (0 = auto-detect) */
    time_t end_time;         /* End of collection window (0 = process all) */
    uint8_t hilbert_order;   /* Hilbert curve order for heatmap */
    uint32_t dimension;      /* Hilbert curve dimension (2^order) */
    uint32_t decay_seconds;  /* How long coordinates persist (default: 3600) */
} TimeBinConfig_t;

/**
 * Time bin heatmap (one frame's worth of data)
 */
typedef struct {
    time_t bin_start;        /* Start time of this bin */
    time_t bin_end;          /* End time of this bin */
    uint32_t event_count;    /* Total events in bin */
    uint32_t unique_ips;     /* Number of unique IPs seen */
    uint32_t *heatmap;       /* 2D array: heatmap[y * dimension + x] */
    uint32_t dimension;      /* Width/height of heatmap */
    uint32_t max_intensity;  /* Maximum hit count in this bin */
} TimeBin_t;

/**
 * Time bin manager - manages multiple bins with decay cache
 */
typedef struct {
    TimeBinConfig_t config;
    TimeBin_t *current_bin;
    time_t next_bin_start;
    uint32_t total_bins;
    uint32_t bins_written;

    /* Decay cache for coordinate persistence */
    DecayCacheEntry_t *decay_cache;  /* Array of cache entries */
    uint32_t cache_size;              /* Current number of cached entries */
    uint32_t cache_capacity;          /* Maximum cache capacity */
} TimeBinManager_t;

/****
 *
 * function prototypes
 *
 ****/

/* Time bin manager */
TimeBinManager_t *createTimeBinManager(TimeBinConfig_t *config);
void destroyTimeBinManager(TimeBinManager_t *manager);

/* Time bin operations */
TimeBin_t *createTimeBin(time_t start_time, uint32_t bin_seconds, uint32_t dimension);
void destroyTimeBin(TimeBin_t *bin);
void resetTimeBin(TimeBin_t *bin);

/* Add events to bins */
int addEventToBin(TimeBin_t *bin, uint32_t x, uint32_t y);
int processEvent(TimeBinManager_t *manager, time_t event_time, uint32_t x, uint32_t y);

/* Finalize and output */
int finalizeBin(TimeBin_t *bin);
time_t getBinForTime(time_t event_time, uint32_t bin_seconds);

/* Utility functions */
int parseTimeBinDuration(const char *str, uint32_t *seconds);
const char *formatTimeBinDuration(uint32_t seconds);

/* Decay cache operations */
int updateDecayCache(TimeBinManager_t *manager, uint32_t x, uint32_t y, time_t event_time, uint32_t intensity);
void applyDecayToHeatmap(TimeBinManager_t *manager, TimeBin_t *bin);
void cleanExpiredCacheEntries(TimeBinManager_t *manager, time_t current_time);

#endif /* TIMEBIN_DOT_H */
