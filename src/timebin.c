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
 *
 * Parse time bin duration string to seconds
 *
 * DESCRIPTION:
 *   Converts human-readable duration strings (e.g., "1m", "5m", "60s", "2h")
 *   into seconds. Supports multiple time unit suffixes: 's' (seconds),
 *   'm' (minutes), 'h' (hours). If no suffix is provided, assumes seconds.
 *   Case-insensitive suffix parsing.
 *
 * PARAMETERS:
 *   str - Duration string to parse (e.g., "1m", "5M", "60s", "2h")
 *   seconds - Output pointer to store parsed duration in seconds
 *
 * RETURNS:
 *   TRUE (1) if parsing succeeded and result is valid
 *   FALSE (0) if parsing failed (NULL pointers, invalid format, non-positive value)
 *
 * SIDE EFFECTS:
 *   Writes to *seconds on success
 *   No side effects on failure
 *
 * ALGORITHM:
 *   1. Validate input pointers (NULL check)
 *   2. Parse numeric value using strtol()
 *   3. Check value is positive
 *   4. Examine suffix character:
 *      - 'm' or 'M': multiply by 60 (minutes)
 *      - 's' or 'S' or '\0': use as-is (seconds)
 *      - 'h' or 'H': multiply by 3600 (hours)
 *      - Other: return FALSE (invalid suffix)
 *   5. Store result in *seconds
 *
 * PERFORMANCE:
 *   O(n) where n = strlen(str) for strtol() parsing
 *   Typical: ~10-50ns for short strings (1-4 characters)
 *
 * EXAMPLES:
 *   "1m"  → 60 seconds
 *   "5M"  → 300 seconds
 *   "30s" → 30 seconds
 *   "60"  → 60 seconds (no suffix defaults to seconds)
 *   "2h"  → 7200 seconds
 *
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
 *
 * Format time bin duration as human-readable string
 *
 * DESCRIPTION:
 *   Converts duration in seconds to compact human-readable format.
 *   Automatically selects the most appropriate unit (hours, minutes, seconds)
 *   based on the input value. Returns static buffer with formatted string.
 *
 * PARAMETERS:
 *   seconds - Duration in seconds to format
 *
 * RETURNS:
 *   Pointer to static buffer containing formatted string (e.g., "5m", "2h", "90s")
 *   WARNING: Static buffer is reused on subsequent calls (not thread-safe)
 *
 * SIDE EFFECTS:
 *   Overwrites static internal buffer on each call
 *   Not thread-safe due to static buffer usage
 *
 * ALGORITHM:
 *   1. Check if seconds evenly divides by 3600 (hours)
 *      - Yes: Format as "Nh" where N = seconds / 3600
 *   2. Else check if seconds evenly divides by 60 (minutes)
 *      - Yes: Format as "Nm" where N = seconds / 60
 *   3. Else format as "Ns" (seconds)
 *
 * PERFORMANCE:
 *   O(1) - Two modulo operations and one snprintf()
 *   Typical: ~50-100ns
 *
 * EXAMPLES:
 *   60     → "1m"
 *   300    → "5m"
 *   3600   → "1h"
 *   7200   → "2h"
 *   90     → "90s" (not evenly divisible by 60)
 *
 * NOTES:
 *   Returned pointer points to static buffer - copy if persistence needed
 *
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
 *
 * Calculate time bin start time for event
 *
 * DESCRIPTION:
 *   Maps an event timestamp to the start time of its corresponding time bin.
 *   Bins are aligned to Unix epoch (1970-01-01 00:00:00 UTC), ensuring
 *   consistent bin boundaries across runs. Uses integer division to floor
 *   the event time to the nearest bin boundary.
 *
 * PARAMETERS:
 *   event_time - Event timestamp (Unix epoch seconds)
 *   bin_seconds - Time bin duration in seconds (e.g., 60, 300, 3600)
 *
 * RETURNS:
 *   Unix timestamp of bin start time (aligned to epoch)
 *
 * SIDE EFFECTS:
 *   None (pure function)
 *
 * ALGORITHM:
 *   bin_start = (event_time / bin_seconds) * bin_seconds
 *
 *   This floors event_time to the nearest bin_seconds boundary.
 *
 * PERFORMANCE:
 *   O(1) - One division and one multiplication
 *   Typical: <10ns on modern CPUs
 *
 * EXAMPLES:
 *   event_time=1234567890, bin_seconds=60  → 1234567860 (floor to minute)
 *   event_time=1234567899, bin_seconds=60  → 1234567860 (same minute)
 *   event_time=1234567900, bin_seconds=60  → 1234567900 (next minute)
 *   event_time=1234567890, bin_seconds=300 → 1234567500 (floor to 5-min)
 *
 * KEY PROPERTIES:
 *   - Deterministic: Same event_time + bin_seconds always returns same result
 *   - Epoch-aligned: Bins align to 1970-01-01 00:00:00 UTC
 *   - Idempotent: getBinForTime(result, bin_seconds) == result
 *
 ****/
time_t getBinForTime(time_t event_time, uint32_t bin_seconds)
{
    return (event_time / bin_seconds) * bin_seconds;
}

/****
 *
 * Create new time bin heatmap
 *
 * DESCRIPTION:
 *   Allocates and initializes a TimeBin_t structure with zero-initialized
 *   heatmap array. The heatmap is a 2D array stored in row-major order
 *   (heatmap[y * dimension + x]). Sets bin time boundaries and dimension.
 *
 * PARAMETERS:
 *   start_time - Bin start time (Unix epoch seconds)
 *   bin_seconds - Duration of this bin in seconds
 *   dimension - Width/height of heatmap (typically 2^order from Hilbert curve)
 *
 * RETURNS:
 *   Pointer to newly allocated TimeBin_t on success
 *   NULL on allocation failure
 *
 * SIDE EFFECTS:
 *   Allocates memory via XMALLOC() (must be freed with destroyTimeBin())
 *   Prints debug message if config->debug >= 2
 *
 * ALGORITHM:
 *   1. Allocate TimeBin_t structure
 *   2. Zero-initialize structure
 *   3. Set bin_start = start_time
 *   4. Set bin_end = start_time + bin_seconds
 *   5. Set dimension
 *   6. Allocate heatmap array: dimension * dimension * sizeof(uint32_t)
 *   7. Zero-initialize heatmap
 *   8. Print debug info if enabled
 *
 * PERFORMANCE:
 *   O(dimension²) due to heatmap allocation and zeroing
 *   For dimension=4096: ~67MB allocation, ~10-50ms initialization
 *
 * MEMORY:
 *   sizeof(TimeBin_t) + dimension² * sizeof(uint32_t)
 *   Example (dimension=4096): 48 bytes + 16,777,216 bytes ≈ 16MB
 *
 * NOTES:
 *   - Caller must call destroyTimeBin() to free memory
 *   - Heatmap starts with all zeros (no activity)
 *   - event_count, unique_ips, max_intensity all initialized to 0
 *
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
 *
 * Destroy time bin and free memory
 *
 * DESCRIPTION:
 *   Frees all memory associated with a TimeBin_t structure, including
 *   the heatmap array. Safe to call with NULL pointer (no-op).
 *
 * PARAMETERS:
 *   bin - Pointer to TimeBin_t to destroy (may be NULL)
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Frees memory allocated by createTimeBin()
 *   Invalidates bin pointer (caller should set to NULL)
 *
 * ALGORITHM:
 *   1. Check if bin is NULL (early return if so)
 *   2. Free heatmap array if non-NULL
 *   3. Free bin structure
 *
 * PERFORMANCE:
 *   O(1) - Two XFREE() calls
 *   Typical: <1μs
 *
 * NOTES:
 *   - Safe to call multiple times (first call frees, subsequent are no-ops if pointer set to NULL)
 *   - Does not validate bin contents (assumes valid structure)
 *
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
 *
 * Reset time bin for reuse
 *
 * DESCRIPTION:
 *   Clears all heatmap data and statistics, preparing bin for reuse with
 *   new time period. More efficient than destroy+create cycle since it
 *   reuses existing allocations. Zeros heatmap and resets counters.
 *
 * PARAMETERS:
 *   bin - Pointer to TimeBin_t to reset (may be NULL for no-op)
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Zeros heatmap array
 *   Resets event_count, unique_ips, max_intensity to 0
 *   Does NOT modify bin_start, bin_end, or dimension
 *
 * ALGORITHM:
 *   1. Check if bin is NULL (early return if so)
 *   2. Calculate heatmap_size = dimension * dimension * sizeof(uint32_t)
 *   3. memset(heatmap, 0, heatmap_size)
 *   4. Set event_count = 0
 *   5. Set unique_ips = 0
 *   6. Set max_intensity = 0
 *
 * PERFORMANCE:
 *   O(dimension²) due to memset() on heatmap
 *   For dimension=4096: ~10-20ms to zero 16MB array
 *
 * NOTES:
 *   - Caller should update bin_start and bin_end after reset
 *   - More efficient than destroyTimeBin() + createTimeBin()
 *   - Preserves bin structure and heatmap allocation
 *
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
 *
 * Add event to time bin heatmap
 *
 * DESCRIPTION:
 *   Increments hit count for a specific coordinate in the heatmap.
 *   Updates bin statistics (event_count, max_intensity). Validates
 *   coordinates are within bounds. Uses row-major array indexing.
 *
 * PARAMETERS:
 *   bin - Pointer to TimeBin_t to update
 *   x - X coordinate (0 to dimension-1)
 *   y - Y coordinate (0 to dimension-1)
 *
 * RETURNS:
 *   TRUE (1) on success
 *   FALSE (0) if bin is NULL, heatmap is NULL, or coordinates out of bounds
 *
 * SIDE EFFECTS:
 *   Increments bin->heatmap[y * dimension + x]
 *   Increments bin->event_count
 *   Updates bin->max_intensity if new value is higher
 *
 * ALGORITHM:
 *   1. Validate bin and heatmap pointers
 *   2. Check x < dimension and y < dimension
 *   3. Calculate array index: idx = y * dimension + x
 *   4. Increment heatmap[idx]
 *   5. Increment event_count
 *   6. Update max_intensity if heatmap[idx] > max_intensity
 *
 * PERFORMANCE:
 *   O(1) - Array index calculation and increment
 *   Typical: <10ns
 *
 * NOTES:
 *   - Uses row-major order: heatmap[y * dimension + x]
 *   - No overflow protection on heatmap values (uint32_t can hold 4B+ events)
 *   - Safe to call multiple times for same coordinate (accumulates)
 *
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
 *
 * Finalize time bin and compute statistics
 *
 * DESCRIPTION:
 *   Computes final statistics for a time bin before output. Scans entire
 *   heatmap to count unique IP locations (non-zero cells). Should be called
 *   after all events for a bin have been added and before visualization.
 *
 * PARAMETERS:
 *   bin - Pointer to TimeBin_t to finalize
 *
 * RETURNS:
 *   TRUE (1) on success
 *   FALSE (0) if bin is NULL or heatmap is NULL
 *
 * SIDE EFFECTS:
 *   Updates bin->unique_ips with count of non-zero heatmap cells
 *   Prints debug message if config->debug >= 1
 *
 * ALGORITHM:
 *   1. Validate bin and heatmap pointers
 *   2. Calculate total_points = dimension * dimension
 *   3. Initialize unique_ips = 0
 *   4. For i = 0 to total_points-1:
 *      - If heatmap[i] > 0, increment unique_ips
 *   5. Print debug summary if enabled
 *
 * PERFORMANCE:
 *   O(dimension²) - Full heatmap scan
 *   For dimension=4096: ~16M comparisons, ~5-20ms
 *
 * STATISTICS COMPUTED:
 *   - unique_ips: Count of distinct coordinate positions with activity
 *   - event_count: Already maintained by addEventToBin()
 *   - max_intensity: Already maintained by addEventToBin()
 *
 * NOTES:
 *   - Call once per bin before output/visualization
 *   - unique_ips may be less than event_count (multiple events per IP)
 *   - Debug output includes bin timestamp and all statistics
 *
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
 *
 * Create time bin manager with decay cache
 *
 * DESCRIPTION:
 *   Allocates and initializes a TimeBinManager_t structure to manage
 *   multiple time bins with decay tracking. Copies configuration and
 *   allocates decay cache for coordinate persistence across bins.
 *   This is the primary entry point for time binning system.
 *
 * PARAMETERS:
 *   config_in - Pointer to TimeBinConfig_t with settings:
 *               - bin_seconds: Time bin duration
 *               - hilbert_order: Curve order for dimension calculation
 *               - dimension: Heatmap width/height (2^order)
 *               - decay_seconds: How long coordinates persist
 *
 * RETURNS:
 *   Pointer to newly allocated TimeBinManager_t on success
 *   NULL on allocation failure or NULL config
 *
 * SIDE EFFECTS:
 *   Allocates memory for manager structure and decay cache
 *   Prints debug message if config->debug >= 1
 *
 * ALGORITHM:
 *   1. Validate config_in pointer
 *   2. Allocate TimeBinManager_t structure
 *   3. Zero-initialize structure
 *   4. Copy config_in to manager->config
 *   5. Set cache_capacity = DECAY_CACHE_MAX_ENTRIES (65536)
 *   6. Allocate decay_cache array
 *   7. Zero-initialize decay cache
 *   8. Initialize counters (current_bin=NULL, cache_size=0, etc.)
 *   9. Print debug summary if enabled
 *
 * PERFORMANCE:
 *   O(1) allocation + O(cache_capacity) zeroing
 *   Typical: <1ms for 65536-entry cache
 *
 * MEMORY:
 *   sizeof(TimeBinManager_t) + (cache_capacity * sizeof(DecayCacheEntry_t))
 *   ~160 bytes + (65536 * 16 bytes) ≈ 1MB for decay cache
 *
 * NOTES:
 *   - Must call destroyTimeBinManager() to free memory
 *   - No bins created yet (current_bin = NULL)
 *   - Decay cache starts empty (cache_size = 0)
 *
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

    /* Initialize residue map - persistent attack memory (cumulative volume tracking) */
    uint32_t residue_map_size = manager->config.dimension * manager->config.dimension * sizeof(uint32_t);
    manager->residue_map = (uint32_t *)XMALLOC((int)residue_map_size);

    if (!manager->residue_map) {
        XFREE(manager->decay_cache);
        XFREE(manager);
        return NULL;
    }

    memset(manager->residue_map, 0, residue_map_size);
    manager->residue_count = 0;
    manager->residue_max_volume = 0;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Created time bin manager: bin_size=%s, order=%u, decay=%us, residue_map=%u bytes\n",
                formatTimeBinDuration(config_in->bin_seconds),
                config_in->hilbert_order,
                config_in->decay_seconds,
                residue_map_size);
    }
#endif

    return manager;
}

/****
 *
 * Destroy time bin manager and free all resources
 *
 * DESCRIPTION:
 *   Frees all memory associated with a TimeBinManager_t structure,
 *   including current bin and decay cache. Safe to call with NULL pointer.
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t to destroy (may be NULL)
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Frees current_bin if non-NULL (via destroyTimeBin())
 *   Frees decay_cache if non-NULL
 *   Frees manager structure
 *   Invalidates manager pointer (caller should set to NULL)
 *
 * ALGORITHM:
 *   1. Check if manager is NULL (early return if so)
 *   2. Destroy current_bin if non-NULL
 *   3. Free decay_cache if non-NULL
 *   4. Free manager structure
 *
 * PERFORMANCE:
 *   O(1) - Three XFREE() calls plus destroyTimeBin()
 *   Typical: <1μs
 *
 * NOTES:
 *   - Automatically finalizes/destroys current bin if active
 *   - Does not flush/save any pending data (caller's responsibility)
 *   - Safe to call multiple times if pointer set to NULL after first call
 *
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

    if (manager->residue_map) {
        XFREE(manager->residue_map);
    }

    XFREE(manager);
}

/****
 *
 * Process event and manage time bin lifecycle
 *
 * DESCRIPTION:
 *   Main event processing function that manages time bin lifecycle.
 *   Automatically creates new bins as time progresses, finalizes and
 *   destroys old bins, and adds events to the current bin. Updates
 *   decay cache to track coordinate persistence across bins.
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t
 *   event_time - Event timestamp (Unix epoch seconds)
 *   x - Hilbert curve X coordinate for event source IP
 *   y - Hilbert curve Y coordinate for event source IP
 *
 * RETURNS:
 *   TRUE (1) on success
 *   FALSE (0) if manager is NULL or bin operations fail
 *
 * SIDE EFFECTS:
 *   May create new time bin (allocates memory)
 *   May finalize and destroy current bin (frees memory)
 *   Updates decay cache with coordinate activity
 *   Increments manager->total_bins when creating new bin
 *   Increments manager->bins_written when finalizing bin
 *   Calls addEventToBin() to update heatmap
 *
 * ALGORITHM:
 *   1. Validate manager pointer
 *   2. Calculate bin_start for event using getBinForTime()
 *   3. Check if current_bin exists and matches bin_start
 *   4. If bin mismatch or no current bin:
 *      a. Finalize current_bin if it exists
 *      b. Increment bins_written
 *      c. Destroy current_bin
 *      d. Create new bin with bin_start
 *      e. Increment total_bins
 *   5. Update decay cache with (x, y, event_time, intensity=1)
 *   6. Add event to current bin at (x, y)
 *
 * PERFORMANCE:
 *   Same bin: O(1) for cache update + addEventToBin()
 *   New bin: O(dimension²) for finalize + O(dimension²) for create
 *   Typical same-bin: <100ns
 *   Typical new-bin: ~20-50ms for dimension=4096
 *
 * NOTES:
 *   - Events MUST be processed in time order for correct binning
 *   - Out-of-order events will trigger premature bin finalization
 *   - Decay cache maintains coordinate visibility across bins
 *   - Bin output handled via external visualization callbacks
 *
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
        /* Finalize current bin if it exists (rendering handled externally) */
        if (manager->current_bin) {
            finalizeBin(manager->current_bin);
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

    /* Mark coordinate in residue map for persistent attack memory */
    markResidue(manager, x, y);

    /* Add event to current bin */
    return addEventToBin(manager->current_bin, x, y);
}

/****
 *
 * Update decay cache with coordinate activity
 *
 * DESCRIPTION:
 *   Records or updates coordinate activity in decay cache for persistence
 *   across time bins. Creates combined coordinate key from x,y and either
 *   updates existing entry or adds new entry if space available. Linear
 *   search through cache (acceptable for 65K entries with good locality).
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t
 *   x - X coordinate (0-65535)
 *   y - Y coordinate (0-65535)
 *   event_time - Event timestamp (Unix epoch seconds)
 *   intensity - Event intensity/count to add (typically 1)
 *
 * RETURNS:
 *   TRUE (1) on success
 *   FALSE (0) if manager or decay_cache is NULL
 *
 * SIDE EFFECTS:
 *   Updates existing cache entry (last_seen, intensity) if coordinate found
 *   Adds new cache entry if not found and cache not full
 *   Increments manager->cache_size when adding new entry
 *
 * ALGORITHM:
 *   1. Validate manager and decay_cache pointers
 *   2. Create coord_key = (x << 16) | y  // Pack x,y into single uint32_t
 *   3. Linear search cache for matching coord_key
 *   4. If found:
 *      - Update last_seen = event_time
 *      - Add intensity to existing intensity
 *   5. If not found and cache_size < cache_capacity:
 *      - Add new entry at cache_size index
 *      - Set coord_key, last_seen, intensity
 *      - Increment cache_size
 *
 * PERFORMANCE:
 *   Best case (found early): O(1)
 *   Average case: O(cache_size/2) for linear search
 *   Worst case: O(cache_size) = O(65536)
 *   Typical: 50-500ns depending on cache locality
 *
 * CACHE EVICTION:
 *   When cache is full (cache_size == cache_capacity), old entries are NOT
 *   added until cleanExpiredCacheEntries() is called to compact the cache.
 *
 * COORD_KEY FORMAT:
 *   coord_key = (x << 16) | y
 *   Upper 16 bits: X coordinate
 *   Lower 16 bits: Y coordinate
 *   Max supported dimension: 65536 (order 16)
 *
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
 *
 * Apply decay cache to heatmap for IP persistence visualization
 *
 * DESCRIPTION:
 *   Overlays cached coordinate activity onto current bin's heatmap with
 *   time-based decay. Coordinates fade over time based on age relative to
 *   decay_seconds. Enables visualization of persistent attackers and
 *   temporal patterns. Uses linear decay function.
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t with decay cache
 *   bin - Pointer to TimeBin_t to apply decay to
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Modifies bin->heatmap by adding decayed intensity values
 *   Updates bin->max_intensity if decayed values create new peak
 *   No modification of decay_cache itself
 *
 * ALGORITHM:
 *   For each cache entry (i = 0 to cache_size-1):
 *     1. Calculate age = bin_start - last_seen
 *     2. Skip if age > decay_seconds or age < 0 (future event)
 *     3. Calculate decay_factor = 1.0 - (age / decay_seconds)
 *        - Fresh (age=0): decay_factor = 1.0 (100% intensity)
 *        - Half-life: decay_factor = 0.5 (50% intensity)
 *        - Expired: decay_factor = 0.0 (0% intensity)
 *     4. Extract x,y from coord_key: x = (coord_key >> 16), y = (coord_key & 0xFFFF)
 *     5. Validate x,y within bounds
 *     6. Calculate decayed_intensity = intensity * decay_factor
 *     7. Ensure minimum visibility: if decayed_intensity < 1 and decay_factor > 0, set to 1
 *     8. Add decayed_intensity to heatmap[y * dimension + x]
 *     9. Update max_intensity if needed
 *
 * PERFORMANCE:
 *   O(cache_size) - Linear scan through decay cache
 *   Typical: 0.5-5ms for 65K entries
 *
 * DECAY FUNCTION:
 *   Linear: intensity(t) = base_intensity * (1 - age/decay_period)
 *   - Simple and predictable
 *   - Events fade smoothly to zero
 *   - Minimum intensity of 1 ensures visibility until fully expired
 *
 * EXAMPLES:
 *   decay_seconds = 10800 (3 hours)
 *   Event at t=1000, bin_start=3000:
 *     age = 2000 seconds
 *     decay_factor = 1 - (2000/10800) = 0.815 (81.5% intensity)
 *
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
 *
 * Clean expired entries from decay cache
 *
 * DESCRIPTION:
 *   Removes expired entries from decay cache by compacting the array.
 *   An entry is expired if its age (current_time - last_seen) exceeds
 *   decay_seconds. Uses in-place compaction to maintain cache without
 *   reallocation. Should be called periodically to prevent cache overflow.
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t with decay cache
 *   current_time - Current timestamp for age calculation (Unix epoch seconds)
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Compacts decay_cache array (moves entries, preserves order)
 *   Updates manager->cache_size to new size after cleaning
 *   Prints debug message if config->debug >= 2
 *
 * ALGORITHM:
 *   1. Validate manager and decay_cache pointers
 *   2. Initialize write_idx = 0 (compaction target index)
 *   3. For i = 0 to cache_size-1:
 *      a. Calculate age = current_time - last_seen
 *      b. Check if age <= decay_seconds and age >= 0 (not future)
 *      c. If valid:
 *         - Copy entry[i] to entry[write_idx] if i != write_idx
 *         - Increment write_idx
 *   4. Update cache_size = write_idx
 *   5. Print debug summary if enabled
 *
 * PERFORMANCE:
 *   O(cache_size) - Single pass through cache
 *   Typical: 0.1-1ms for 65K entries
 *
 * COMPACTION EXAMPLE:
 *   Before: [valid, expired, valid, expired, valid]  (size=5)
 *   After:  [valid, valid, valid, ?, ?]              (size=3)
 *   Entries marked '?' are beyond new size (ignored)
 *
 * WHEN TO CALL:
 *   - Periodically during long runs (e.g., every 1000 events)
 *   - When cache_size approaches cache_capacity
 *   - Before applying decay to ensure cache has space for new entries
 *
 * NOTES:
 *   - Does NOT shrink cache allocation, only reduces logical size
 *   - Preserves insertion order of remaining entries
 *   - Future events (age < 0) are also removed as invalid
 *
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

/****
 *
 * Mark coordinate in residue map with cumulative volume tracking
 *
 * DESCRIPTION:
 *   Increments cumulative attack volume for a coordinate in the residue map.
 *   Tracks total attack count across all time bins to enable volume-based
 *   visualization (minimal/average/heavy attacker coloring).
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t with residue map
 *   x - X coordinate to mark
 *   y - Y coordinate to mark
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Increments residue_map[y * dimension + x]
 *   Increments residue_count if this is first attack from this coordinate
 *   Updates residue_max_volume if new maximum reached
 *
 ****/
void markResidue(TimeBinManager_t *manager, uint32_t x, uint32_t y)
{
    uint32_t idx;

    if (!manager || !manager->residue_map) {
        return;
    }

    /* Check bounds */
    if (x >= manager->config.dimension || y >= manager->config.dimension) {
        return;
    }

    /* Calculate index into residue map */
    idx = y * manager->config.dimension + x;

    /* Increment cumulative volume for this coordinate */
    if (manager->residue_map[idx] == 0) {
        manager->residue_count++;  /* First attack from this coordinate */
    }

    manager->residue_map[idx]++;

    /* Track maximum volume across all coordinates */
    if (manager->residue_map[idx] > manager->residue_max_volume) {
        manager->residue_max_volume = manager->residue_map[idx];
    }

#ifdef DEBUG
    if (config->debug >= 5) {
        fprintf(stderr, "DEBUG - Residue at (%u,%u): volume=%u, max_volume=%u, unique_coords=%u\n",
                x, y, manager->residue_map[idx], manager->residue_max_volume, manager->residue_count);
    }
#endif
}

/****
 *
 * Get residue volume for coordinate
 *
 * DESCRIPTION:
 *   Returns cumulative attack volume for a coordinate from the residue map.
 *
 * PARAMETERS:
 *   manager - Pointer to TimeBinManager_t with residue map
 *   x - X coordinate to check
 *   y - Y coordinate to check
 *
 * RETURNS:
 *   Cumulative attack count for this coordinate (0 = never attacked)
 *
 ****/
uint32_t getResidue(TimeBinManager_t *manager, uint32_t x, uint32_t y)
{
    uint32_t idx;

    if (!manager || !manager->residue_map) {
        return 0;
    }

    /* Check bounds */
    if (x >= manager->config.dimension || y >= manager->config.dimension) {
        return 0;
    }

    /* Calculate index and return residue volume */
    idx = y * manager->config.dimension + x;
    return manager->residue_map[idx];
}
