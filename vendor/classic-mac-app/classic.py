#!/usr/bin/env python3
"""Build, deploy, and test a Retro68 application in a warm classic Mac guest."""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


VERSION = "0.1.0"
CONFIG_NAME = "classic-project.json"
PORT = 1984
TARGET_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")


@dataclass(frozen=True)
class Project:
    root: Path
    application: str
    run_target: str
    test_target: str
    test_timeout: int

    @classmethod
    def load(cls, root):
        root = Path(root).resolve()
        path = root / CONFIG_NAME
        try:
            config = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError as error:
            raise RuntimeError(f"no {CONFIG_NAME} found in {root}") from error
        except json.JSONDecodeError as error:
            raise RuntimeError(f"invalid {CONFIG_NAME}: {error}") from error
        if not isinstance(config, dict):
            raise RuntimeError(f"{CONFIG_NAME} must contain a JSON object")
        if config.get("schemaVersion") != 1:
            raise RuntimeError(f"{CONFIG_NAME} requires schemaVersion 1")

        required = ("application", "runTarget", "testTarget", "testTimeoutSeconds")
        missing = [key for key in required if key not in config]
        if missing:
            raise RuntimeError(f"{CONFIG_NAME} is missing: {', '.join(missing)}")

        application = config["application"]
        if not isinstance(application, str) or not application or Path(application).name != application:
            raise RuntimeError("application must be a non-empty file name")
        for key in ("runTarget", "testTarget"):
            value = config[key]
            if not isinstance(value, str) or TARGET_PATTERN.fullmatch(value) is None:
                raise RuntimeError(f"{key} is not a valid Nix attribute name")
        timeout = config["testTimeoutSeconds"]
        if isinstance(timeout, bool) or not isinstance(timeout, int) or timeout <= 0:
            raise RuntimeError("testTimeoutSeconds must be a positive integer")
        return cls(root, application, config["runTarget"], config["testTarget"], timeout)


def build(project, target, *options):
    if TARGET_PATTERN.fullmatch(target) is None:
        raise RuntimeError(f"invalid Nix target: {target}")
    command = [
        "nix", "build", f".#{target}", "--no-link", "--print-out-paths",
        "--option", "fallback", "false", *options,
    ]
    result = subprocess.run(
        command, cwd=project.root, check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, timeout=180,
    )
    outputs = [line for line in result.stdout.splitlines() if line]
    if len(outputs) != 1:
        raise RuntimeError(f".#{target} produced {len(outputs)} output paths; expected one")
    return Path(outputs[0])


def ensure_vm(vm_name):
    if not vm_name:
        return
    utmctl = shutil.which("utmctl")
    if utmctl is None:
        bundled = Path("/Applications/UTM.app/Contents/MacOS/utmctl")
        if bundled.exists():
            utmctl = str(bundled)
    if utmctl is None:
        raise RuntimeError("UTM VM requested, but utmctl was not found")
    status = subprocess.run(
        [utmctl, "status", vm_name], capture_output=True, text=True, timeout=15,
    )
    if "started" not in status.stdout.lower():
        subprocess.run([utmctl, "start", vm_name], check=True, timeout=30)


def wait_for_guest(guest, timeout):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((guest, PORT), timeout=2):
                return
        except OSError as error:
            last_error = error
            time.sleep(1)
    raise RuntimeError(f"LaunchAPPLServer at {guest}:{PORT} is unavailable: {last_error}")


def launch_command(project, target, guest):
    tools = build(project, "retro68-tools", "--max-jobs", "0")
    app = build(project, target) / f"{project.application}.bin"
    if not app.is_file():
        raise RuntimeError(f"build did not produce {app.name}")
    return [
        str(tools / "bin" / "LaunchAPPL"), "-e", "tcp",
        "--tcp-address", guest, str(app),
    ]


def validate_tap(report):
    lines = [line.strip() for line in report.splitlines() if line.strip()]
    plans = [line for line in lines if re.fullmatch(r"1\.\.[0-9]+", line)]
    if len(plans) != 1:
        raise RuntimeError(f"guest report contains {len(plans)} TAP plans; expected one")
    results = [line for line in lines if re.match(r"^(?:not )?ok\b", line)]
    failures = [line for line in results if line.startswith("not ok")]
    expected = int(plans[0].split("..", 1)[1])
    if len(results) != expected:
        raise RuntimeError(f"guest reported {len(results)} of {expected} planned tests")
    if failures:
        raise RuntimeError("guest test failed: " + "; ".join(failures))
    return results


def selected_guest(args):
    guest = getattr(args, "guest", None) or os.environ.get("CLASSIC_MAC_GUEST")
    if not guest:
        raise RuntimeError("guest address is required (argument or CLASSIC_MAC_GUEST)")
    return guest


def selected_vm(args):
    return getattr(args, "vm", None) or os.environ.get("CLASSIC_MAC_VM")


def run(project, args):
    guest = selected_guest(args)
    ensure_vm(selected_vm(args))
    wait_for_guest(guest, args.guest_timeout)
    target = args.target or project.run_target
    print(f"Launching {project.application} from .#{target}; quit it in the guest to return.")
    command = launch_command(project, target, guest)
    with tempfile.TemporaryDirectory(prefix="linewren-launch-") as work:
        return subprocess.run(command, cwd=work, check=True).returncode


def test(project, args):
    guest = selected_guest(args)
    ensure_vm(selected_vm(args))
    wait_for_guest(guest, args.guest_timeout)
    target = args.target or project.test_target
    timeout = args.timeout or project.test_timeout
    command = launch_command(project, target, guest)
    with tempfile.TemporaryDirectory(prefix="linewren-smoke-") as work:
        result = subprocess.run(command, cwd=work, capture_output=True, timeout=timeout)
    sys.stderr.buffer.write(result.stderr)
    report = result.stdout.decode("utf-8", errors="replace")
    print(report, end="" if report.endswith("\n") else "\n")
    tests = validate_tap(report)
    if result.returncode != 0:
        raise RuntimeError(f"LaunchAPPL exited with status {result.returncode}")
    print(f"PASS: {len(tests)} guest tests ran in classic Mac OS.")
    return 0


def doctor(project, args):
    subprocess.run(["nix", "--version"], check=True)
    tools = build(project, "retro68-tools", "--max-jobs", "0")
    launch = tools / "bin" / "LaunchAPPL"
    if not launch.is_file():
        raise RuntimeError("Retro68 tools do not contain LaunchAPPL")
    print(f"ok: project: {project.root}")
    print(f"ok: configuration schema: 1")
    print(f"ok: LaunchAPPL: {launch}")
    guest = getattr(args, "guest", None) or os.environ.get("CLASSIC_MAC_GUEST")
    if guest:
        ensure_vm(selected_vm(args))
        wait_for_guest(guest, args.guest_timeout)
        print(f"ok: LaunchAPPLServer: {guest}:{PORT}")
    else:
        print("skip: no guest address supplied")
    return 0


def build_only(project, args):
    output = build(project, args.target or "default")
    print(output)
    return 0


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--version", action="version", version=f"classic {VERSION}")
    result.add_argument("--project-root", default=".", help="directory containing classic-project.json")
    commands = result.add_subparsers(dest="command", required=True)

    build_parser = commands.add_parser("build", help="build a Nix target")
    build_parser.add_argument("--target")
    build_parser.set_defaults(handler=build_only)

    for name, handler in (("run", run), ("test", test)):
        command = commands.add_parser(name, help=f"{name} in a warm classic Mac guest")
        command.add_argument("guest", nargs="?", help="guest IPv4 address")
        command.add_argument("--target")
        command.add_argument("--vm", help="start this UTM VM if necessary")
        command.add_argument("--guest-timeout", type=int, default=60)
        if name == "test":
            command.add_argument("--timeout", type=int)
        command.set_defaults(handler=handler)

    check = commands.add_parser("doctor", help="check the build and guest connection")
    check.add_argument("guest", nargs="?", help="guest IPv4 address")
    check.add_argument("--vm", help="start this UTM VM if necessary")
    check.add_argument("--guest-timeout", type=int, default=60)
    check.set_defaults(handler=doctor)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        project = Project.load(args.project_root)
        return args.handler(project, args)
    except subprocess.TimeoutExpired as error:
        print(f"timed out: {' '.join(map(str, error.cmd))}", file=sys.stderr)
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr, file=sys.stderr, end="")
        print(f"command failed with status {error.returncode}", file=sys.stderr)
    except (OSError, RuntimeError, KeyError, ValueError) as error:
        print(error, file=sys.stderr)
    except KeyboardInterrupt:
        return 130
    return 1


if __name__ == "__main__":
    sys.exit(main())
