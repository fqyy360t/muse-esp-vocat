#!/usr/bin/env sh
# Thin wrapper for Git Bash / macOS / Linux users.
# All logic lives in setup.py so Windows cmd/PyCharm users can run
# "python setup.py" directly (no .bat encoding issues).
set -e
cd "$(dirname "$0")"
exec python setup.py "$@"
