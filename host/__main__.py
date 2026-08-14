"""Entry point so the bridge can be run as ``python3 host/`` or ``python3 -m host``.

Kept trivial on purpose: everything real is in ``gridpulse/cli.py``, which is
importable and therefore testable without spawning a process.
"""

import os
import sys

# Allow running this directory directly (``python3 host/``), where the parent is not
# automatically on sys.path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gridpulse import cli  # noqa: E402

if __name__ == "__main__":
    sys.exit(cli.main())
