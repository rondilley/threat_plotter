# Threat Plot (tplot)

by Ron Dilley <ron.dilley@uberadmin.com>

You can find the latest information on tplot [here](http://www.uberadmin.com/Projects/tplot/ "Threat Plot")

## What is Threat Plot (tplot)?

Threat Plot is a visualization tool that processes log files from honeypots and other security sources and plots the data on Hilbert curves. Source IP addresses are positioned deterministically such that CIDR blocks naturally cluster together, revealing attack campaigns and patterns. Optional timezone-aware CIDR mapping enhances geographic context.

## Why use it?

This tool helps security analysts visualize attack patterns and threat data from honeypots and other security log sources. By plotting data on Hilbert curves with automatic CIDR clustering, you can:

- **Identify coordinated campaigns**: CIDR blocks naturally cluster together, revealing related attack sources
- **Track persistent threats**: Static IP positioning shows recurring sources across time
- **Detect anomalies**: Visual patterns highlight unusual activity
- **Visualize private IP space**: Gray overlay marks non-routable addresses (RFC1918, loopback, multicast)
- **See temporal patterns**: Optional timezone mapping shows geographic attack flow
- **Analyze large datasets**: Process millions of events in under a minute

## Features

### Core Engine
- **High-speed log processing**: 125K+ lines/second with gzip decompression
- **Honeypot sensor log parsing**: Standard syslog format with `sensor:` identifier
- **IP address extraction**: Source and destination IPs from network traffic
- **Microsecond precision timestamps**: Accurate temporal analysis
- **Protocol detection**: TCP/UDP protocol identification
- **Gzip streaming support**: Process compressed logs without temp files

### Visualization
- **Hilbert curve mapping**: Space-filling curve preserves CIDR locality
- **Automatic CIDR clustering**: Adjacent IPs appear at nearby coordinates
- **Non-routable IP overlay**: Gray mask shows RFC1918 private, loopback, multicast, reserved ranges
- **Linear color scaling**: Maximum visibility mode - even single events are clearly visible
- **Time binning**: Configurable time windows (1m, 5m, 30m, 1h)
- **Decay visualization**: IPs fade over time (auto-scaled based on data span, default 3 hours for 1 day)
- **Video generation**: Animated MP4 output using ffmpeg
- **High resolution**: 3440×1440 (UWQHD) default output

### Geographic Intelligence (Optional)
- **GeoIP integration**: Optional MaxMind GeoLite2 database support
- **CIDR timezone mapping**: Optional pre-computed IPv4 space allocation by timezone
- **Timezone detection**: UTC-12 to UTC+14 coverage
- **Proportional allocation**: US timezones get more X-axis space due to higher CIDR density
- **Works without**: CIDR clustering functions with direct IP mapping (no external files required)

## Current Status

**Phase**: Production Ready - Full implementation complete and tested

**Implemented**:
- [DONE] High-performance log parser (125K+ lines/sec)
- [DONE] Gzip streaming decompression
- [DONE] Honeypot sensor log format support
- [DONE] IP/port/timestamp/protocol extraction
- [DONE] Hilbert curve coordinate mapping with CIDR awareness
- [DONE] Time binning and aggregation with decay cache
- [DONE] PPM visualization rendering
- [DONE] Video frame generation (MP4)
- [DONE] GeoIP timezone lookup (libmaxminddb)
- [DONE] CIDR allocation mapping with proportional bands

**Planned**:
- FortiGate firewall log parser
- Additional output formats (SVG, JSON)
- Real-time streaming mode
- Interactive web visualization

## Log Format Support

### Honeypot Sensor Logs (Supported)
Standard syslog format with these characteristics:
- Contains `sensor:` identifier
- `PacketTime:YYYY-MM-DD HH:MM:SS.microseconds`
- Format: `source_ip:port -> dest_ip:port`
- BASE64 encoded packet data
- Example:
```
Feb 22 09:26:39 10.10.10.40 honeypi00 sensor: PacketTime:2019-02-22 17:26:39.092449
Len:60 IPv4/TCP 45.55.247.43:35398 -> 10.10.10.40:5900 ID:58486 TOS:0x0 TTL:51
IpLen:20 DgLen:40 *A**** Seq:0x652f4680 Ack:0xfbb1f77f Win:0xfaef TcpLen:20
Resp: Packetdata:3KYyaJoq1Haglac3CABFAAAo5HZAADMGKsUtN/crCgoKKIpGFwxlL0aA+7H3f1AQ...
```

### FortiGate Firewall Logs (Planned)
Key=value pairs format with pre-resolved country information.

## Building

```bash
./bootstrap      # If building from git
./configure
make
```

**Dependencies**:
- zlib (for gzip support)
- libmaxminddb (for GeoIP lookups)
- ffmpeg (for video generation)
- Standard C build tools (gcc, make)

**Installing Dependencies** (Arch Linux):
```bash
sudo pacman -S zlib libmaxminddb ffmpeg
```

**Installing Dependencies** (Debian/Ubuntu):
```bash
sudo apt-get install zlib1g-dev libmaxminddb-dev ffmpeg
```

**GeoIP Database**:
Download GeoLite2-City.mmdb from [MaxMind](https://dev.maxmind.com/geoip/geolite2-free-geolocation-data) and place in the project directory.

**CIDR Mapping** (Optional):
Pre-generated `cidr_map.txt` included. To regenerate:
```bash
gcc -o cidr_mapper src/cidr_map.c -lmaxminddb
./cidr_mapper GeoLite2-City.mmdb cidr_map.txt 4096
```

## Usage

### Basic Visualization

```bash
# Process honeypot logs with default settings (1 minute bins)
./src/tplot logs/sensor.log.gz

# Use 5-minute time bins for longer-term patterns
./src/tplot -p 5m logs/sensor.log.gz

# Custom output directory
./src/tplot -p 5m -o output logs/sensor.log.gz

# Process without generating video (keep frames only)
./src/tplot -p 5m -V logs/sensor.log.gz

# Enable debug output
./src/tplot -d 1 -p 5m logs/sensor.log.gz

# Process multiple files using the wrapper script
# (processes N oldest files from logs/ directory)
./tplot.sh 7              # Process last 7 files with default settings
./tplot.sh 7 -p 5m        # Process last 7 files with 5-minute bins
./tplot.sh 7 -d 1 -p 5m   # Process last 7 files with debug output
```

**Note**: The `tplot.sh` wrapper script automatically finds and processes log files in oldest-to-newest order. For files named `local.N.gz`, higher numbers are treated as older (e.g., `local.604.gz` is older than `local.0.gz`).

### Command-Line Options

```
tplot v0.1.0 [Oct 14 2025]

syntax: tplot [options] filename [filename ...]
 -c|--codec CODEC       video codec (default: libx264)
                        examples: libx264, libx265, libvpx-vp9
 -C|--cidr-map FILE     CIDR mapping file (default: cidr_map.txt)
 -d|--debug (0-9)       enable debugging info
 -D|--duration SECS     target video duration in seconds (default: 300)
                        FPS and decay auto-scale based on data span
 -f|--fps FPS           video framerate (default: auto-scaled)
                        baseline: 1 day = 3 FPS, scales linearly
 -h|--help              this info
 -o|--output DIR        output directory for frames/video (default: plots)
 -p|--period DURATION   time bin period (default: 1m)
                        examples: 1m, 5m, 15m, 30m, 60m, 120s, 1h
 -v|--version           display version information
 -V|--no-video          don't generate video (keep frames only)
 filename               one or more files to process
```

### Auto-Scaling

**FPS and decay auto-scale** based on the time span of your data:

- **Baseline**: 1 day of data → 3 FPS, 3 hour decay
- **Scaling**: N days of data → N×3 FPS, N×3 hours decay
- **Examples**:
  - 2 days → 6 FPS, 6 hour decay
  - 10 days → 30 FPS, 30 hour decay
  - 0.5 days (12 hours) → 2 FPS, 1.5 hour decay

This ensures consistent video playback speed and appropriate decay timing regardless of your data's time span. The default target video duration is 5 minutes (300 seconds), configurable with `-D`.

### Output Files

- **Frame images**: `plots/frame_YYYYMMDD_HHMMSS_NNNN.ppm` (PPM format, 15MB each)
- **Video file**: `plots/output.mp4` (H.264 encoded)

Each frame shows a heatmap of attack sources over that time period, with CIDR blocks naturally clustering together.

### Understanding the Visualization

**Default Mode (Direct IP Mapping)**:
- **Hilbert curve**: Square region in center of frame
- **CIDR clustering**: Related IPs (same /8, /16, /24) appear as visual clusters
- **Dark blue overlay**: Non-routable IP space (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, etc.)
- **Color scale**: Black background with attack intensity gradient
  - Black: No activity
  - White: Low volume attacks (single events clearly visible)
  - Yellow: Medium volume attacks
  - Red: High volume attacks (larger volume = more red)
- **Decay effect**: IPs fade over time (auto-scaled, default 3 hours for 1 day of data)
- **Static positioning**: Each IP always appears at the same coordinate across all frames

**Enhanced Mode (With CIDR Mapping)**:
- **X-axis**: Geographic timezone (UTC-12 to UTC+14), proportional to CIDR density
  - US timezones: ~30-37% of X-axis
  - Europe (UTC+0): ~37%
  - Asia (UTC+8): ~10%
- **Y-axis**: CIDR clustering within timezone bands

## Performance

Tested with 7.4M line log file (631MB compressed, 3.4GB uncompressed):
- **Processing time**: 58.8 seconds
- **Throughput**: 125,488 lines/second
- **Bandwidth**: 58.4 MB/second
- **Events processed**: 5,482,175 honeypot events
- **Frames generated**: 577 frames (5-minute bins)
- **Video duration**: 26+ minutes at 3 FPS
- **Memory**: Streaming, low memory footprint
- **Success rate**: 74.3% (sensor logs), 25.7% skipped (FortiGate logs)

## Security Implications

Assume that there are errors in the tplot source that would allow a specially crafted log file to allow an attacker to exploit tplot to gain access to the computer that it is running on! Don't trust this software and install and use it at your own risk.

## Bugs

I am not a programmer by any stretch of the imagination. I have attempted to remove the obvious bugs and other programmer related errors but please keep in mind the first sentence. If you find an issue with code, please send me an e-mail with details and I will be happy to look into it.

Ron Dilley
ron.dilley@uberadmin.com
