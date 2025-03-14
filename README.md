# prct - Linux Process Tree Analyzer

prct is a command-line utility written in C for Linux systems that provides comprehensive analysis and management of process trees.

## Purpose

prct was developed to provide system administrators and developers with a powerful tool for understanding and controlling process hierarchies on Linux systems. It can be used for debugging, monitoring, and managing process resources.

## Features

*   Lists process relationships (parent, child, sibling)
*   Identifies defunct (zombie) processes
*   Controls process states (kill, stop, continue) using signals
*   Provides detailed information about process resource usage (optional - if you implemented resource monitoring)

## Compilation

To compile prct, use the following command:

```bash
gcc prct.c -o prct
