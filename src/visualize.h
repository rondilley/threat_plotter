/*****
 *
 * Description: Visualization Output Headers
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

#ifndef VISUALIZE_DOT_H
#define VISUALIZE_DOT_H

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
#include "timebin.h"
#include <stdint.h>

/****
 *
 * defines
 *
 ****/

/* Common aspect ratio resolutions */
#define VIZ_WIDTH_720P    1280
#define VIZ_HEIGHT_720P   720

#define VIZ_WIDTH_1080P   1920
#define VIZ_HEIGHT_1080P  1080

#define VIZ_WIDTH_1440P   2560
#define VIZ_HEIGHT_1440P  1440

#define VIZ_WIDTH_UWQHD   3440  /* 21:9 ultrawide */
#define VIZ_HEIGHT_UWQHD  1440

#define VIZ_WIDTH_4K      3840
#define VIZ_HEIGHT_4K     2160

#define VIZ_WIDTH_DEFAULT  VIZ_WIDTH_UWQHD
#define VIZ_HEIGHT_DEFAULT VIZ_HEIGHT_UWQHD

/****
 *
 * typedefs & structs
 *
 ****/

/**
 * RGB color
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGB_t;

/**
 * Visualization configuration
 */
typedef struct {
    uint32_t width;          /* Output image width */
    uint32_t height;         /* Output image height */
    const char *output_dir;  /* Output directory for frames */
    const char *output_prefix; /* Filename prefix for frames */
} VisualizationConfig_t;

/****
 *
 * function prototypes
 *
 ****/

/* Initialization */
int initVisualization(VisualizationConfig_t *config);
void deInitVisualization(void);

/* Color mapping */
RGB_t intensityToColor(uint32_t intensity, uint32_t max_intensity);

/* PPM output */
int writePPM(const char *filename, const TimeBin_t *bin, uint32_t width, uint32_t height, const uint8_t *residue_map);

/* Render time bin to image file */
int renderTimeBin(const TimeBin_t *bin, const char *output_path, uint32_t width, uint32_t height, const uint8_t *residue_map);

/* Generate filename for bin */
int generateBinFilename(char *buf, size_t buf_size, const char *dir,
                       const char *prefix, time_t bin_start, uint32_t bin_num);

#endif /* VISUALIZE_DOT_H */
