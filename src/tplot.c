/*****
 *
 * Description: Threat Plot Functions
 *
 * Copyright (c) 2008-2025, Ron Dilley
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

#include "tplot.h"
#include <sys/wait.h>  /* For waitpid() */
#include <glob.h>      /* For glob() */

/****
 *
 * typedefs
 *
 ****/

/**
 * Callback data structure for event processing
 */
typedef struct {
  uint64_t event_count;
  TimeBinManager_t *bin_manager;
  VisualizationConfig_t *viz_config;
} CallbackData_t;

/****
 *
 * local variables
 *
 ****/

/* Multi-file processing state */
PRIVATE TimeBinManager_t *g_bin_manager = NULL;
PRIVATE VisualizationConfig_t g_viz_config;
PRIVATE CallbackData_t g_callback_data;
PRIVATE int g_processing_initialized = FALSE;
PRIVATE time_t g_first_timestamp = 0;
PRIVATE time_t g_last_timestamp = 0;

/****
 *
 * global variables
 *
 ****/

/****
 *
 * external variables
 *
 ****/

/* errno and environ are provided by standard headers */
extern Config_t *config;
extern int quit;
extern int reload;

/* secure_fopen() is now in util.c */

/****
 *
 * Validate video codec against whitelist
 *
 * DESCRIPTION:
 *   Checks codec name against allowed list. Prevents command injection.
 *
 * PARAMETERS:
 *   codec - Codec name to validate
 *
 * RETURNS:
 *   TRUE if codec is in whitelist, FALSE otherwise
 *
 ****/
PRIVATE int isValidCodec(const char *codec)
{
  /* Whitelist of safe video codecs */
  static const char *allowed_codecs[] = {
    "libx264",
    "libx265",
    "libvpx",
    "libvpx-vp9",
    "h264",
    "hevc",
    "vp8",
    "vp9",
    NULL
  };

  if (!codec) {
    return FALSE;
  }

  for (int i = 0; allowed_codecs[i] != NULL; i++) {
    if (strcmp(codec, allowed_codecs[i]) == 0) {
      return TRUE;
    }
  }

  fprintf(stderr, "ERR - Invalid codec '%s'. Allowed codecs: ", codec);
  for (int i = 0; allowed_codecs[i] != NULL; i++) {
    fprintf(stderr, "%s%s", allowed_codecs[i],
            allowed_codecs[i + 1] ? ", " : "\n");
  }

  return FALSE;
}

/****
 *
 * Delete PPM frame files after video generation
 *
 * DESCRIPTION:
 *   Removes all frame_*.ppm files from output directory to save disk space.
 *   Uses glob() to find matching files, only called after successful video creation.
 *
 * PARAMETERS:
 *   output_dir - Directory containing frame files
 *
 * RETURNS:
 *   Number of files deleted, or -1 on error
 *
 ****/
PRIVATE int cleanup_frame_files(const char *output_dir)
{
  char pattern[PATH_MAX];
  int deleted_count = 0;

  /* Build glob pattern for frame files */
  snprintf(pattern, sizeof(pattern), "%s/frame_*.ppm", output_dir);

  /* Use glob to find all matching files */
  glob_t glob_result;
  memset(&glob_result, 0, sizeof(glob_result));

  int ret = glob(pattern, GLOB_NOSORT, NULL, &glob_result);

  if (ret == 0) {
    /* Delete each file */
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
      if (unlink(glob_result.gl_pathv[i]) == 0) {
        deleted_count++;
#ifdef DEBUG
        if (config->debug >= 3) {
          fprintf(stderr, "DEBUG - Deleted frame file: %s\n", glob_result.gl_pathv[i]);
        }
#endif
      } else {
        fprintf(stderr, "WARN - Failed to delete frame file: %s (%s)\n",
                glob_result.gl_pathv[i], strerror(errno));
      }
    }
    globfree(&glob_result);
  } else if (ret == GLOB_NOMATCH) {
    /* No files found - not an error */
    return 0;
  } else {
    fprintf(stderr, "WARN - Failed to glob frame files: %s\n", pattern);
    return -1;
  }

  if (deleted_count > 0) {
    fprintf(stderr, "Cleaned up %d frame files\n", deleted_count);
  }

  return deleted_count;
}

/****
 *
 * Execute ffmpeg to generate video from frames
 *
 * DESCRIPTION:
 *   Safely executes ffmpeg using fork/execvp to avoid shell injection.
 *   Validates codec, builds arguments, and waits for completion.
 *
 * PARAMETERS:
 *   output_dir - Directory containing frame files
 *   codec - Video codec (validated against whitelist)
 *   fps - Frames per second
 *   output_path - Output video file path
 *
 * RETURNS:
 *   0 on success, -1 or ffmpeg exit code on failure
 *
 ****/
PRIVATE int execute_ffmpeg(const char *output_dir, const char *codec,
                           uint32_t fps, const char *output_path)
{
  pid_t pid;
  int status;
  char fps_str[16];
  char input_pattern[PATH_MAX];

  /* Build arguments */
  snprintf(fps_str, sizeof(fps_str), "%u", fps);
  snprintf(input_pattern, sizeof(input_pattern), "%s/frame_*.ppm", output_dir);

  /* Validate codec */
  if (!isValidCodec(codec)) {
    return -1;
  }

  pid = fork();

  if (pid == -1) {
    fprintf(stderr, "ERR - Failed to fork process: %s\n", strerror(errno));
    return -1;
  }

  if (pid == 0) {
    /* Child process - execute ffmpeg
     * POSIX API QUIRK: execvp() takes char*[] not const char*[] despite
     * never modifying strings. Suppress unavoidable cast-qual warnings.
     */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
    char *args[] = {
      (char *)"ffmpeg",
      (char *)"-y",                    /* Overwrite output file */
      (char *)"-framerate", fps_str,
      (char *)"-pattern_type", (char *)"glob",
      (char *)"-i", input_pattern,
      (char *)"-c:v", (char *)codec,
      (char *)"-preset", (char *)"medium",
      (char *)"-crf", (char *)"23",
      (char *)"-pix_fmt", (char *)"yuv420p",
      (char *)output_path,
      NULL
    };
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

    /* Redirect stderr to stdout to suppress ffmpeg output */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    execvp("ffmpeg", args);

    /* If execvp returns, it failed */
    fprintf(stderr, "ERR - Failed to execute ffmpeg: %s\n", strerror(errno));
    _exit(127);  /* Use _exit in child process */
  }

  /* Parent process - wait for child */
  if (waitpid(pid, &status, 0) == -1) {
    fprintf(stderr, "ERR - Failed to wait for ffmpeg: %s\n", strerror(errno));
    return -1;
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr, "ERR - ffmpeg terminated by signal %d\n", WTERMSIG(status));
    return -1;
  }

  return -1;
}

/****
 *
 * functions
 *
 ****/

/****
 *
 * Process honeypot log events
 *
 * DESCRIPTION:
 *   Callback for log parser. Maps IP to Hilbert coordinates, tracks time span,
 *   processes events into bins, renders frames when bins complete, applies decay.
 *
 * PARAMETERS:
 *   event - Honeypot event data (src IP, timestamp, etc.)
 *   user_data - CallbackData_t with bin manager and config
 *
 * RETURNS:
 *   TRUE to continue processing, FALSE to stop
 *
 ****/
PRIVATE int honeypotEventCallback(const HoneypotEvent_t *event, void *user_data)
{
  CallbackData_t *data = (CallbackData_t *)user_data;
  HilbertCoord_t coord;
  TimeBin_t *old_bin = NULL;
  char output_path[PATH_MAX];

  data->event_count++;

  /* Track time span for auto-scaling */
  if (g_first_timestamp == 0 || event->timestamp < g_first_timestamp) {
    g_first_timestamp = event->timestamp;
  }
  if (event->timestamp > g_last_timestamp) {
    g_last_timestamp = event->timestamp;
  }

  /* Map IP to Hilbert curve coordinates */
  coord = ipToHilbert(event->src_ip, HILBERT_ORDER_DEFAULT);

#ifdef DEBUG
  /* Print first 10 events for verification (debug mode only) */
  if (config->debug >= 2 && data->event_count <= 10) {
    fprintf(stderr, "DEBUG - Event %lu: %s:%u -> %s:%u proto=%s time=%ld.%06u Hilbert(%u,%u)\n",
            data->event_count,
            event->src_ip_str, event->src_port,
            event->dst_ip_str, event->dst_port,
            event->protocol == PROTO_TCP ? "TCP" : "UDP",
            (long)event->timestamp, event->timestamp_us,
            coord.x, coord.y);
  }
#endif

  /* Check if this event triggers a new time bin */
  time_t event_bin = getBinForTime(event->timestamp, data->bin_manager->config.bin_seconds);

  if (data->bin_manager->current_bin &&
      event_bin != data->bin_manager->current_bin->bin_start) {
    /* Finalize and render the current bin before moving to next */
    old_bin = data->bin_manager->current_bin;

    /* Apply decay cache to show fading IPs */
    applyDecayToHeatmap(data->bin_manager, old_bin);

    /* Clean expired cache entries periodically */
    if (data->bin_manager->bins_written % 10 == 0) {
      cleanExpiredCacheEntries(data->bin_manager, old_bin->bin_start);
    }

    finalizeBin(old_bin);

    /* Generate output filename and render */
    generateBinFilename(output_path, sizeof(output_path),
                       data->viz_config->output_dir,
                       data->viz_config->output_prefix,
                       old_bin->bin_start,
                       data->bin_manager->bins_written);

    if (renderTimeBin(old_bin, output_path,
                      data->viz_config->width,
                      data->viz_config->height)) {
      data->bin_manager->bins_written++;
#ifdef DEBUG
      if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Wrote frame %u: %s (events=%u, unique_ips=%u, max_intensity=%u, cached=%u)\n",
                data->bin_manager->bins_written - 1, output_path,
                old_bin->event_count, old_bin->unique_ips, old_bin->max_intensity,
                data->bin_manager->cache_size);
      }
#endif
    } else {
      fprintf(stderr, "ERR - Failed to write frame: %s\n", output_path);
    }
  }

  /* Process event into time bin manager */
  if (!processEvent(data->bin_manager, event->timestamp, coord.x, coord.y)) {
    fprintf(stderr, "ERR - Failed to process event at time %ld\n",
            (long)event->timestamp);
    return FALSE;
  }

  return TRUE;  /* Continue processing */
}

/****
 *
 * Process single honeypot log file
 *
 * DESCRIPTION:
 *   Complete pipeline for single-file processing. Initializes all subsystems,
 *   processes log file, renders frames, generates video, cleans up.
 *
 * PARAMETERS:
 *   fName - Path to gzip compressed log file
 *
 * RETURNS:
 *   EXIT_SUCCESS or EXIT_FAILURE
 *
 ****/
int processHoneypotFile(const char *fName)
{
  CallbackData_t callback_data;
  TimeBinConfig_t bin_config;
  VisualizationConfig_t viz_config;
  char output_path[PATH_MAX];

  fprintf(stderr, "Processing honeypot log file: %s\n", fName);
  fprintf(stderr, "Time bin period: %s\n", formatTimeBinDuration(config->time_bin_seconds));

  /* Setup time bin configuration */
  bin_config.bin_seconds = config->time_bin_seconds;
  bin_config.start_time = 0;  /* Auto-detect from first event */
  bin_config.end_time = 0;    /* Process all events */
  bin_config.hilbert_order = HILBERT_ORDER_DEFAULT;
  bin_config.dimension = 1 << HILBERT_ORDER_DEFAULT;  /* 2^order */
  bin_config.decay_seconds = DECAY_CACHE_DURATION_DEFAULT;  /* 1 hour decay */

  /* Setup visualization configuration */
  viz_config.width = config->viz_width;
  viz_config.height = config->viz_height;
  viz_config.output_dir = config->output_dir ? config->output_dir : "plots";
  viz_config.output_prefix = "frame";

  fprintf(stderr, "Output directory: %s\n", viz_config.output_dir);
  fprintf(stderr, "Resolution: %ux%u\n", viz_config.width, viz_config.height);

  /* Create output directory if it doesn't exist */
  if (mkdir(viz_config.output_dir, 0755) != 0 && errno != EEXIST) {
    fprintf(stderr, "ERR - Failed to create output directory: %s\n", viz_config.output_dir);
    return EXIT_FAILURE;
  }

  /* Initialize Hilbert curve engine */
  if (!initHilbert(HILBERT_ORDER_DEFAULT)) {
    fprintf(stderr, "ERR - Failed to initialize Hilbert curve engine\n");
    return EXIT_FAILURE;
  }

  /* Load CIDR mapping for proportional timezone-based positioning */
  if (config->cidr_map_file && config->cidr_map_file[0] != '\0') {
    fprintf(stderr, "Loading CIDR mapping: %s\n", config->cidr_map_file);
    if (!loadCIDRMapping(config->cidr_map_file)) {
      fprintf(stderr, "WARN - Failed to load CIDR mapping, using fallback hash-based distribution\n");
    }
  } else {
    /* Try default location */
    if (!loadCIDRMapping("cidr_map.txt")) {
      fprintf(stderr, "WARN - No CIDR mapping found, using fallback hash-based distribution\n");
    }
  }

  /* Initialize visualization system */
  if (!initVisualization(&viz_config)) {
    fprintf(stderr, "ERR - Failed to initialize visualization\n");
    deInitHilbert();
    return EXIT_FAILURE;
  }

  /* Initialize log parser */
  if (!initLogParser()) {
    fprintf(stderr, "ERR - Failed to initialize log parser\n");
    deInitVisualization();
    deInitHilbert();
    return EXIT_FAILURE;
  }

  /* Create time bin manager */
  callback_data.bin_manager = createTimeBinManager(&bin_config);
  if (!callback_data.bin_manager) {
    fprintf(stderr, "ERR - Failed to create time bin manager\n");
    deInitLogParser();
    deInitVisualization();
    deInitHilbert();
    return EXIT_FAILURE;
  }

  callback_data.event_count = 0;
  callback_data.viz_config = &viz_config;

  /* Process the gzip file */
  if (!processGzipFile(fName, honeypotEventCallback, &callback_data)) {
    fprintf(stderr, "ERR - Failed to process honeypot log file\n");
    destroyTimeBinManager(callback_data.bin_manager);
    deInitLogParser();
    deInitVisualization();
    deInitHilbert();
    return EXIT_FAILURE;
  }

  /* Finalize and render the last bin if it exists */
  if (callback_data.bin_manager->current_bin) {
    /* Apply decay cache to final bin */
    applyDecayToHeatmap(callback_data.bin_manager, callback_data.bin_manager->current_bin);

    finalizeBin(callback_data.bin_manager->current_bin);

    generateBinFilename(output_path, sizeof(output_path),
                       viz_config.output_dir,
                       viz_config.output_prefix,
                       callback_data.bin_manager->current_bin->bin_start,
                       callback_data.bin_manager->bins_written);

    if (renderTimeBin(callback_data.bin_manager->current_bin, output_path,
                      viz_config.width, viz_config.height)) {
      callback_data.bin_manager->bins_written++;
#ifdef DEBUG
      if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Wrote final frame %u: %s (events=%u, unique_ips=%u, max_intensity=%u, cached=%u)\n",
                callback_data.bin_manager->bins_written - 1, output_path,
                callback_data.bin_manager->current_bin->event_count,
                callback_data.bin_manager->current_bin->unique_ips,
                callback_data.bin_manager->current_bin->max_intensity,
                callback_data.bin_manager->cache_size);
      }
#endif
    } else {
      fprintf(stderr, "ERR - Failed to write final frame: %s\n", output_path);
    }
  }

  fprintf(stderr, "\nSummary:\n");
  fprintf(stderr, "========\n");
  fprintf(stderr, "Total honeypot events processed: %lu\n", callback_data.event_count);
  fprintf(stderr, "Total frames written: %u\n", callback_data.bin_manager->bins_written);
  fprintf(stderr, "Average events per frame: %.1f\n",
          (float)callback_data.event_count / (float)callback_data.bin_manager->bins_written);

  /* Generate video if requested */
  if (config->generate_video && callback_data.bin_manager->bins_written > 0) {
    char video_path[PATH_MAX];
    int ret;

    snprintf(video_path, sizeof(video_path), "%s/output.mp4", viz_config.output_dir);

    fprintf(stderr, "\nGenerating video: %s\n", video_path);
    fprintf(stderr, "Codec: %s, FPS: %u\n", config->video_codec, config->video_fps);

    /* Execute ffmpeg safely without shell interpretation */
    fprintf(stderr, "Running: ffmpeg...\n");
    ret = execute_ffmpeg(viz_config.output_dir, config->video_codec,
                        config->video_fps, video_path);

    if (ret == 0) {
      fprintf(stderr, "Video created successfully: %s\n", video_path);

      /* Clean up frame files after successful video generation */
      cleanup_frame_files(viz_config.output_dir);
    } else {
      fprintf(stderr, "WARNING - ffmpeg returned exit code: %d\n", ret);
      fprintf(stderr, "Video may still have been created. Check: %s\n", video_path);
      fprintf(stderr, "Frame files retained for inspection\n");
    }
  }

  /* Cleanup */
  destroyTimeBinManager(callback_data.bin_manager);
  deInitLogParser();
  deInitVisualization();
  deInitHilbert();

  return EXIT_SUCCESS;
}

/****
 *
 * Multi-file processing interface
 *
 ****/

/****
 *
 * Initialize multi-file processing pipeline
 *
 * DESCRIPTION:
 *   Sets up subsystems for processing multiple log files into single timeline.
 *   Creates output directory, initializes Hilbert/visualization/parser, loads CIDR map.
 *
 * RETURNS:
 *   EXIT_SUCCESS or EXIT_FAILURE
 *
 ****/
int initProcessing(void)
{
  TimeBinConfig_t bin_config;

  if (g_processing_initialized) {
    fprintf(stderr, "ERR - Processing already initialized\n");
    return EXIT_FAILURE;
  }

  /* Reset timestamp tracking for auto-scaling */
  g_first_timestamp = 0;
  g_last_timestamp = 0;

  fprintf(stderr, "Time bin period: %s\n", formatTimeBinDuration(config->time_bin_seconds));

  /* Setup time bin configuration */
  bin_config.bin_seconds = config->time_bin_seconds;
  bin_config.start_time = 0;  /* Auto-detect from first event */
  bin_config.end_time = 0;    /* Process all events */
  bin_config.hilbert_order = HILBERT_ORDER_DEFAULT;
  bin_config.dimension = 1 << HILBERT_ORDER_DEFAULT;  /* 2^order */
  bin_config.decay_seconds = DECAY_CACHE_DURATION_DEFAULT;

  /* Setup visualization configuration */
  g_viz_config.width = config->viz_width;
  g_viz_config.height = config->viz_height;
  g_viz_config.output_dir = config->output_dir ? config->output_dir : "plots";
  g_viz_config.output_prefix = "frame";

  fprintf(stderr, "Output directory: %s\n", g_viz_config.output_dir);
  fprintf(stderr, "Resolution: %ux%u\n", g_viz_config.width, g_viz_config.height);

  /* Create output directory if it doesn't exist */
  if (mkdir(g_viz_config.output_dir, 0755) != 0 && errno != EEXIST) {
    fprintf(stderr, "ERR - Failed to create output directory: %s\n", g_viz_config.output_dir);
    return EXIT_FAILURE;
  }

  /* Initialize Hilbert curve engine */
  if (!initHilbert(HILBERT_ORDER_DEFAULT)) {
    fprintf(stderr, "ERR - Failed to initialize Hilbert curve engine\n");
    return EXIT_FAILURE;
  }

  /* Load CIDR mapping for proportional timezone-based positioning */
  if (config->cidr_map_file && config->cidr_map_file[0] != '\0') {
    fprintf(stderr, "Loading CIDR mapping: %s\n", config->cidr_map_file);
    if (!loadCIDRMapping(config->cidr_map_file)) {
      fprintf(stderr, "WARN - Failed to load CIDR mapping, using direct Hilbert mapping\n");
    }
  } else {
    /* Try default location */
    if (!loadCIDRMapping("cidr_map.txt")) {
      fprintf(stderr, "WARN - No CIDR mapping found, using direct Hilbert mapping\n");
    }
  }

  /* Initialize visualization system */
  if (!initVisualization(&g_viz_config)) {
    fprintf(stderr, "ERR - Failed to initialize visualization\n");
    deInitHilbert();
    return EXIT_FAILURE;
  }

  /* Initialize log parser */
  if (!initLogParser()) {
    fprintf(stderr, "ERR - Failed to initialize log parser\n");
    deInitVisualization();
    deInitHilbert();
    return EXIT_FAILURE;
  }

  /* Create time bin manager */
  g_bin_manager = createTimeBinManager(&bin_config);
  if (!g_bin_manager) {
    fprintf(stderr, "ERR - Failed to create time bin manager\n");
    deInitLogParser();
    deInitVisualization();
    deInitHilbert();
    return EXIT_FAILURE;
  }

  g_callback_data.event_count = 0;
  g_callback_data.bin_manager = g_bin_manager;
  g_callback_data.viz_config = &g_viz_config;

  g_processing_initialized = TRUE;

  return EXIT_SUCCESS;
}

/****
 *
 * Process single file into existing timeline
 *
 * DESCRIPTION:
 *   Processes one log file and adds events to global timeline.
 *   Must be called after initProcessing(). Can be called multiple times.
 *
 * PARAMETERS:
 *   fName - Path to gzip log file
 *
 * RETURNS:
 *   EXIT_SUCCESS or EXIT_FAILURE
 *
 ****/
int processFileIntoTimeline(const char *fName)
{
  if (!g_processing_initialized) {
    fprintf(stderr, "ERR - Processing not initialized. Call initProcessing() first\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr, "\nProcessing: %s\n", fName);

  /* Process the gzip file */
  if (!processGzipFile(fName, honeypotEventCallback, &g_callback_data)) {
    fprintf(stderr, "ERR - Failed to process file: %s\n", fName);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

/****
 *
 * Finalize multi-file processing and generate video
 *
 * DESCRIPTION:
 *   Completes timeline processing, auto-scales FPS/decay based on data span,
 *   renders final frame, generates video, cleans up subsystems.
 *
 * RETURNS:
 *   EXIT_SUCCESS or EXIT_FAILURE
 *
 ****/
int finalizeProcessing(void)
{
  char output_path[PATH_MAX];
  double data_span_days;
  uint32_t calculated_fps;
  uint32_t calculated_decay_seconds;

  if (!g_processing_initialized) {
    fprintf(stderr, "ERR - Processing not initialized\n");
    return EXIT_FAILURE;
  }

  /* Calculate auto-scaled FPS and decay based on data time span */
  if (config->auto_scale && g_first_timestamp > 0 && g_last_timestamp > g_first_timestamp) {
    /* Calculate time span in days */
    data_span_days = (double)(g_last_timestamp - g_first_timestamp) / 86400.0;

    fprintf(stderr, "\nData time span: %.2f days (%ld to %ld)\n",
            data_span_days,
            (long)g_first_timestamp,
            (long)g_last_timestamp);

    /* Baseline: 1 day = 3 FPS, 3 hours decay
     * Scaling: N days = N*3 FPS, N*3 hours decay
     */
    calculated_fps = (uint32_t)(data_span_days * 3.0 + 0.5);  /* Round to nearest */
    if (calculated_fps < 1) calculated_fps = 1;
    if (calculated_fps > 120) calculated_fps = 120;  /* Cap at 120 FPS */

    calculated_decay_seconds = (uint32_t)(data_span_days * 3.0 * 3600.0);  /* N * 3 hours */
    if (calculated_decay_seconds < 3600) calculated_decay_seconds = 3600;  /* Minimum 1 hour */

    /* Update config for video generation */
    config->video_fps = calculated_fps;

    /* Update decay in bin manager config */
    g_bin_manager->config.decay_seconds = calculated_decay_seconds;

    fprintf(stderr, "Auto-scaled: FPS=%u, Decay=%uh (%.1f days x 3)\n",
            config->video_fps,
            calculated_decay_seconds / 3600,
            data_span_days);
  }

  /* Finalize and render the last bin if it exists */
  if (g_bin_manager->current_bin) {
    /* Apply decay cache to final bin */
    applyDecayToHeatmap(g_bin_manager, g_bin_manager->current_bin);

    finalizeBin(g_bin_manager->current_bin);

    generateBinFilename(output_path, sizeof(output_path),
                       g_viz_config.output_dir,
                       g_viz_config.output_prefix,
                       g_bin_manager->current_bin->bin_start,
                       g_bin_manager->bins_written);

    if (renderTimeBin(g_bin_manager->current_bin, output_path,
                      g_viz_config.width, g_viz_config.height)) {
      g_bin_manager->bins_written++;
#ifdef DEBUG
      if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Wrote final frame %u: %s (events=%u, unique_ips=%u, max_intensity=%u, cached=%u)\n",
                g_bin_manager->bins_written - 1, output_path,
                g_bin_manager->current_bin->event_count,
                g_bin_manager->current_bin->unique_ips,
                g_bin_manager->current_bin->max_intensity,
                g_bin_manager->cache_size);
      }
#endif
    } else {
      fprintf(stderr, "ERR - Failed to write final frame: %s\n", output_path);
    }
  }

  fprintf(stderr, "\nSummary:\n");
  fprintf(stderr, "========\n");
  fprintf(stderr, "Total honeypot events processed: %lu\n", g_callback_data.event_count);
  fprintf(stderr, "Total frames written: %u\n", g_bin_manager->bins_written);
  if (g_bin_manager->bins_written > 0) {
    fprintf(stderr, "Average events per frame: %.1f\n",
            (float)g_callback_data.event_count / (float)g_bin_manager->bins_written);
  }

  /* Generate video if requested */
  if (config->generate_video && g_bin_manager->bins_written > 0) {
    char video_path[PATH_MAX];
    int ret;

    snprintf(video_path, sizeof(video_path), "%s/output.mp4", g_viz_config.output_dir);

    fprintf(stderr, "\nGenerating video: %s\n", video_path);
    fprintf(stderr, "Codec: %s, FPS: %u\n", config->video_codec, config->video_fps);

    /* Execute ffmpeg safely without shell interpretation */
    fprintf(stderr, "Running: ffmpeg...\n");
    ret = execute_ffmpeg(g_viz_config.output_dir, config->video_codec,
                        config->video_fps, video_path);

    if (ret == 0) {
      fprintf(stderr, "Video created successfully: %s\n", video_path);

      /* Clean up frame files after successful video generation */
      cleanup_frame_files(g_viz_config.output_dir);
    } else {
      fprintf(stderr, "WARNING - ffmpeg returned exit code: %d\n", ret);
      fprintf(stderr, "Video may still have been created. Check: %s\n", video_path);
      fprintf(stderr, "Frame files retained for inspection\n");
    }
  }

  /* Cleanup */
  destroyTimeBinManager(g_bin_manager);
  deInitLogParser();
  deInitVisualization();
  deInitHilbert();

  g_bin_manager = NULL;
  g_processing_initialized = FALSE;

  return EXIT_SUCCESS;
}