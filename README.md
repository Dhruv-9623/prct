# prct - Linux Process Tree Analyzer

prct is a command-line utility written in C for Linux systems that provides comprehensive analysis of process trees.

## Purpose

This project  demonstrates proficiency in system calls, process management, and signal handling.

## Features

*   Lists process relationships (parent, child, sibling)
*   Identifies defunct processes
*   Controls process states (kill, stop, continue) using signals

## Compilation

To compile prct, use the following command:

```bash
gcc -o prct prct.c
