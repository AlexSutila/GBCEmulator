#!/usr/bin/env bash
VERBOSITY=2
set -euo pipefail

# Make sure pip package is installed
pip show pytest-md-report
[ $? -ne 0 ] && pip install pytest-md-report
[ -d release ] || { echo "Missing release build directory: 'release'" >&2; exit 1; }

# Run test framework against suite
PYTHONPATH=release/ pytest testing/test_mooneye.py --md-report \
    --md-report-verbose=$VERBOSITY \
    --md-report-output "assets/test_results.md"
