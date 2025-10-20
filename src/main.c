/*****
 *
 * Description: Main Functions
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
 *.
 ****/

#include "main.h"
#include "timebin.h"

/****
 *
 * local variables
 *
 ****/

/****
 *
 * global variables
 *
 ****/

/* Signal handler variables - must be volatile sig_atomic_t for safety */
PUBLIC volatile sig_atomic_t quit = 0;
PUBLIC volatile sig_atomic_t reload = 0;
PUBLIC Config_t *config = NULL;

/****
 *
 * external variables
 *
 ****/

/* errno and environ are provided by standard headers */

/****
 *
 * Parse integer from string with bounds checking
 *
 * DESCRIPTION:
 *   Safely parses integer with validation for conversion errors, incomplete parsing,
 *   and bounds violations.
 *
 * PARAMETERS:
 *   str - String to parse
 *   min_val - Minimum allowed value
 *   max_val - Maximum allowed value
 *   result - Output parameter for parsed value
 *
 * RETURNS:
 *   TRUE on success, FALSE on error or out of range
 *
 ****/
PRIVATE int safe_parse_int(const char *str, int min_val, int max_val, int *result)
{
  char *endptr;
  long val;
  
  if (!str || !result) {
    return FALSE;
  }
  
  errno = 0;
  val = strtol(str, &endptr, 10);
  
  /* Check for conversion errors */
  if (errno == ERANGE || errno == EINVAL) {
    return FALSE;
  }
  
  /* Check if entire string was consumed */
  if (*endptr != '\0') {
    return FALSE;
  }
  
  /* Check bounds */
  if (val < min_val || val > max_val) {
    return FALSE;
  }
  
  *result = (int)val;
  return TRUE;
}

/****
 *
 * Validate file path against security policy
 *
 * DESCRIPTION:
 *   Resolves path using realpath() to detect symlinks and relative paths, then
 *   checks against blacklist of system directories.
 *
 * PARAMETERS:
 *   path - File path to validate
 *
 * RETURNS:
 *   TRUE if path is safe, FALSE if blacklisted or invalid
 *
 * SIDE EFFECTS:
 *   Allocates temporary memory for path manipulation
 *
 ****/
PRIVATE int validate_file_path(const char *path)
{
  char resolved[PATH_MAX];
  char *resolved_ptr = NULL;
  char *parent_dir = NULL;
  char *path_copy = NULL;
  int result = FALSE;

  if (!path) {
    return FALSE;
  }

  /* Check path length */
  if (strlen(path) >= PATH_MAX) {
    fprintf(stderr, "ERR - Path too long: %s\n", path);
    return FALSE;
  }

  /* Attempt to resolve the full path */
  resolved_ptr = realpath(path, resolved);

  if (resolved_ptr == NULL) {
    /* Path doesn't exist yet - validate parent directory */
    if (errno == ENOENT) {
      /* Copy path for dirname() which may modify it */
      path_copy = strdup(path);
      if (!path_copy) {
        fprintf(stderr, "ERR - Memory allocation failed\n");
        return FALSE;
      }

      /* Get parent directory */
      parent_dir = dirname(path_copy);

      /* Try to resolve parent */
      resolved_ptr = realpath(parent_dir, resolved);
      free(path_copy);

      if (resolved_ptr == NULL) {
        fprintf(stderr, "ERR - Cannot resolve parent directory: %s\n", parent_dir);
        return FALSE;
      }
    } else {
      /* Other error - cannot validate */
      fprintf(stderr, "ERR - Cannot resolve path: %s (%s)\n", path, strerror(errno));
      return FALSE;
    }
  }

  /* Check resolved path against blacklist */
  if (strncmp(resolved, "/etc/", 5) == 0 ||
      strncmp(resolved, "/proc/", 6) == 0 ||
      strncmp(resolved, "/sys/", 5) == 0 ||
      strncmp(resolved, "/dev/", 5) == 0 ||
      strncmp(resolved, "/boot/", 6) == 0 ||
      strncmp(resolved, "/root/", 6) == 0) {
    fprintf(stderr, "ERR - Access to system directory denied: %s -> %s\n", path, resolved);
    result = FALSE;
  } else {
    result = TRUE;
  }

  return result;
}

/* secure_fopen() is now in util.c */

/****
 *
 * main function
 *
 ****/

int main(int argc, char *argv[])
{
  PRIVATE int c = 0;

#ifndef DEBUG
  struct rlimit rlim;

  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);
#endif

  /* setup config */
  config = (Config_t *)XMALLOC(sizeof(Config_t));
  XMEMSET(config, 0, sizeof(Config_t));

  /* force mode to forground */
  config->mode = MODE_INTERACTIVE;

  /* store current pid */
  config->cur_pid = getpid();

  /* get real uid and gid in prep for priv drop */
  config->gid = getgid();
  config->uid = getuid();

  /* set visualization defaults */
  config->time_bin_seconds = 60;  /* 1 minute default */
  config->output_dir = NULL;
  config->viz_width = 4096;       /* Match Hilbert curve dimension (2^12) */
  config->viz_height = 4096;
  config->generate_video = 1;     /* Generate video by default */
  config->video_fps = 3;          /* 3 FPS default (auto-scaled based on data span) */
  config->video_codec = "libx264"; /* H.264 codec default */
  config->cidr_map_file = NULL;   /* Will try default location */
  config->target_video_duration = 300;  /* 5 minutes default */
  config->auto_scale = 1;         /* Auto-scale FPS and decay by default */
  config->show_timestamp = 0;     /* Timestamp overlay off by default */

  while (1)
  {
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {
        {"version", no_argument, 0, 'v'},
        {"debug", required_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"period", required_argument, 0, 'p'},
        {"output", required_argument, 0, 'o'},
        {"no-video", no_argument, 0, 'V'},
        {"fps", required_argument, 0, 'f'},
        {"codec", required_argument, 0, 'c'},
        {"cidr-map", required_argument, 0, 'C'},
        {"duration", required_argument, 0, 'D'},
        {"timestamp", no_argument, 0, 't'},
        {0, no_argument, 0, 0}};
    c = getopt_long(argc, argv, "vd:hp:o:Vf:c:C:D:t", long_options, &option_index);
#else
    c = getopt(argc, argv, "vd:hp:o:Vf:c:C:D:t");
#endif

    if (c EQ - 1)
      break;

    switch (c)
    {

    case 'v':
      /* show the version */
      print_version();
      return (EXIT_SUCCESS);

    case 'd':
      /* show debug info */
      if (!safe_parse_int(optarg, 0, 9, &config->debug)) {
        fprintf(stderr, "ERR - Invalid debug level: %s (must be 0-9)\n", optarg);
        return (EXIT_FAILURE);
      }
      break;

    case 'h':
      /* show help info */
      print_help();
      return (EXIT_SUCCESS);

    case 'p':
      /* set time bin period */
      if (!parseTimeBinDuration(optarg, &config->time_bin_seconds)) {
        fprintf(stderr, "ERR - Invalid time period: %s (use format: 1m, 5m, 60m, etc.)\n", optarg);
        return (EXIT_FAILURE);
      }
      break;

    case 'o':
      /* set output directory */
      if (!validate_file_path(optarg)) {
        fprintf(stderr, "ERR - Invalid output directory: %s\n", optarg);
        return (EXIT_FAILURE);
      }
      config->output_dir = optarg;
      break;

    case 'V':
      /* disable video generation */
      config->generate_video = 0;
      break;

    case 'f':
      /* set video framerate */
      if (!safe_parse_int(optarg, 1, 120, (int *)&config->video_fps)) {
        fprintf(stderr, "ERR - Invalid framerate: %s (must be 1-120)\n", optarg);
        return (EXIT_FAILURE);
      }
      break;

    case 'c':
      /* set video codec */
      config->video_codec = optarg;
      break;

    case 'C':
      /* set CIDR mapping file */
      if (!validate_file_path(optarg)) {
        fprintf(stderr, "ERR - Invalid CIDR map file: %s\n", optarg);
        return (EXIT_FAILURE);
      }
      config->cidr_map_file = optarg;
      break;

    case 'D':
      /* set target video duration */
      if (!safe_parse_int(optarg, 10, 3600, (int *)&config->target_video_duration)) {
        fprintf(stderr, "ERR - Invalid video duration: %s (must be 10-3600 seconds)\n", optarg);
        return (EXIT_FAILURE);
      }
      break;

    case 't':
      /* enable timestamp overlay */
      config->show_timestamp = 1;
      break;

    default:
      fprintf(stderr, "Unknown option code [0%o]\n", c);
    }
  }

  /* override cluster depth */
  if ((config->clusterDepth <= 0) || (config->clusterDepth > 10000))
    config->clusterDepth = MAX_ARGS_IN_FIELD;

  /* check dirs and files for danger */

  if (time(&config->current_time) EQ - 1)
  {
    display(LOG_ERR, "Unable to get current time");

    /* cleanup buffers */
    cleanup();
    return (EXIT_FAILURE);
  }

  /* initialize program wide config options */
  config->hostname = (char *)XMALLOC(MAXHOSTNAMELEN + 1);

  /* get processor hostname */
  if (gethostname(config->hostname, MAXHOSTNAMELEN) != 0)
  {
    display(LOG_ERR, "Unable to get hostname");
    strncpy(config->hostname, "unknown", MAXHOSTNAMELEN);
    config->hostname[MAXHOSTNAMELEN] = '\0';
  }

  config->cur_pid = getpid();

  /* setup current time updater */
  signal(SIGALRM, ctime_prog);
  alarm(ALARM_TIMER);

  /*
   * get to work
   */

  /* Initialize processing for all files */
  if (initProcessing() != EXIT_SUCCESS) {
    fprintf(stderr, "ERR - Failed to initialize processing\n");
    cleanup();
    return (EXIT_FAILURE);
  }

  /* Process all the files into a single timeline */
  while (optind < argc)
  {
    /* Update current time in main loop (not in signal handler) */
    if (time(&config->current_time) EQ - 1) {
      display(LOG_ERR, "Unable to update current time");
      finalizeProcessing();
      cleanup();
      return (EXIT_FAILURE);
    }

    if (!validate_file_path(argv[optind])) {
      fprintf(stderr, "ERR - Invalid file path: %s\n", argv[optind]);
      finalizeProcessing();
      cleanup();
      return (EXIT_FAILURE);
    }
    if (processFileIntoTimeline(argv[optind++]) != EXIT_SUCCESS) {
      fprintf(stderr, "ERR - Failed to process file\n");
      finalizeProcessing();
      cleanup();
      return (EXIT_FAILURE);
    }
  }

  /* Finalize processing and generate video */
  if (finalizeProcessing() != EXIT_SUCCESS) {
    fprintf(stderr, "ERR - Failed to finalize processing\n");
    cleanup();
    return (EXIT_FAILURE);
  }

  /*
   * finished with the work
   */

  cleanup();

  return (EXIT_SUCCESS);
}

/****
 *
 * Display program information and license
 *
 * DESCRIPTION:
 *   Prints program name, version, author, and GPL license notice.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 ****/

#ifdef DEBUG
void show_info(void)
{
  fprintf(stderr, "%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
  fprintf(stderr, "By: Ron Dilley\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY.\n", PROGNAME);
  fprintf(stderr, "This is free software, and you are welcome\n");
  fprintf(stderr, "to redistribute it under certain conditions;\n");
  fprintf(stderr, "See the GNU General Public License for details.\n");
  fprintf(stderr, "\n");
}
#endif

/****
 *
 * Display version information
 *
 * DESCRIPTION:
 *   Prints program name, version, and build date.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 ****/

PRIVATE void print_version(void)
{
  printf("%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
}

/****
 *
 * Display usage information
 *
 * DESCRIPTION:
 *   Prints command-line syntax and option descriptions for all supported flags.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 ****/

PRIVATE void print_help(void)
{
  print_version();

  fprintf(stderr, "\n");
  fprintf(stderr, "syntax: %s [options] filename [filename ...]\n", PACKAGE);

#ifdef HAVE_GETOPT_LONG
  fprintf(stderr, " -c|--codec CODEC       video codec (default: libx264)\n");
  fprintf(stderr, "                        examples: libx264, libx265, libvpx-vp9\n");
  fprintf(stderr, " -C|--cidr-map FILE     CIDR mapping file (default: cidr_map.txt)\n");
  fprintf(stderr, " -d|--debug (0-9)       enable debugging info\n");
  fprintf(stderr, " -D|--duration SECS     target video duration in seconds (default: 300)\n");
  fprintf(stderr, "                        FPS and decay auto-scale based on data span\n");
  fprintf(stderr, " -f|--fps FPS           video framerate (default: auto-scaled)\n");
  fprintf(stderr, "                        baseline: 1 day = 3 FPS, scales linearly\n");
  fprintf(stderr, " -h|--help              this info\n");
  fprintf(stderr, " -o|--output DIR        output directory for frames/video (default: plots)\n");
  fprintf(stderr, " -p|--period DURATION   time bin period (default: 1m)\n");
  fprintf(stderr, "                        examples: 1m, 5m, 15m, 30m, 60m, 120s, 1h\n");
  fprintf(stderr, " -t|--timestamp         show timestamp overlay on frames\n");
  fprintf(stderr, " -v|--version           display version information\n");
  fprintf(stderr, " -V|--no-video          don't generate video (keep frames only)\n");
  fprintf(stderr, " filename               one or more files to process\n");
#else
  fprintf(stderr, " -c {codec}    video codec (default: libx264)\n");
  fprintf(stderr, " -C {file}     CIDR mapping file (default: cidr_map.txt)\n");
  fprintf(stderr, " -d {lvl}      enable debugging info\n");
  fprintf(stderr, " -D {secs}     target video duration (default: 300)\n");
  fprintf(stderr, " -f {fps}      video framerate (default: auto-scaled)\n");
  fprintf(stderr, " -h            this info\n");
  fprintf(stderr, " -o {dir}      output directory for frames/video (default: plots)\n");
  fprintf(stderr, " -p {period}   time bin period (default: 1m)\n");
  fprintf(stderr, " -t            show timestamp overlay on frames\n");
  fprintf(stderr, " -v            display version information\n");
  fprintf(stderr, " -V            don't generate video (keep frames only)\n");
  fprintf(stderr, " filename      one or more files to process\n");
#endif

  fprintf(stderr, "\n");
}

/****
 *
 * Free allocated resources before exit
 *
 * DESCRIPTION:
 *   Closes open files and frees allocated configuration memory.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Frees config structure and closes output file
 *
 ****/

PRIVATE void cleanup(void)
{
  if (config->outFile_st != NULL)
    fclose(config->outFile_st);
  XFREE(config->hostname);
#ifdef MEM_DEBUG
  XFREE_ALL();
#else
  XFREE(config);
#endif
}

/*****
 *
 * interrupt handler (current time)
 *
 * SECURITY: Signal handlers must only use async-signal-safe functions
 * Per POSIX, only sig_atomic_t variables should be modified
 *
 *****/

void ctime_prog(int signo)
{
  /* Simply set a flag for main loop to handle */
  /* DO NOT call fprintf() or other non-async-signal-safe functions here */

  (void)signo;  /* Unused but required by signal handler signature */

  /* Increment alarm count using only safe operations */
  static volatile sig_atomic_t alarm_counter = 0;

  alarm_counter++;

  if (alarm_counter >= 60)
  {
    reload = 1;
    alarm_counter = 0;
  }

  /* Reset alarm for next iteration */
  alarm(ALARM_TIMER);
}
