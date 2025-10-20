/*****
 *
 * Description: Visualization Output Implementation
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

#include "visualize.h"
#include "hilbert.h"
#include "mem.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/****
 *
 * local variables
 *
 ****/

PRIVATE int viz_initialized = FALSE;
PRIVATE VisualizationConfig_t viz_config;

/* Cached non-routable mask to avoid recreating every frame */
PRIVATE uint8_t *cached_nonroutable_mask = NULL;
PRIVATE uint8_t cached_mask_order = 0;
PRIVATE uint32_t cached_mask_dimension = 0;

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
 * Initialize visualization system
 ****/
int initVisualization(VisualizationConfig_t *config_in)
{
    if (!config_in) {
        return FALSE;
    }

    memcpy(&viz_config, config_in, sizeof(VisualizationConfig_t));
    viz_initialized = TRUE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Visualization initialized: %ux%u\n",
                viz_config.width, viz_config.height);
    }
#endif

    return TRUE;
}

/****
 * Deinitialize visualization
 ****/
void deInitVisualization(void)
{
    /* Free cached mask if allocated */
    if (cached_nonroutable_mask) {
        XFREE(cached_nonroutable_mask);
        cached_nonroutable_mask = NULL;
        cached_mask_order = 0;
        cached_mask_dimension = 0;
    }

    viz_initialized = FALSE;
}

/****
 * Create a mask for non-routable IP space
 *
 * For each point on the Hilbert curve, we need to check if ANY IP that could
 * map to that coordinate is non-routable. Since we use hashing, we iterate
 * through the entire IPv4 space and mark positions.
 *
 * Note: This samples the IP space efficiently by checking representative IPs
 * Returns: Allocated mask array (caller must free), or NULL on error
 ****/
PRIVATE uint8_t *createNonRoutableMask(uint8_t order, uint32_t dimension)
{
    uint8_t *mask;
    uint32_t mask_size;
    uint32_t ip, x, y, idx;
    uint64_t hilbert_idx;
    uint32_t sample_step;

    mask_size = dimension * dimension;
    mask = (uint8_t *)XMALLOC((int)(mask_size * sizeof(uint8_t)));
    if (!mask) {
        fprintf(stderr, "ERR - Failed to allocate non-routable mask\n");
        return NULL;
    }

    /* Initialize to zero (routable) */
    memset(mask, 0, mask_size);

    /* Sample the IP space - we'll check every Nth IP to build the mask
     * For order 12 (4096x4096 = 16M points) vs 4.3B IPv4 addresses,
     * we sample every ~256 IPs to get good coverage
     */
    sample_step = (order <= 10) ? 64 : 256;

#ifdef DEBUG
    if (config->debug >= 2) {
        fprintf(stderr, "DEBUG - Creating non-routable IP mask (order=%u, step=%u)\n",
                order, sample_step);
    }
#endif

    /* Scan IP ranges that are non-routable */
    for (ip = 0; ip < 0xFFFFFFFF; ip += sample_step) {
        if (isNonRoutableIP(ip)) {
            /* Map this IP to Hilbert coordinates */
            hilbert_idx = ipToHilbertIndex(ip, order);
            hilbertIndexToXY(hilbert_idx, order, &x, &y);

            /* Mark this position as non-routable */
            if (x < dimension && y < dimension) {
                idx = y * dimension + x;
                mask[idx] = 1;
            }
        }

        /* Handle wrap-around at end of IP space */
        if (ip > 0xFFFFFFFF - sample_step) {
            break;
        }
    }

    /* Also check the last IP explicitly */
    if (isNonRoutableIP(0xFFFFFFFF)) {
        hilbert_idx = ipToHilbertIndex(0xFFFFFFFF, order);
        hilbertIndexToXY(hilbert_idx, order, &x, &y);
        if (x < dimension && y < dimension) {
            idx = y * dimension + x;
            mask[idx] = 1;
        }
    }

#ifdef DEBUG
    if (config->debug >= 2) {
        uint32_t marked_count = 0;
        for (idx = 0; idx < mask_size; idx++) {
            if (mask[idx]) marked_count++;
        }
        fprintf(stderr, "DEBUG - Non-routable mask: %u/%u positions marked (%.2f%%)\n",
                marked_count, mask_size, (100.0f * marked_count) / mask_size);
    }
#endif

    return mask;
}

/****
 * Map intensity to color (black background, bright dots for activity)
 * Higher intensity = more red (white -> yellow -> red)
 *
 * Low volume attacks appear white
 * Medium volume attacks appear yellow
 * High volume attacks appear red
 ****/
RGB_t intensityToColor(uint32_t intensity, uint32_t max_intensity)
{
    RGB_t color;
    float normalized, enhanced;

    if (intensity == 0) {
        /* Black for no activity */
        color.r = color.g = color.b = 0;
        return color;
    }

    if (max_intensity == 0) {
        max_intensity = 1;
    }

    /* Normalize intensity to 0.0-1.0 */
    normalized = (float)intensity / (float)max_intensity;

    /* Non-linear brightness scaling with high base brightness
     * Start at 50% brightness for even a single attack
     * Then scale from 50% to 100% based on intensity
     * Formula: 0.5 + 0.5 * normalized
     *
     * This ensures:
     * - Single attack (intensity=1, max=1000): ~50% brightness
     * - Medium attacks: 50-75% brightness
     * - Maximum attacks: 100% brightness
     */
    enhanced = 0.5f + 0.5f * normalized;

    /* Clamp to valid range */
    if (enhanced > 1.0f) enhanced = 1.0f;
    if (enhanced < 0.5f) enhanced = 0.5f;  /* Minimum 50% for any activity */

    /* Color gradient: White -> Yellow -> Red
     * Larger attack volume = more red
     *
     * enhanced ranges from 0.5 (single attack) to 1.0 (maximum attacks)
     * Normalize to 0.0-1.0 range: t = (enhanced - 0.5) / 0.5
     *
     * 0.5-0.75 (t=0.0-0.5): White to Yellow (remove blue)
     * 0.75-1.0 (t=0.5-1.0): Yellow to Red (remove green)
     */

    /* Normalize enhanced [0.5, 1.0] to t [0.0, 1.0] */
    float t = (enhanced - 0.5f) / 0.5f;

    if (t < 0.5f) {
        /* White to Yellow: Keep R=255, G=255, fade B from 255 to 0 */
        color.r = 255;
        color.g = 255;
        color.b = (uint8_t)(255.0f * (1.0f - 2.0f * t));  /* 255 -> 0 */
    } else {
        /* Yellow to Red: Keep R=255, fade G from 255 to 0, B=0 */
        color.r = 255;
        color.g = (uint8_t)(255.0f * (2.0f - 2.0f * t));  /* 255 -> 0 */
        color.b = 0;
    }

    return color;
}

/****
 * Write PPM image file
 *
 * PPM is a simple uncompressed raster format, very fast to write
 ****/
int writePPM(const char *filename, const TimeBin_t *bin, uint32_t width, uint32_t height)
{
    FILE *fp;
    uint32_t x, y, src_x, src_y;
    uint32_t intensity, idx;
    RGB_t color;
    uint8_t *nonroutable_mask = NULL;
    int is_nonroutable;

    if (!filename || !bin || !bin->heatmap) {
        return FALSE;
    }

    /* Create mask for non-routable IP space */
    /* Calculate Hilbert order from dimension (dimension = 2^order) */
    uint8_t hilbert_order = 0;
    uint32_t temp_dim = bin->dimension;
    while (temp_dim > 1) {
        temp_dim >>= 1;
        hilbert_order++;
    }

    /* Check if we can use cached mask */
    if (cached_nonroutable_mask &&
        cached_mask_order == hilbert_order &&
        cached_mask_dimension == bin->dimension) {
        /* Reuse cached mask */
        nonroutable_mask = cached_nonroutable_mask;
#ifdef DEBUG
        if (config->debug >= 4) {
            fprintf(stderr, "DEBUG - Using cached non-routable mask\n");
        }
#endif
    } else {
        /* Create new mask and cache it */
        nonroutable_mask = createNonRoutableMask(hilbert_order, bin->dimension);
        if (!nonroutable_mask) {
            fprintf(stderr, "WARN - Failed to create non-routable mask, continuing without it\n");
        } else {
            /* Free old cache if different dimensions */
            if (cached_nonroutable_mask &&
                (cached_mask_order != hilbert_order || cached_mask_dimension != bin->dimension)) {
                XFREE(cached_nonroutable_mask);
            }
            /* Cache the new mask */
            cached_nonroutable_mask = nonroutable_mask;
            cached_mask_order = hilbert_order;
            cached_mask_dimension = bin->dimension;
        }
    }

    /* Use secure_fopen() to prevent symlink attacks */
    fp = secure_fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "ERR - Failed to open %s for writing\n", filename);
        /* Note: Do not free nonroutable_mask - it's cached */
        return FALSE;
    }

    /* Write PPM header (P6 = binary RGB) */
    fprintf(fp, "P6\n%u %u\n255\n", width, height);

    /* Render heatmap to 16:9 image with centered square */
    /* Calculate scaling and offset to center the square Hilbert curve */
    float scale_x, scale_y, scale;
    uint32_t offset_x, offset_y;

    if (width > height) {
        /* Landscape - center horizontally */
        scale_y = (float)height / (float)bin->dimension;
        scale_x = scale_y;
        offset_x = (width - (uint32_t)((float)bin->dimension * scale_x)) / 2;
        offset_y = 0;
    } else {
        /* Portrait or square - center vertically */
        scale_x = (float)width / (float)bin->dimension;
        scale_y = scale_x;
        offset_x = 0;
        offset_y = (height - (uint32_t)((float)bin->dimension * scale_y)) / 2;
    }
    scale = scale_x;

    /* Write pixels row by row */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /* Check if we're in the Hilbert curve area */
            if (x >= offset_x && x < offset_x + (uint32_t)((float)bin->dimension * scale) &&
                y >= offset_y && y < offset_y + (uint32_t)((float)bin->dimension * scale)) {

                /* Map back to source coordinates */
                src_x = (uint32_t)((float)(x - offset_x) / scale);
                src_y = (uint32_t)((float)(y - offset_y) / scale);

                if (src_x < bin->dimension && src_y < bin->dimension) {
                    idx = src_y * bin->dimension + src_x;
                    intensity = bin->heatmap[idx];
                    color = intensityToColor(intensity, bin->max_intensity);

                    /* Apply dark blue overlay for non-routable IP space */
                    is_nonroutable = (nonroutable_mask && nonroutable_mask[idx]);
                    if (is_nonroutable) {
                        /* If no activity, show dark blue base color
                         * If activity present, blend with moderately dark blue
                         * This makes private IP space visible against black background
                         */
                        if (intensity == 0) {
                            /* No activity: show darker blue base (0, 0, 30) */
                            color.r = 0;
                            color.g = 0;
                            color.b = 30;
                        } else {
                            /* Activity present: blend with dark blue at 40% opacity
                             * Formula: result = color * 0.6 + dark_blue * 0.4
                             */
                            color.r = (uint8_t)(color.r * 0.6f);
                            color.g = (uint8_t)(color.g * 0.6f);
                            color.b = (uint8_t)(color.b * 0.6f + 30 * 0.4f);
                        }
                    }
                } else {
                    /* Border - black */
                    color.r = color.g = color.b = 0;
                }
            } else {
                /* Outside curve area - black */
                color.r = color.g = color.b = 0;
            }

            /* Write RGB pixel */
            fputc(color.r, fp);
            fputc(color.g, fp);
            fputc(color.b, fp);
        }
    }

    fclose(fp);

    /* Note: Do not free nonroutable_mask here - it's cached for reuse */

#ifdef DEBUG
    if (config->debug >= 2) {
        fprintf(stderr, "DEBUG - Wrote PPM: %s (%ux%u)\n", filename, width, height);
    }
#endif

    return TRUE;
}

/****
 * Generate filename for bin
 ****/
int generateBinFilename(char *buf, size_t buf_size, const char *dir,
                       const char *prefix, time_t bin_start, uint32_t bin_num)
{
    struct tm *tm_info;
    char time_str[64];

    if (!buf) {
        return FALSE;
    }

    tm_info = localtime(&bin_start);
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", tm_info);

    snprintf(buf, buf_size, "%s/%s_%s_%04u.ppm",
             dir ? dir : ".",
             prefix ? prefix : "frame",
             time_str,
             bin_num);

    return TRUE;
}

/****
 * Render time bin to output file
 ****/
int renderTimeBin(const TimeBin_t *bin, const char *output_path, uint32_t width, uint32_t height)
{
    if (!bin || !output_path) {
        return FALSE;
    }

    return writePPM(output_path, bin, width, height);
}
