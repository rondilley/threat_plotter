# Threat Plot (tplot)

by Ron Dilley <ron.dilley@uberadmin.com>

You can find the latest information on tplot [here](http://www.uberadmin.com/Projects/tplot/ "Threat Plot")

## What is Threat Plot (tplot)?

Threat Plot is a visualization tool that processes log files from honeypots and other security sources and plots the data on Hilbert curves using a selectable time domain and format for the curve. The first version plots using the timezone of the source IP address.

## Why use it?

This tool helps security analysts visualize attack patterns and threat data from honeypots and other security log sources. By plotting data on Hilbert curves with timezone-aware visualization, you can identify patterns, trends, and anomalies in security event data.

## Features

- Process honeypot and security log files
- Plot data on Hilbert curves
- Selectable time domain and curve format
- Timezone-aware plotting based on source IP address geography
- Fast log processing engine

## Implementation

Threat Plot has a simple command line interface. In its simplest form, pass a log file as an argument and the output will be a visualization of the threat data.

To get a list of all the options, you can execute the command with the -h or --help switch.

```
tplot v1.0.0 [Oct 12 2025]

syntax: tplot [options] filename [filename ...]
 -c|--cluster           show invariable fields in output
 -d|--debug (0-9)       enable debugging info
 -g|--greedy            ignore quotes
 -h|--help              this info
 -l|--line {line}       show all lines that match template of {line}
 -L|--linefile {fname}  show all the lines that match templates of lines in {fname}
 -m|--match {template}  show all lines that match {template}
 -M|--matchfile {fname} show all the lines that match templates in {fname}
 -n|--cnum {num}        max cluster args [default: 1]
 -t|--templates {file}  load templates to ignore
 -v|--version           display version information
 -w|--write {file}      save templates to file
 filename               one or more files to process, use '-' to read from stdin
```

## Security Implications

Assume that there are errors in the tplot source that would allow a specially crafted log file to allow an attacker to exploit tplot to gain access to the computer that it is running on! Don't trust this software and install and use it at your own risk.

## Bugs

I am not a programmer by any stretch of the imagination. I have attempted to remove the obvious bugs and other programmer related errors but please keep in mind the first sentence. If you find an issue with code, please send me an e-mail with details and I will be happy to look into it.

Ron Dilley
ron.dilley@uberadmin.com
