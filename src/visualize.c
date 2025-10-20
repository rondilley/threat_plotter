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

/* Timestamp height in pixels */
#define TIMESTAMP_HEIGHT 30
#define TIMESTAMP_MARGIN 10

/* Simple 5x7 bitmap font for timestamp rendering */
/* Characters: 0-9, space, colon, dash */
#define FONT_WIDTH  5
#define FONT_HEIGHT 7

PRIVATE const uint8_t font_5x7[13][7] = {
    /* '0' */
    {0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C},
    /* '1' */
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E},
    /* '2' */
    {0x7C, 0xC6, 0x06, 0x0C, 0x30, 0x60, 0xFE},
    /* '3' */
    {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C},
    /* '4' */
    {0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C},
    /* '5' */
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C},
    /* '6' */
    {0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C},
    /* '7' */
    {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30},
    /* '8' */
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C},
    /* '9' */
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78},
    /* ' ' (space) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ':' */
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00},
    /* '-' */
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00}
};

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
 * Get font character index
 *
 * DESCRIPTION:
 *   Maps character to font bitmap index
 *
 * PARAMETERS:
 *   c - Character to map
 *
 * RETURNS:
 *   Font index (0-12), or 10 (space) if unknown
 *
 ****/
PRIVATE int getFontIndex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c == ' ') {
        return 10;
    } else if (c == ':') {
        return 11;
    } else if (c == '-') {
        return 12;
    }
    return 10;  /* Default to space for unknown chars */
}

/****
 *
 * Draw character at position
 *
 * DESCRIPTION:
 *   Renders single character from bitmap font to image buffer
 *
 * PARAMETERS:
 *   image - RGB image buffer (3 bytes per pixel)
 *   img_width - Image width in pixels
 *   img_height - Image height in pixels
 *   x - X position (left)
 *   y - Y position (top)
 *   c - Character to draw
 *   r, g, b - RGB color values
 *   scale - Character scale factor
 *
 * RETURNS:
 *   void
 *
 ****/
PRIVATE void drawChar(uint8_t *image, uint32_t img_width, uint32_t img_height,
                      uint32_t x, uint32_t y, char c,
                      uint8_t r, uint8_t g, uint8_t b, uint32_t scale)
{
    int font_idx = getFontIndex(c);
    uint32_t cx, cy, sx, sy;
    uint32_t pixel_offset;

    for (cy = 0; cy < FONT_HEIGHT; cy++) {
        uint8_t row = font_5x7[font_idx][cy];
        for (cx = 0; cx < 8; cx++) {  /* Check all 8 bits */
            if (row & (1 << (7 - cx))) {
                /* Draw scaled pixel */
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        uint32_t px = x + cx * scale + sx;
                        uint32_t py = y + cy * scale + sy;

                        if (px < img_width && py < img_height) {
                            pixel_offset = (py * img_width + px) * 3;
                            image[pixel_offset] = r;
                            image[pixel_offset + 1] = g;
                            image[pixel_offset + 2] = b;
                        }
                    }
                }
            }
        }
    }
}

/****
 *
 * Draw timestamp text
 *
 * DESCRIPTION:
 *   Renders timestamp string at bottom left of frame
 *
 * PARAMETERS:
 *   image - RGB image buffer
 *   img_width - Image width
 *   img_height - Image height
 *   timestamp - Time to display
 *
 * RETURNS:
 *   void
 *
 ****/
PRIVATE void drawTimestamp(uint8_t *image, uint32_t img_width, uint32_t img_height, time_t timestamp)
{
    char time_str[32];
    struct tm *tm_info;
    uint32_t x, i;
    uint32_t scale = 2;  /* 2x scale for readability */
    uint32_t char_spacing = (FONT_WIDTH + 2) * scale;

    tm_info = localtime(&timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Position at bottom left with margin */
    x = TIMESTAMP_MARGIN;
    uint32_t y = img_height - TIMESTAMP_HEIGHT + 5;

    /* Draw each character */
    for (i = 0; time_str[i] != '\0' && x + char_spacing < img_width; i++) {
        drawChar(image, img_width, img_height, x, y, time_str[i],
                255, 255, 255, scale);  /* White text */
        x += char_spacing;
    }
}

/****
 *
 * Initialize visualization system
 *
 * DESCRIPTION:
 *   Initializes the visualization subsystem with output configuration.
 *   Copies configuration parameters and sets initialization flag.
 *   Must be called before any rendering operations.
 *
 * PARAMETERS:
 *   config_in - Pointer to VisualizationConfig_t containing:
 *               - width: Output image width in pixels
 *               - height: Output image height in pixels
 *               - output_dir: Directory path for output frames
 *               - output_prefix: Filename prefix for frames
 *
 * RETURNS:
 *   TRUE (1) on success
 *   FALSE (0) if config_in is NULL
 *
 * SIDE EFFECTS:
 *   Copies config_in to static viz_config
 *   Sets viz_initialized flag to TRUE
 *   Prints debug message if config->debug >= 1
 *
 * ALGORITHM:
 *   1. Validate config_in pointer
 *   2. memcpy() config_in to viz_config
 *   3. Set viz_initialized = TRUE
 *   4. Print debug output if enabled
 *
 * PERFORMANCE:
 *   O(1) - Single memcpy() of small struct
 *   Typical: <100ns
 *
 * NOTES:
 *   - Must call before renderTimeBin() or writePPM()
 *   - Safe to call multiple times (overwrites previous config)
 *   - Does not allocate memory
 *   - Pairs with deInitVisualization()
 *
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
 *
 * Deinitialize visualization system and free resources
 *
 * DESCRIPTION:
 *   Cleans up visualization subsystem by freeing cached non-routable mask
 *   and clearing initialization flag. Safe to call multiple times.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Frees cached_nonroutable_mask if non-NULL
 *   Resets cached_nonroutable_mask to NULL
 *   Resets cached_mask_order to 0
 *   Resets cached_mask_dimension to 0
 *   Sets viz_initialized to FALSE
 *
 * ALGORITHM:
 *   1. Check if cached_nonroutable_mask is allocated
 *   2. If allocated, free with XFREE()
 *   3. Reset cache pointers and dimensions
 *   4. Set viz_initialized = FALSE
 *
 * PERFORMANCE:
 *   O(1) - Single XFREE() call
 *   Typical: <1μs
 *
 * NOTES:
 *   - Safe to call even if initVisualization() was never called
 *   - Safe to call multiple times
 *   - Should be called at program shutdown to free mask memory
 *   - Mask cache size: dimension² bytes (e.g., 4096² = 16MB)
 *
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
 *
 * Create non-routable IP space mask for Hilbert curve visualization
 *
 * DESCRIPTION:
 *   Generates a binary mask identifying Hilbert curve coordinates that
 *   correspond to non-routable IP addresses (RFC1918 private, loopback,
 *   multicast, etc.). Samples the IPv4 space efficiently to map IPs to
 *   coordinates and mark non-routable positions. Used for overlay visualization.
 *
 * PARAMETERS:
 *   order - Hilbert curve order (determines 2^order dimension)
 *   dimension - Hilbert curve dimension (must equal 2^order)
 *
 * RETURNS:
 *   Pointer to allocated uint8_t array of size dimension² on success
 *   NULL on allocation failure
 *   Array values: 0 = routable, 1 = non-routable
 *
 * SIDE EFFECTS:
 *   Allocates memory for mask array (caller must free if not cached)
 *   Prints debug messages if config->debug >= 2
 *
 * ALGORITHM:
 *   1. Allocate mask array: dimension * dimension bytes
 *   2. Initialize all to 0 (routable)
 *   3. Determine sample_step:
 *      - order <= 10: step = 64 (finer sampling)
 *      - order > 10: step = 256 (coarser sampling for large curves)
 *   4. For each IP in IPv4 space (0 to 0xFFFFFFFF) by sample_step:
 *      a. Check if isNonRoutableIP(ip)
 *      b. If yes, map IP to Hilbert coordinates
 *      c. Mark mask[y * dimension + x] = 1
 *   5. Explicitly check last IP (0xFFFFFFFF) to handle wraparound
 *   6. Print statistics if debug enabled
 *
 * PERFORMANCE:
 *   O((2³²/sample_step) * order) - IP space sampling + Hilbert mapping
 *   For order=12, step=256: ~16M IP checks, ~5-10 seconds
 *   Result is cached to avoid repeated expensive computation
 *
 * MEMORY:
 *   dimension² bytes
 *   Examples:
 *   - order 10 (1024×1024): 1 MB
 *   - order 12 (4096×4096): 16 MB
 *   - order 14 (16384×16384): 256 MB
 *
 * SAMPLING STRATEGY:
 *   Full IPv4 space: 4,294,967,296 addresses
 *   Hilbert points (order 12): 16,777,216 coordinates
 *   Sample every 256 IPs gives ~16.7M samples for good coverage
 *   Trade-off: Lower sample_step = better accuracy, slower generation
 *
 * NON-ROUTABLE RANGES DETECTED:
 *   - 10.0.0.0/8 (RFC1918)
 *   - 172.16.0.0/12 (RFC1918)
 *   - 192.168.0.0/16 (RFC1918)
 *   - 127.0.0.0/8 (loopback)
 *   - 224.0.0.0/4 (multicast)
 *   - Plus 8 other reserved ranges (see isNonRoutableIP())
 *
 * NOTES:
 *   - Result is typically cached by caller to amortize cost
 *   - Mask represents sampled approximation, not exact coverage
 *   - Higher order curves need coarser sampling to stay performant
 *
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
 *
 * Map attack intensity to color gradient
 *
 * DESCRIPTION:
 *   Converts event intensity values to RGB colors using a white→yellow→red
 *   gradient. Applies high base brightness (50% minimum) to ensure single
 *   events are clearly visible. Uses non-linear scaling for maximum visibility
 *   mode where even low-volume attacks stand out against black background.
 *
 * PARAMETERS:
 *   intensity - Event count at this coordinate (0 = no activity)
 *   max_intensity - Maximum intensity in current time bin (for normalization)
 *
 * RETURNS:
 *   RGB_t struct with r,g,b values (0-255 each)
 *
 * SIDE EFFECTS:
 *   None (pure function)
 *
 * ALGORITHM:
 *   1. If intensity == 0: return black (0,0,0)
 *   2. Normalize: normalized = intensity / max_intensity (0.0-1.0)
 *   3. Apply high base brightness: enhanced = 0.5 + 0.5 * normalized
 *      - Single attack (intensity=1, max=1000): ~50% brightness
 *      - Maximum attacks: 100% brightness
 *   4. Clamp enhanced to [0.5, 1.0] range
 *   5. Map enhanced [0.5, 1.0] to t [0.0, 1.0]: t = (enhanced - 0.5) / 0.5
 *   6. Apply color gradient based on t:
 *      - t < 0.5 (White→Yellow): R=255, G=255, B fades 255→0
 *      - t >= 0.5 (Yellow→Red): R=255, G fades 255→0, B=0
 *
 * PERFORMANCE:
 *   O(1) - Simple floating point arithmetic
 *   Typical: <20ns
 *
 * COLOR GRADIENT:
 *   intensity=0:    (0, 0, 0)      Black - no activity
 *   Low volume:     (255, 255, *)  White - single/few events clearly visible
 *   Medium volume:  (255, *, 0)    Yellow - moderate attacks
 *   High volume:    (255, 0, 0)    Red - large attack campaigns
 *
 * BRIGHTNESS FORMULA:
 *   enhanced = 0.5 + 0.5 * normalized
 *   This ensures minimum 50% brightness for ANY non-zero activity,
 *   making single events stand out clearly on black background.
 *
 * EXAMPLES:
 *   intensity=0, max=1000   → (0,0,0)        Black
 *   intensity=1, max=1000   → (255,255,255)  White (50% brightness)
 *   intensity=250, max=1000 → (255,255,127)  Light yellow (62.5%)
 *   intensity=500, max=1000 → (255,255,0)    Yellow (75%)
 *   intensity=750, max=1000 → (255,127,0)    Orange (87.5%)
 *   intensity=1000, max=1000 → (255,0,0)     Red (100%)
 *
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
 *
 * Write time bin heatmap as PPM image file
 *
 * DESCRIPTION:
 *   Renders heatmap to PPM format with color gradient and non-routable IP overlay.
 *   Centers square Hilbert curve in rectangular frame. Uses cached mask for efficiency.
 *   Renders residue map as dark grey layer for historical attack memory.
 *
 * PARAMETERS:
 *   filename - Output file path
 *   bin - TimeBin_t with heatmap data
 *   width - Output width in pixels
 *   height - Output height in pixels
 *   residue_map - Persistent attack memory bitmap (may be NULL)
 *
 * RETURNS:
 *   TRUE on success, FALSE on error
 *
 * SIDE EFFECTS:
 *   Creates/overwrites file, may create and cache non-routable mask
 *
 ****/
int writePPM(const char *filename, const TimeBin_t *bin, uint32_t width, uint32_t height, const uint8_t *residue_map)
{
    FILE *fp;
    uint32_t x, y, src_x, src_y;
    uint32_t intensity, idx;
    RGB_t color;
    uint8_t *nonroutable_mask = NULL;
    int is_nonroutable;
    uint8_t *image_buffer = NULL;
    uint32_t actual_height = height;
    uint32_t image_buffer_size;

    if (!filename || !bin || !bin->heatmap) {
        return FALSE;
    }

    /* Add extra height for timestamp if enabled */
    if (config->show_timestamp) {
        actual_height = height + TIMESTAMP_HEIGHT;
    }

    /* Allocate image buffer */
    image_buffer_size = actual_height * width * 3;  /* 3 bytes per pixel (RGB) */
    image_buffer = (uint8_t *)XMALLOC((int)image_buffer_size);
    if (!image_buffer) {
        fprintf(stderr, "ERR - Failed to allocate image buffer\n");
        return FALSE;
    }

    /* Initialize buffer to black */
    memset(image_buffer, 0, image_buffer_size);

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
        XFREE(image_buffer);
        /* Note: Do not free nonroutable_mask - it's cached */
        return FALSE;
    }

    /* Write PPM header (P6 = binary RGB) */
    fprintf(fp, "P6\n%u %u\n255\n", width, actual_height);

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

    /* Render heatmap to buffer */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint32_t pixel_offset = (y * width + x) * 3;

            /* Check if we're in the Hilbert curve area */
            if (x >= offset_x && x < offset_x + (uint32_t)((float)bin->dimension * scale) &&
                y >= offset_y && y < offset_y + (uint32_t)((float)bin->dimension * scale)) {

                /* Map back to source coordinates */
                src_x = (uint32_t)((float)(x - offset_x) / scale);
                src_y = (uint32_t)((float)(y - offset_y) / scale);

                if (src_x < bin->dimension && src_y < bin->dimension) {
                    idx = src_y * bin->dimension + src_x;
                    intensity = bin->heatmap[idx];
                    int residue_shown = FALSE;

                    /* Check residue map first - show grey for historical attacks with no current activity */
                    if (residue_map && residue_map[idx] && intensity == 0) {
                        /* Dark grey residue - persistent attack memory (15% darker than before) */
                        color.r = 54;
                        color.g = 54;
                        color.b = 54;
                        residue_shown = TRUE;
                    } else {
                        /* Normal heatmap color gradient */
                        color = intensityToColor(intensity, bin->max_intensity);
                    }

                    /* Apply dark blue overlay for non-routable IP space */
                    is_nonroutable = (nonroutable_mask && nonroutable_mask[idx]);
                    if (is_nonroutable && !residue_shown) {
                        /* If no activity and no residue, show dark blue base color
                         * If activity present, blend with moderately dark blue
                         * This makes private IP space visible against black background
                         * Note: Skip blue overlay if residue is shown to avoid obscuring it
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

            /* Store RGB pixel in buffer */
            image_buffer[pixel_offset] = color.r;
            image_buffer[pixel_offset + 1] = color.g;
            image_buffer[pixel_offset + 2] = color.b;
        }
    }

    /* Add timestamp overlay if enabled */
    if (config->show_timestamp) {
        drawTimestamp(image_buffer, width, actual_height, bin->bin_start);
    }

    /* Write buffer to file */
    if (fwrite(image_buffer, 1, image_buffer_size, fp) != image_buffer_size) {
        fprintf(stderr, "ERR - Failed to write image data to %s\n", filename);
        XFREE(image_buffer);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    XFREE(image_buffer);

    /* Note: Do not free nonroutable_mask here - it's cached for reuse */

#ifdef DEBUG
    if (config->debug >= 2) {
        fprintf(stderr, "DEBUG - Wrote PPM: %s (%ux%u)\n", filename, width, actual_height);
    }
#endif

    return TRUE;
}

/****
 *
 * Generate timestamped filename for time bin frame
 *
 * DESCRIPTION:
 *   Creates formatted filename: {dir}/{prefix}_{YYYYMMDD_HHMMSS}_{NNNN}.ppm
 *
 * PARAMETERS:
 *   buf - Output buffer
 *   buf_size - Buffer size
 *   dir - Directory (NULL = ".")
 *   prefix - Filename prefix (NULL = "frame")
 *   bin_start - Timestamp
 *   bin_num - Frame sequence number
 *
 * RETURNS:
 *   TRUE on success, FALSE if buf is NULL
 *
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
 *
 * Render time bin to image file
 *
 * DESCRIPTION:
 *   Wrapper that renders TimeBin_t to image file. Currently delegates to writePPM().
 *
 * PARAMETERS:
 *   bin - TimeBin_t to render
 *   output_path - Output file path
 *   width - Image width
 *   height - Image height
 *   residue_map - Persistent attack memory bitmap (may be NULL)
 *
 * RETURNS:
 *   TRUE on success, FALSE on error
 *
 ****/
int renderTimeBin(const TimeBin_t *bin, const char *output_path, uint32_t width, uint32_t height, const uint8_t *residue_map)
{
    if (!bin || !output_path) {
        return FALSE;
    }

    return writePPM(output_path, bin, width, height, residue_map);
}
