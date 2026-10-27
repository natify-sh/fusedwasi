"""The neutral users run where their engines were tried: Linux. (The main job of the tests runs on three systems; these have a job of their own, `neutral.yml`.)"""

import sys

collect_ignore_glob = [] if sys.platform.startswith("linux") else ["test_*.py"]
