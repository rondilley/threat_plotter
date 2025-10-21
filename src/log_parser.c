/*****
 *
 * Description: High-Speed Honeypot Log Parser Implementation
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

#include "log_parser.h"
#include "mem.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>

/****
 *
 * local variables
 *
 ****/

PRIVATE int parser_initialized = FALSE;

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
 * Initialize log parser
 *
 * DESCRIPTION:
 *   Sets parser initialization flag. Idempotent (safe to call multiple times).
 *
 * RETURNS:
 *   TRUE
 *
 ****/
int initLogParser(void)
{
    if (parser_initialized) {
        return TRUE;
    }

    parser_initialized = TRUE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Log parser initialized\n");
    }
#endif

    return TRUE;
}

/****
 *
 * Deinitialize log parser
 *
 * DESCRIPTION:
 *   Clears parser initialization flag.
 *
 ****/
void deInitLogParser(void)
{
    parser_initialized = FALSE;

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Log parser deinitialized\n");
    }
#endif
}

/****
 *
 * Convert IP string to 32-bit integer
 *
 * DESCRIPTION:
 *   Parses dotted-decimal IP string to network byte order uint32_t.
 *
 * PARAMETERS:
 *   ip_str - IP address string (e.g., "192.168.1.1")
 *
 * RETURNS:
 *   IP as uint32_t (network byte order), or 0 if invalid
 *
 ****/
uint32_t ipStringToInt(const char *ip_str)
{
    struct in_addr addr;

    if (inet_aton(ip_str, &addr) == 0) {
        return 0;  // Invalid IP
    }

    return addr.s_addr;  // Already in network byte order
}

/****
 *
 * Convert 32-bit IP integer to string
 *
 * DESCRIPTION:
 *   Converts network byte order IP to dotted-decimal string.
 *
 * PARAMETERS:
 *   ip - IP as uint32_t (network byte order)
 *   buf - Output buffer
 *   buf_size - Buffer size
 *
 ****/
void ipIntToString(uint32_t ip, char *buf, size_t buf_size)
{
    struct in_addr addr;
    addr.s_addr = ip;

    const char *ip_str = inet_ntoa(addr);
    if (ip_str && buf_size > 0) {
        strncpy(buf, ip_str, buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

/****
 *
 * Find PacketTime field in log line
 *
 * DESCRIPTION:
 *   Searches for "PacketTime:" substring.
 *
 * PARAMETERS:
 *   line - Log line to search
 *
 * RETURNS:
 *   Pointer to "PacketTime:" or NULL if not found
 *
 ****/
const char *findPacketTime(const char *line)
{
    return strstr(line, "PacketTime:");
}

/****
 *
 * Find IPv4 protocol field in log line
 *
 * DESCRIPTION:
 *   Searches for "IPv4/" substring (TCP or UDP follows).
 *
 * PARAMETERS:
 *   line - Log line to search
 *
 * RETURNS:
 *   Pointer to "IPv4/" or NULL if not found
 *
 ****/
const char *findIPv4Protocol(const char *line)
{
    const char *p = strstr(line, "IPv4/");
    if (!p) {
        return NULL;
    }

    return p;
}

/****
 *
 * Parse timestamp from PacketTime field
 *
 * DESCRIPTION:
 *   Parses "PacketTime:YYYY-MM-DD HH:MM:SS.microseconds" format.
 *
 * PARAMETERS:
 *   time_str - Timestamp string (with or without "PacketTime:" prefix)
 *   timestamp - Output Unix timestamp
 *   microseconds - Output microseconds
 *
 * RETURNS:
 *   TRUE on success, FALSE on parse failure
 *
 ****/
int parseTimestamp(const char *time_str, time_t *timestamp, uint32_t *microseconds)
{
    struct tm tm_info;
    int year, month, day, hour, min, sec, usec;

    if (!time_str || !timestamp || !microseconds) {
        return FALSE;
    }

    /* Skip "PacketTime:" prefix if present */
    if (strncmp(time_str, "PacketTime:", 11) == 0) {
        time_str += 11;
    }

    /* Parse: YYYY-MM-DD HH:MM:SS.microseconds */
    int parsed = sscanf(time_str, "%d-%d-%d %d:%d:%d.%d",
                        &year, &month, &day, &hour, &min, &sec, &usec);

    if (parsed < 6) {
        return FALSE;  // Failed to parse minimum required fields
    }

    /* Fill tm structure */
    memset(&tm_info, 0, sizeof(struct tm));
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min = min;
    tm_info.tm_sec = sec;
    tm_info.tm_isdst = -1;  // Let system determine DST

    /* Convert to Unix timestamp */
    *timestamp = mktime(&tm_info);

    if (*timestamp == (time_t)-1) {
        return FALSE;
    }

    /* Store microseconds (if parsed) */
    *microseconds = (parsed == 7) ? (uint32_t)usec : 0;

    return TRUE;
}

/****
 *
 * Extract IP:port from string
 *
 * DESCRIPTION:
 *   Parses "IP:port" format (e.g., "192.168.1.1:8080").
 *
 * PARAMETERS:
 *   str - String containing IP:port
 *   ip_buf - Output buffer for IP string
 *   ip_buf_size - Buffer size
 *   port - Output port number
 *
 * RETURNS:
 *   TRUE on success, FALSE on parse failure
 *
 ****/
int extractIPPort(const char *str, char *ip_buf, int ip_buf_size, uint16_t *port)
{
    const char *p = str;
    int ip_len = 0;

    if (!str || !ip_buf || !port) {
        return FALSE;
    }

    /* Skip whitespace */
    while (*p && isspace(*p)) {
        p++;
    }

    /* Extract IP address (until ':') */
    while (*p && *p != ':' && ip_len + 1 < ip_buf_size) {
        if (isdigit(*p) || *p == '.') {
            ip_buf[ip_len++] = *p;
        } else if (*p != ' ') {
            /* Invalid character in IP */
            return FALSE;
        }
        p++;
    }

    ip_buf[ip_len] = '\0';

    if (*p != ':') {
        return FALSE;  // No port separator found
    }

    /* Skip ':' and extract port */
    p++;

    /* Use strtol for safer integer parsing */
    char *endptr;
    long port_val = strtol(p, &endptr, 10);

    /* Validate port number */
    if (port_val < 0 || port_val > 65535 || endptr == p) {
        return FALSE;
    }

    *port = (uint16_t)port_val;

    return (ip_len > 0);
}

/****
 *
 * Parse honeypot sensor log line
 *
 * DESCRIPTION:
 *   Parses syslog honeypot format. Extracts timestamp, IPs, ports, protocol.
 *
 * PARAMETERS:
 *   line - Log line to parse
 *   event - Output HoneypotEvent_t structure
 *
 * RETURNS:
 *   TRUE on success, FALSE if line doesn't match format
 *
 ****/
int parseHoneypotLine(const char *line, HoneypotEvent_t *event)
{
    const char *p;
    char ip_buf[16];
    uint16_t port;

    if (!line || !event) {
        return FALSE;
    }

    /* Initialize event structure */
    memset(event, 0, sizeof(HoneypotEvent_t));
    event->log_type = LOG_TYPE_HONEYPOT_SENSOR;

    /* Find and parse PacketTime */
    p = findPacketTime(line);
    if (!p) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - PacketTime not found in line\n");
        }
#endif
        return FALSE;
    }

    /* Parse timestamp */
    if (!parseTimestamp(p, &event->timestamp, &event->timestamp_us)) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - Failed to parse timestamp\n");
        }
#endif
        return FALSE;
    }

    /* Find protocol field (IPv4/TCP or IPv4/UDP) */
    p = findIPv4Protocol(line);
    if (!p) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - IPv4 protocol not found\n");
        }
#endif
        return FALSE;
    }

    /* Determine protocol */
    if (strncmp(p, "IPv4/TCP", 8) == 0) {
        event->protocol = PROTO_TCP;
        p += 8;  // Skip "IPv4/TCP"
    } else if (strncmp(p, "IPv4/UDP", 8) == 0) {
        event->protocol = PROTO_UDP;
        p += 8;  // Skip "IPv4/UDP"
    } else {
        return FALSE;  // Unknown protocol
    }

    /* Skip whitespace */
    while (*p && isspace(*p)) {
        p++;
    }

    /* Extract source IP:port */
    if (!extractIPPort(p, ip_buf, sizeof(ip_buf), &port)) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - Failed to extract source IP:port\n");
        }
#endif
        return FALSE;
    }

    /* Convert source IP to integer */
    event->src_ip = ipStringToInt(ip_buf);
    if (event->src_ip == 0) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - Invalid source IP: %s\n", ip_buf);
        }
#endif
        return FALSE;
    }

    event->src_port = port;
    strncpy(event->src_ip_str, ip_buf, sizeof(event->src_ip_str) - 1);
    event->src_ip_str[sizeof(event->src_ip_str) - 1] = '\0';

    /* Find " -> " separator */
    p = strstr(p, " -> ");
    if (!p) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - No ' -> ' separator found\n");
        }
#endif
        return FALSE;
    }

    p += 4;  // Skip " -> "

    /* Extract destination IP:port */
    if (!extractIPPort(p, ip_buf, sizeof(ip_buf), &port)) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - Failed to extract destination IP:port\n");
        }
#endif
        return FALSE;
    }

    /* Convert destination IP to integer */
    event->dst_ip = ipStringToInt(ip_buf);
    if (event->dst_ip == 0) {
#ifdef DEBUG
        if (config->debug >= 3) {
            fprintf(stderr, "DEBUG - Invalid destination IP: %s\n", ip_buf);
        }
#endif
        return FALSE;
    }

    event->dst_port = port;
    strncpy(event->dst_ip_str, ip_buf, sizeof(event->dst_ip_str) - 1);
    event->dst_ip_str[sizeof(event->dst_ip_str) - 1] = '\0';

#ifdef DEBUG
    if (config->debug >= 5) {
        fprintf(stderr, "DEBUG - Parsed: %s:%u -> %s:%u proto=%u time=%ld.%06u\n",
                event->src_ip_str, event->src_port,
                event->dst_ip_str, event->dst_port,
                event->protocol,
                (long)event->timestamp, event->timestamp_us);
    }
#endif

    return TRUE;
}

/****
 *
 * Open gzip compressed file for streaming
 *
 * DESCRIPTION:
 *   Opens .gz file for line-by-line reading. Allocates GzipStream_t structure.
 *
 * PARAMETERS:
 *   file_path - Path to .gz file
 *
 * RETURNS:
 *   Pointer to GzipStream_t on success, NULL on failure
 *
 ****/
GzipStream_t *openGzipStream(const char *file_path)
{
    GzipStream_t *stream;

    if (!file_path) {
        return NULL;
    }

    stream = (GzipStream_t *)XMALLOC(sizeof(GzipStream_t));
    if (!stream) {
        return NULL;
    }

    memset(stream, 0, sizeof(GzipStream_t));

    /* Open gzip file */
    stream->gz_file = gzopen(file_path, "rb");
    if (!stream->gz_file) {
        fprintf(stderr, "ERR - Failed to open gzip file: %s\n", file_path);
        XFREE(stream);
        return NULL;
    }

    /* Allocate read buffer */
    stream->buffer_size = LOG_PARSER_BUFFER_SIZE;
    stream->buffer = (char *)XMALLOC((int)stream->buffer_size);
    if (!stream->buffer) {
        gzclose(stream->gz_file);
        XFREE(stream);
        return NULL;
    }

    /* Store file path */
    stream->file_path = strdup(file_path);

    /* Set compression level for better speed */
    gzbuffer(stream->gz_file, 128 * 1024);  // 128KB internal buffer

    resetParserStats(&stream->stats);

#ifdef DEBUG
    if (config->debug >= 1) {
        fprintf(stderr, "DEBUG - Opened gzip stream: %s\n", file_path);
    }
#endif

    return stream;
}

/****
 *
 * Close gzip stream and free resources
 *
 * DESCRIPTION:
 *   Closes gzip file, frees buffers and stream structure.
 *
 * PARAMETERS:
 *   stream - GzipStream_t to close (NULL-safe)
 *
 ****/
void closeGzipStream(GzipStream_t *stream)
{
    if (!stream) {
        return;
    }

    if (stream->gz_file) {
        gzclose(stream->gz_file);
    }

    if (stream->buffer) {
        XFREE(stream->buffer);
    }

    if (stream->file_path) {
        free(stream->file_path);
    }

    XFREE(stream);
}

/****
 *
 * Read one line from gzip stream
 *
 * DESCRIPTION:
 *   Reads next line from compressed file. Updates line count and byte statistics.
 *
 * PARAMETERS:
 *   stream - GzipStream_t handle
 *   line_buf - Output buffer for line
 *   buf_size - Buffer size
 *
 * RETURNS:
 *   TRUE if line read, FALSE on EOF or error
 *
 ****/
int readLineGzip(GzipStream_t *stream, char *line_buf, size_t buf_size)
{
    if (!stream || !line_buf || buf_size == 0) {
        return FALSE;
    }

    if (stream->eof_reached) {
        return FALSE;
    }

    /* Use gzgets for line-by-line reading */
    if (gzgets(stream->gz_file, line_buf, (int)buf_size) == NULL) {
        stream->eof_reached = TRUE;
        return FALSE;
    }

    /* Update statistics */
    stream->stats.lines_processed++;
    stream->stats.bytes_read += strlen(line_buf);

    return TRUE;
}

/****
 *
 * Reset parser statistics
 *
 * DESCRIPTION:
 *   Zeros ParserStats_t structure.
 *
 * PARAMETERS:
 *   stats - ParserStats_t to reset (NULL-safe)
 *
 ****/
void resetParserStats(ParserStats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(ParserStats_t));
}

/****
 *
 * Print parser statistics to stderr
 *
 * DESCRIPTION:
 *   Displays lines processed, parse rate, throughput, success rate.
 *
 * PARAMETERS:
 *   stats - ParserStats_t to print (NULL-safe)
 *
 ****/
void printParserStats(const ParserStats_t *stats)
{
    if (!stats) {
        return;
    }

    fprintf(stderr, "\n=== Parser Statistics ===\n");
    fprintf(stderr, "Lines processed:     %lu\n", stats->lines_processed);
    fprintf(stderr, "Lines parsed OK:     %lu\n", stats->lines_parsed_ok);
    fprintf(stderr, "Lines parse failed:  %lu\n", stats->lines_parse_failed);
    fprintf(stderr, "Bytes read:          %lu (%.2f MB)\n",
            stats->bytes_read, (double)stats->bytes_read / (1024.0 * 1024.0));

    if (stats->parse_time_sec > 0) {
        fprintf(stderr, "Parse time:          %.2f seconds\n", stats->parse_time_sec);
        fprintf(stderr, "Lines/second:        %.0f\n",
                (double)stats->lines_processed / stats->parse_time_sec);
        fprintf(stderr, "MB/second:           %.2f\n",
                ((double)stats->bytes_read / (1024.0 * 1024.0)) / stats->parse_time_sec);
    }

    if (stats->lines_processed > 0) {
        fprintf(stderr, "Success rate:        %.2f%%\n",
                (100.0 * (double)stats->lines_parsed_ok) / (double)stats->lines_processed);
    }

    fprintf(stderr, "=========================\n\n");
}

/****
 *
 * Process entire gzip log file with event callback
 *
 * DESCRIPTION:
 *   Main processing loop. Reads/parses lines, calls callback for each event.
 *   Tracks timing and statistics. Prints progress every 1M lines.
 *
 * PARAMETERS:
 *   file_path - Path to .gz log file
 *   event_callback - Function called for each parsed event (return FALSE to stop)
 *   user_data - Opaque pointer passed to callback
 *
 * RETURNS:
 *   TRUE on success, FALSE on error or callback abort
 *
 ****/
int processGzipFile(const char *file_path,
                    int (*event_callback)(const HoneypotEvent_t *event, void *user_data),
                    void *user_data)
{
    GzipStream_t *stream;
    char line_buf[LOG_PARSER_MAX_LINE];
    HoneypotEvent_t event;
    struct timeval start_time, end_time;
    int result = TRUE;

    if (!file_path || !event_callback) {
        return FALSE;
    }

    /* Open gzip stream */
    stream = openGzipStream(file_path);
    if (!stream) {
        return FALSE;
    }

    /* Start timing */
    gettimeofday(&start_time, NULL);

    /* Read and parse each line */
    while (readLineGzip(stream, line_buf, sizeof(line_buf))) {
        /* Parse honeypot sensor log line */
        if (parseHoneypotLine(line_buf, &event)) {
            stream->stats.lines_parsed_ok++;

            /* Call user callback with parsed event */
            if (!event_callback(&event, user_data)) {
                /* Callback returned FALSE - stop processing */
                result = FALSE;
                break;
            }
        } else {
            stream->stats.lines_parse_failed++;
        }

        /* Progress indicator every 1M lines */
        if (stream->stats.lines_processed % 1000000 == 0) {
            fprintf(stderr, "  Processed %luM lines...\n",
                    stream->stats.lines_processed / 1000000);
        }
    }

    /* End timing */
    gettimeofday(&end_time, NULL);

    /* Calculate elapsed time */
    stream->stats.parse_time_sec =
        (double)(end_time.tv_sec - start_time.tv_sec) +
        (double)(end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    /* Print statistics */
    printParserStats(&stream->stats);

    /* Cleanup */
    closeGzipStream(stream);

    return result;
}

/****
 *
 * Extract timestamp from FortiGate log line (basic parsing for sorting only)
 *
 * DESCRIPTION:
 *   Parses FortiGate format: date=YYYY-MM-DD time=HH:MM:SS
 *   This is a simplified parser just for timestamp extraction.
 *
 * PARAMETERS:
 *   line - Log line to parse
 *
 * RETURNS:
 *   Unix timestamp, or 0 if parsing fails
 *
 ****/
PRIVATE time_t parseFortiGateTimestamp(const char *line)
{
    const char *date_ptr, *time_ptr;
    struct tm tm_info;
    int year, month, day, hour, minute, second;

    if (!line) {
        return 0;
    }

    /* Look for date=YYYY-MM-DD pattern */
    date_ptr = strstr(line, "date=");
    if (!date_ptr) {
        return 0;
    }
    date_ptr += 5;  /* Skip "date=" */

    /* Parse date: YYYY-MM-DD */
    if (sscanf(date_ptr, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        return 0;
    }

    /* Look for time=HH:MM:SS pattern */
    time_ptr = strstr(line, "time=");
    if (!time_ptr) {
        return 0;
    }
    time_ptr += 5;  /* Skip "time=" */

    /* Parse time: HH:MM:SS */
    if (sscanf(time_ptr, "%2d:%2d:%2d", &hour, &minute, &second) != 3) {
        return 0;
    }

    /* Build struct tm */
    memset(&tm_info, 0, sizeof(tm_info));
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min = minute;
    tm_info.tm_sec = second;
    tm_info.tm_isdst = -1;  /* Let mktime determine DST */

    return mktime(&tm_info);
}

/****
 *
 * Peek at first parseable timestamp in log file
 *
 * DESCRIPTION:
 *   Opens log file, reads lines until first parseable event is found,
 *   extracts timestamp, and closes file. Used for chronological file sorting.
 *   Supports both honeypot sensor format and FortiGate format.
 *
 * PARAMETERS:
 *   file_path - Path to gzip or plain text log file
 *
 * RETURNS:
 *   Unix timestamp of first parseable event, or 0 if no events found
 *
 * SIDE EFFECTS:
 *   Opens and closes file
 *
 ****/
time_t peekFirstTimestamp(const char *file_path)
{
    GzipStream_t *stream = NULL;
    char line[LOG_PARSER_MAX_LINE];
    HoneypotEvent_t event;
    time_t first_timestamp = 0;
    int max_lines_to_check = 1000;  /* Don't scan forever if file is corrupt */
    int lines_checked = 0;

    if (!file_path) {
        return 0;
    }

    /* Open gzip stream */
    stream = openGzipStream(file_path);
    if (!stream) {
        fprintf(stderr, "WARN - Cannot peek timestamp from %s: failed to open\n", file_path);
        return 0;
    }

    /* Read lines until we find a parseable event */
    while (readLineGzip(stream, line, sizeof(line)) && lines_checked < max_lines_to_check) {
        lines_checked++;

        /* Try to parse as honeypot sensor log */
        if (parseHoneypotLine(line, &event)) {
            first_timestamp = event.timestamp;
            break;
        }

        /* Try to parse as FortiGate log (basic timestamp extraction) */
        first_timestamp = parseFortiGateTimestamp(line);
        if (first_timestamp > 0) {
            break;
        }
    }

    /* Cleanup */
    closeGzipStream(stream);

    /* Only warn if no timestamp found (this is rare and indicates an issue) */
    if (first_timestamp == 0) {
        fprintf(stderr, "WARN - No parseable timestamp found in %s (checked %d lines)\n",
                file_path, lines_checked);
    }

    return first_timestamp;
}
