#!/usr/bin/env python3
"""Bounded, isolated read-only application startup under all supported selectors."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import time


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise AssertionError("Process did not terminate promptly")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--forbid-path", type=Path)
    parser.add_argument("--logs", type=Path, required=True)
    args = parser.parse_args()
    args.logs.mkdir(parents=True, exist_ok=True)
    executable, prefix = args.executable.resolve(), args.prefix.resolve()
    for mode in ("default", "environment", "command-line", "external-config"):
        with tempfile.TemporaryDirectory(prefix="uqc105-") as directory:
            root = Path(directory)
            env = dict(PATH="", LANG="C.UTF-8", HOME=str(root),
                       QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       QT_QPA_PLATFORMTHEME="", QML_IMPORT_TRACE="1", QT_DEBUG_PLUGINS="1",
                       QT_FORCE_STDERR_LOGGING="1",
                       QT_LOGGING_RULES="qt.qml.import.debug=true;qt.core.plugin.loader.debug=true",
                       LD_LIBRARY_PATH=str(prefix / "lib"),
                       DBUS_SESSION_BUS_ADDRESS=f"unix:path={root}/unavailable-bus")
            for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR",
                        "XDG_CONFIG_DIRS", "XDG_DATA_DIRS"):
                path = root / key
                path.mkdir(mode=0o700)
                env[key] = str(path)
            command = [str(executable)]
            expected = "Holonight" if mode == "default" else "Fusion"
            if mode == "environment":
                env["QT_QUICK_CONTROLS_STYLE"] = "Fusion"
            elif mode == "command-line":
                env["QT_QUICK_CONTROLS_STYLE"] = "Holonight"
                command += ["-style", "Fusion"]
            elif mode == "external-config":
                config = root / "controls.conf"
                config.write_text("[Controls]\nStyle=Fusion\n")
                env["QT_QUICK_CONTROLS_CONF"] = str(config)
            log_path = args.logs / f"{mode}.log"
            with log_path.open("w") as log:
                process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        assert process.poll() is None, f"{mode}: premature exit {process.returncode}"
                        time.sleep(0.05)
                    maps = Path(f"/proc/{process.pid}/maps").read_text()
                    qml = prefix / "lib/qt6/qml/Holonight"
                    for module in ("Core/libholonight_core_qml.so", "Controls/libholonight_controls_qml.so"):
                        assert str(qml / module) in maps, f"{mode}: missing staged {module}"
                    if expected == "Holonight":
                        assert str(qml / "libholonight_qml.so") in maps, f"{mode}: missing staged style"
                    else:
                        assert "libqtquickcontrols2fusionstyleplugin" in maps, f"{mode}: missing Fusion plugin"
                        assert "libholonight_qml.so" not in maps, f"{mode}: unexpected Holonight style"
                    (args.logs / f"{mode}.maps").write_text(maps)
                finally:
                    stop(process)
            evidence = log_path.read_text()
            assert re.search(rf"/{expected}/(?:Button|ComboBox)\.qml", evidence), f"{mode}: no implementation evidence"
            diagnostics = re.findall(r"^.*(?:ReferenceError|TypeError|Binding loop|Cannot assign|Unable to assign|"
                                     r"is not a type|is not installed|Failed to create|Error loading|QML [A-Za-z]+:|"
                                     r"Required property|Skipped invalid|failed to load component).*$", evidence, re.M | re.I)
            assert not diagnostics, "\n".join(diagnostics)
            if args.forbid_path:
                forbidden = str(args.forbid_path.resolve())
                assert forbidden not in evidence.replace(str(prefix), "<installed>"), f"{mode}: build discovery"
                assert forbidden not in maps.replace(str(prefix), "<installed>"), f"{mode}: build library loaded"
            print(f"PASS {mode}: {expected} origins and plugins; observed, terminated and reaped")


if __name__ == "__main__":
    main()
