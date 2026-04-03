"""
PlatformIO extra script for ESP_Hub filesystem handling.

What it does:
- Builds the Vite UI before buildfs/uploadfs.
  • Normal envs (esp_hub, esp_hub_c6):         ui/       → data/
  • Light envs  (esp_hub_light, esp_hub_c6_light): ui_light/ → data_light/
- On normal firmware upload, triggers uploadfs first so UI updates are
    flashed automatically as part of one upload workflow.
"""

import os
import subprocess
import sys
from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821 – injected by PlatformIO


def _is_light_env(env) -> bool:
    return "light" in env.subst("$PIOENV")


def _build_ui(env):
    project_dir = env.subst("$PROJECT_DIR")  # …/ESP_Hub
    light = _is_light_env(env)
    ui_subdir = "ui_light" if light else "ui"
    ui_dir = os.path.join(project_dir, ui_subdir)

    if not os.path.isdir(ui_dir):
        sys.exit(
            "[pre_build_fs] ERROR: {} directory not found at {}".format(ui_subdir, ui_dir)
        )

    npm = "npm.cmd" if sys.platform == "win32" else "npm"

    # Install dependencies only when node_modules directory is absent.
    # To force a clean reinstall, delete node_modules manually and rerun.
    node_modules = os.path.join(ui_dir, "node_modules")
    if not os.path.isdir(node_modules):
        print("[pre_build_fs] Running: npm install  (in {})".format(ui_subdir))
        result = subprocess.run([npm, "install"], cwd=ui_dir)
        if result.returncode != 0:
            sys.exit("[pre_build_fs] ERROR: 'npm install' failed (see output above)")

    out_dir = "data_light" if light else "data"
    print("[pre_build_fs] Running: npm run build  (output → ESP_Hub/{})".format(out_dir))
    result = subprocess.run([npm, "run", "build"], cwd=ui_dir)
    if result.returncode != 0:
        sys.exit("[pre_build_fs] ERROR: 'npm run build' failed (see output above)")

    print("[pre_build_fs] Web UI build complete.")


def _find_platformio_executable() -> str:
    if sys.platform == "win32":
        candidate = os.path.join(sys.prefix, "Scripts", "platformio.exe")
        if os.path.isfile(candidate):
            return candidate
    else:
        candidate = os.path.join(sys.prefix, "bin", "platformio")
        if os.path.isfile(candidate):
            return candidate

    # Fallback when PlatformIO is in PATH
    return "platformio"


def _ensure_uploadfs_before_upload(source, target, env):  # noqa: ARG001
    # Prevent accidental nested execution loops.
    if os.environ.get("ESP_HUB_SKIP_AUTO_UPLOADFS") == "1":
        return

    pio = _find_platformio_executable()
    pio_env = env.subst("$PIOENV")
    cmd = [pio, "run", "-e", pio_env, "-t", "uploadfs"]

    upload_port = env.subst("$UPLOAD_PORT").strip()
    if upload_port:
        cmd.extend(["--upload-port", upload_port])

    child_env = os.environ.copy()
    child_env["ESP_HUB_SKIP_AUTO_UPLOADFS"] = "1"
    child_env["ESP_HUB_SKIP_UI_BUILD"] = "1"

    print("[pre_build_fs] Auto-sync web files: running uploadfs before upload")
    result = subprocess.run(cmd, cwd=env.subst("$PROJECT_DIR"), env=child_env)
    if result.returncode != 0:
        sys.exit("[pre_build_fs] ERROR: automatic 'uploadfs' failed")


def _needs_ui_build() -> bool:
    if os.environ.get("ESP_HUB_SKIP_UI_BUILD") == "1":
        return False

    requested = set(COMMAND_LINE_TARGETS)
    return bool(requested.intersection({"buildfs", "uploadfs", "upload"}))


# Run UI build immediately when filesystem-related targets are requested.
# This guarantees data/ is fresh before any LittleFS image generation starts.
if _needs_ui_build():
    _build_ui(env)

# A regular firmware upload should also refresh and upload filesystem content.
env.AddPreAction("upload", _ensure_uploadfs_before_upload)
