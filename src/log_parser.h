/*****
 *
 * Description: High-Speed Honeypot Log Parser Headers
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

#ifndef LOG_PARSER_DOT_H
#define LOG_PARSER_DOT_H

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
#include <zlib.h>
#include <time.h>
#include <sys/time.h>

/****
 *
 * defines
 *
 ****/

#define LOG_PARSER_MAX_LINE 4096
#define LOG_PARSER_BUFFER_SIZE (1024 * 1024)  // 1MB read buffer

/* Log format types */
#define LOG_TYPE_UNKNOWN 0
#define LOG_TYPE_HONEYPOT_SENSOR 1
#define LOG_TYPE_FORTIGATE 2

/* Protocol types */
#define PROTO_TCP 6
#define PROTO_UDP 17
#define PROTO_ICMP 1

/****
 *
 * typedefs & structs
 *
 ****/

/**
 * Honeypot sensor log event
 *
 * Format: Feb 22 09:26:39 10.10.10.40 honeypi00 sensor: PacketTime:2019-02-22 17:26:39.092449
 *         Len:60 IPv4/TCP 45.55.247.43:35398 -> 10.10.10.40:5900 ...
 */
typedef struct {
    /* Parsed timestamp */
    time_t timestamp;           // Unix timestamp
    uint32_t timestamp_us;      // Microseconds component

    /* Network information */
    uint32_t src_ip;            // Source IP (network byte order)
    uint32_t dst_ip;            // Destination IP (network byte order)
    uint16_t src_port;          // Source port
    uint16_t dst_port;          // Destination port
    uint8_t protocol;           // PROTO_TCP or PROTO_UDP

    /* TCP specific */
    uint8_t tcp_flags;          // TCP flags if protocol is TCP

    /* Raw fields for reference */
    char packet_time_str[32];   // Original PacketTime string
    char src_ip_str[16];        // Source IP string
    char dst_ip_str[16];        // Destination IP string

    /* Parser metadata */
    uint8_t log_type;           // LOG_TYPE_HONEYPOT_SENSOR
    int line_number;            // Line number in file (for debugging)

} HoneypotEvent_t;

/**
 * Parser statistics for performance monitoring
 */
typedef struct {
    uint64_t lines_processed;
    uint64_t lines_parsed_ok;
    uint64_t lines_parse_failed;
    uint64_t bytes_read;
    double parse_time_sec;
    double read_time_sec;
} ParserStats_t;

/**
 * Gzip file handle for streaming decompression
 */
typedef struct {
    gzFile gz_file;
    char *buffer;
    size_t buffer_size;
    size_t buffer_used;
    size_t buffer_pos;
    int eof_reached;
    char *file_path;
    ParserStats_t stats;
} GzipStream_t;

/****
 *
 * function prototypes
 *
 ****/

/* Parser initialization and cleanup */
int initLogParser(void);
void deInitLogParser(void);

/* Honeypot sensor log parsing */
int parseHoneypotLine(const char *line, HoneypotEvent_t *event);

/* Fast field extraction functions */
const char *findPacketTime(const char *line);
const char *findIPv4Protocol(const char *line);
int extractIPPort(const char *str, char *ip_buf, int ip_buf_size, uint16_t *port);
int parseTimestamp(const char *time_str, time_t *timestamp, uint32_t *microseconds);

/* IP address utilities */
uint32_t ipStringToInt(const char *ip_str);
void ipIntToString(uint32_t ip, char *buf, size_t buf_size);

/* Gzip streaming functions */
GzipStream_t *openGzipStream(const char *file_path);
void closeGzipStream(GzipStream_t *stream);
int readLineGzip(GzipStream_t *stream, char *line_buf, size_t buf_size);
void resetParserStats(ParserStats_t *stats);
void printParserStats(const ParserStats_t *stats);

/* Batch processing */
int processGzipFile(const char *file_path,
                    int (*event_callback)(const HoneypotEvent_t *event, void *user_data),
                    void *user_data);

#endif /* LOG_PARSER_DOT_H */
