/*****
 *
 * Description: Utility Functions
 * 
 * Copyright (c) 2008-2023, Ron Dilley
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
 * defines
 *
 ****/

/* turn on priority names */
#define SYSLOG_NAMES

/****
 *
 * includes
 *
 ****/

#include "util.h"

/****
 *
 * local variables
 *
 ****/

PRIVATE const char *restricted_environ[] = {
    "IFS= \t\n",
    "PATH= /bin:/usr/bin",
    0};
PRIVATE const char *preserve_environ[] = {
    "TZ",
    0};

#ifdef SOLARIS

#define INTERNAL_NOPRI 0x10 /* the "no priority" priority */
                            /* mark "facility" */
#define INTERNAL_MARK LOG_MAKEPRI(LOG_NFACILITIES, 0)
typedef struct _code
{
  char *c_name;
  int c_val;
} CODE;

CODE prioritynames[] =
    {
        {"alert", LOG_ALERT},
        {"crit", LOG_CRIT},
        {"debug", LOG_DEBUG},
        {"emerg", LOG_EMERG},
        {"err", LOG_ERR},
        {"error", LOG_ERR}, /* DEPRECATED */
        {"info", LOG_INFO},
        {"none", INTERNAL_NOPRI}, /* INTERNAL */
        {"notice", LOG_NOTICE},
        {"panic", LOG_EMERG},  /* DEPRECATED */
        {"warn", LOG_WARNING}, /* DEPRECATED */
        {"warning", LOG_WARNING},
        {NULL, -1}};
#else
/* prioritynames is provided by syslog.h on Linux */
#endif

/****
 *
 * external global variables
 *
 ****/

extern Config_t *config;
/* environ is provided by unistd.h */

/****
 *
 * forward declarations for static functions
 *
 ****/

static int safe_open(const char *filename);
static void cleanup_pid_file(const char *filename);

/****
 *
 * functions
 *
 ****/

/****
 *
 * Display formatted output to syslog or stderr
 *
 * DESCRIPTION:
 *   Formats message and sends to syslog (daemon mode) or stderr/stdout (interactive mode).
 *
 * PARAMETERS:
 *   level - Syslog priority level (LOG_ERR, LOG_INFO, etc.)
 *   format - printf-style format string
 *   ... - Variable arguments
 *
 * RETURNS:
 *   TRUE on success, FAILED on error
 *
 ****/

int display(int level, const char *format, ...)
{
  PRIVATE va_list args;
  PRIVATE char tmp_buf[SYSLOG_MAX];
  PRIVATE int i;

  va_start(args, format);
  vsnprintf(tmp_buf, sizeof(tmp_buf), format, args);
  tmp_buf[sizeof(tmp_buf) - 1] = '\0';  /* Ensure null termination */
  if (strlen(tmp_buf) > 0 && tmp_buf[strlen(tmp_buf) - 1] == '\n')
  {
    tmp_buf[strlen(tmp_buf) - 1] = '\0';
  }
  va_end(args);

  if (config->mode != MODE_INTERACTIVE)
  {
    /* display info via syslog */
    syslog(level, "%s", tmp_buf);
  }
  else
  {
    if (level <= LOG_ERR)
    {
      /* display info via stderr */
      for (i = 0; prioritynames[i].c_name != NULL; i++)
      {
        if (prioritynames[i].c_val == level)
        {
          fprintf(stderr, "%s[%u] - %s\n", prioritynames[i].c_name, config->cur_pid, tmp_buf);
          return TRUE;
        }
      }
    }
    else
    {
      /* display info via stdout */
      for (i = 0; prioritynames[i].c_name != NULL; i++)
      {
        if (prioritynames[i].c_val == level)
        {
          printf("%s[%u] - %s\n", prioritynames[i].c_name, config->cur_pid, tmp_buf);
          return TRUE;
        }
      }
    }
  }

  return FAILED;
}

/****
 *
 * Redirect file descriptor to /dev/null
 *
 * DESCRIPTION:
 *   Redirects stdin/stdout/stderr to null device.
 *
 * PARAMETERS:
 *   fd - File descriptor (0=stdin, 1=stdout, 2=stderr)
 *
 * RETURNS:
 *   TRUE on success, FALSE on failure
 *
 ****/

PUBLIC int open_devnull(int fd)
{
  FILE *f_st = 0;

  if (fd EQ 0)
    f_st = freopen(DEV_NULL, "rb", stdin);
  else if (fd EQ 1)
    f_st = freopen(DEV_NULL, "wb", stdout);
  else if (fd EQ 2)
    f_st = freopen(DEV_NULL, "wb", stderr);
  return (f_st && fileno(f_st) EQ fd);
}

/****
 *
 * Check directory permissions for security
 *
 * DESCRIPTION:
 *   Validates directory ownership and permissions to prevent privilege escalation.
 *   Checks directory tree up to root.
 *
 * PARAMETERS:
 *   dir - Directory path to check
 *
 * RETURNS:
 *   1 if safe, 0 if unsafe, FAILED on error
 *
 ****/

int is_dir_safe(const char *dir)
{
  DIR *fd, *start;
  int rc = FAILED;
  char new_dir[PATH_MAX + 1];
  uid_t uid;
  struct stat f, l;

  if (!(start = opendir(".")))
    return FAILED;
  if (lstat(dir, &l) == FAILED)
  {
    closedir(start);
    return FAILED;
  }
  uid = geteuid();

  do
  {
    if (chdir(dir) EQ FAILED)
      break;
    if (!(fd = opendir(".")))
      break;

#ifdef LINUX
    if (fstat(dirfd(fd), &f) EQ FAILED)
    {
#elif MACOS
    if (fstat(fd->__dd_fd, &f) EQ FAILED)
    {
#elif CYGWIN
    if (fstat(fd->__d_fd, &f) EQ FAILED)
    {
#elif OPENBSD
    if (fstat(dirfd(fd), &f) EQ FAILED)
    {
#elif FREEBSD
    if (fstat(dirfd(fd), &f) EQ FAILED)
    {        
#else
    if (fstat(fd->dd_fd, &f) EQ FAILED)
    {
#endif

      closedir(fd);
      break;
    }
    closedir(fd);

    if (l.st_mode != f.st_mode || l.st_ino != f.st_ino || l.st_dev != f.st_dev)
      break;
    if ((f.st_mode & (S_IWOTH | S_IWGRP)) || (f.st_uid && f.st_uid != uid))
    {
      rc = 0;
      break;
    }
    dir = "..";
    if (lstat(dir, &l) EQ FAILED)
      break;
    if (!getcwd(new_dir, PATH_MAX + 1))
      break;
  } while (new_dir[1]); /* new_dir[0] will always be a slash */
  if (!new_dir[1])
    rc = 1;

#ifdef LINUX
  if (fchdir(dirfd(start)) EQ FAILED)
  {
#elif MACOS
  if (fchdir(start->__dd_fd) EQ FAILED)
  {
#elif CYGWIN
  if (fchdir(start->__d_fd) EQ FAILED)
  {
#elif OPENBSD
  if (fchdir(dirfd(start)) EQ FAILED)
  {
#elif FREEBSD
  if (fstat(dirfd(fd), &f) EQ FAILED)
  {     
#else
  if (fchdir(start->dd_fd) EQ FAILED)
  {
#endif
    closedir(start);
    return (FAILED);
  }

  closedir(start);
  return rc;
}

/****
 *
 * Create PID file with current process ID
 *
 * DESCRIPTION:
 *   Safely creates PID file, removing any existing file first.
 *
 * PARAMETERS:
 *   filename - Path to PID file
 *
 * RETURNS:
 *   TRUE on success, FAILED on error
 *
 ****/

int create_pid_file(const char *filename)
{
  int fd;
  FILE *lockfile;
  pid_t pid;

  /* remove old pid file if it exists */
  cleanup_pid_file(filename);
  if ((fd = safe_open(filename)) < 0)
  {
    display(LOG_ERR, "Unable to open pid file [%s]", filename);
    return FAILED;
  }
  if ((lockfile = fdopen(fd, "w")) EQ NULL)
  {
    display(LOG_ERR, "Unable to fdopen() pid file [%d]", fd);
    close(fd);
    return FAILED;
  }
  pid = getpid();
  if (fprintf(lockfile, "%ld\n", (long)pid) < 0)
  {
    display(LOG_ERR, "Unable to write pid to file [%s]", filename);
    fclose(lockfile);
    return FAILED;
  }
  if (fflush(lockfile) EQ EOF)
  {
    display(LOG_ERR, "fflush() failed [%s]", filename);
    fclose(lockfile);
    return FAILED;
  }

  fclose(lockfile);
  return TRUE;
}

/****
 *
 * Safely open file for writing
 *
 * DESCRIPTION:
 *   Opens file with exclusive creation, preventing race conditions.
 *
 * PARAMETERS:
 *   filename - File path
 *
 * RETURNS:
 *   File descriptor on success, FAILED on error
 *
 ****/

static int safe_open(const char *filename)
{
  int fd;
  struct stat sb;
  XMEMSET(&sb, 0, sizeof(struct stat));

  if (lstat(filename, &sb) EQ FAILED)
  {
    if (errno != ENOENT)
      return (FAILED);
  }
  else if ((sb.st_mode & S_IFREG) EQ 0)
  {
    errno = EOPNOTSUPP;
    return (FAILED);
  }

  unlink(filename);
  fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  return (fd);
}

/****
 *
 * Remove PID file
 *
 * DESCRIPTION:
 *   Unlinks PID file if filename is non-empty.
 *
 * PARAMETERS:
 *   filename - PID file path
 *
 ****/

static void cleanup_pid_file(const char *filename)
{
  if (strlen(filename) > 0)
  {
    unlink(filename);
  }
}

/****
 *
 * Sanitize environment variables
 *
 * DESCRIPTION:
 *   Creates clean environment with only safe variables. Prevents privilege escalation.
 *
 ****/

void sanitize_environment(void)
{
  int i;
  char **new_environ;
  char *ptr, *value;
  const char *var;
  size_t arr_size = 1;
  size_t arr_ptr = 0;
  size_t len;
  size_t new_size = 0;

  for (i = 0; (var = restricted_environ[i]) != 0; i++)
  {
    new_size += strlen(var) + 1;
    arr_size++;
  }

  for (i = 0; (var = preserve_environ[i]) != 0; i++)
  {
    if (!(value = getenv(var)))
      continue;
    new_size += strlen(var) + strlen(value) + 2;
    arr_size++;
  }

  new_size += (arr_size * sizeof(char *));
  new_environ = (char **)XMALLOC((int)new_size);
  new_environ[arr_size - 1] = 0;
  ptr = (char *)new_environ + (arr_size * sizeof(char *));
  for (i = 0; (var = restricted_environ[i]) != 0; i++)
  {
    new_environ[arr_ptr++] = ptr;
    len = strlen(var);
    XMEMCPY(ptr, var, (int)(len + 1));
    ptr += len + 1;
  }

  for (i = 0; (var = preserve_environ[i]) != 0; i++)
  {
    if (!(value = getenv(var)))
      continue;
    new_environ[arr_ptr++] = ptr;
    len = strlen(var);
    XMEMCPY(ptr, var, (int)len);
    *(ptr + len + 1) = '=';
    XMEMCPY(ptr + len + 2, value, (int)(strlen(value) + 1));
    ptr += len + strlen(value) + 2;
  }

  environ = new_environ;
}

/****
 *
 * Secure file open with symlink protection
 *
 * DESCRIPTION:
 *   Opens file using O_NOFOLLOW flag to prevent symlink attacks.
 *
 * PARAMETERS:
 *   path - File path
 *   mode - fopen mode string ("r", "w", "a", etc.)
 *
 * RETURNS:
 *   FILE pointer on success, NULL on error or symlink detected
 *
 ****/
PUBLIC FILE *secure_fopen(const char *path, const char *mode)
{
  int flags = 0;
  int fd;
  FILE *fp;

  if (!path || !mode) {
    return NULL;
  }

  /* Determine flags based on mode - check '+' variants first */
  if (strchr(mode, '+')) {
    if (strchr(mode, 'r')) {
      flags = O_RDWR | O_NOFOLLOW;
    } else if (strchr(mode, 'w')) {
      flags = O_RDWR | O_CREAT | O_TRUNC | O_NOFOLLOW;
    } else if (strchr(mode, 'a')) {
      flags = O_RDWR | O_CREAT | O_APPEND | O_NOFOLLOW;
    } else {
      fprintf(stderr, "ERR - Invalid file mode: %s\n", mode);
      return NULL;
    }
  } else if (strchr(mode, 'r')) {
    flags = O_RDONLY | O_NOFOLLOW;
  } else if (strchr(mode, 'w')) {
    flags = O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW;
  } else if (strchr(mode, 'a')) {
    flags = O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW;
  } else {
    fprintf(stderr, "ERR - Invalid file mode: %s\n", mode);
    return NULL;
  }

  /* Open file with O_NOFOLLOW to prevent symlink attacks */
  fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    if (errno == ELOOP) {
      fprintf(stderr, "ERR - Symbolic link detected, access denied: %s\n", path);
    }
    return NULL;
  }

  /* Convert file descriptor to FILE* */
  fp = fdopen(fd, mode);
  if (fp == NULL) {
    close(fd);
    return NULL;
  }

  return fp;
}
