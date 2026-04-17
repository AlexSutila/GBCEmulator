#!/usr/bin/env bash
VERBOSITY=2
set -euo pipefail

# Make sure pip package is installed
pip show pytest-md-report
[ $? -ne 0 ] && pip install pytest-md-report

# Run test framework against suite
pytest testing/ --md-report \
    --md-report-verbose=$VERBOSITY \
    --md-report-output "assets/test_results.md"
