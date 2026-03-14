"""
PlatformIO extra script – automatically builds the Vite Web UI into
ESP_Hub/data/ before the LittleFS image is created and uploaded.

Triggered by:
    pio run -e esp_hub -t uploadfs

The script runs 'npm install' (only when node_modules/ is absent)
and then 'npm run build' inside ESP_Hub/ui/.
"""

import os
import subprocess
import sys

Import("env")  # noqa: F821 – injected by PlatformIO


def _build_ui(source, target, env):  # noqa: ARG001
    project_dir = env.subst("$PROJECT_DIR")  # …/ESP_Hub
    ui_dir = os.path.join(project_dir, "ui")

    if not os.path.isdir(ui_dir):
        sys.exit(
            "[pre_build_fs] ERROR: ui/ directory not found at {}".format(ui_dir)
        )

    npm = "npm.cmd" if sys.platform == "win32" else "npm"

    # Install dependencies only when node_modules directory is absent.
    # To force a clean reinstall, delete node_modules manually and rerun.
    node_modules = os.path.join(ui_dir, "node_modules")
    if not os.path.isdir(node_modules):
        print("[pre_build_fs] Running: npm install")
        result = subprocess.run([npm, "install"], cwd=ui_dir)
        if result.returncode != 0:
            sys.exit("[pre_build_fs] ERROR: 'npm install' failed (see output above)")

    print("[pre_build_fs] Running: npm run build  (output → ESP_Hub/data/)")
    result = subprocess.run([npm, "run", "build"], cwd=ui_dir)
    if result.returncode != 0:
        sys.exit("[pre_build_fs] ERROR: 'npm run build' failed (see output above)")

    print("[pre_build_fs] Web UI build complete.")


# Hook runs immediately before the LittleFS image is packed and uploaded
env.AddPreAction("uploadfs", _build_ui)
