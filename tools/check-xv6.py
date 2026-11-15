"""Run a bounded xv6 shell session and save its complete output (Windows/Linux)."""
import argparse
import os
import queue
import subprocess
import threading
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("command")
parser.add_argument("--log", required=True)
parser.add_argument("--timeout", type=int, default=300)
parser.add_argument("--make", action="store_true", help="boot through make qemu")
args = parser.parse_args()
qemu = os.environ.get("QEMU", "qemu-system-riscv64")
cmd = [qemu, "-machine", "virt", "-bios", "none", "-kernel", "kernel/kernel",
       "-m", "128M", "-smp", "3", "-nographic",
       "-global", "virtio-mmio.force-legacy=false",
       "-drive", "file=fs.img,if=none,format=raw,id=x0",
       "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
if args.make:
    cmd = ([os.environ.get("MSYS_BASH", "C:/msys64/usr/bin/bash.exe"),
            "tools/msys-build.sh", "qemu"] if os.name == "nt" else ["make", "qemu"])
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, start_new_session=os.name != "nt")
chunks = queue.Queue()

def read_output():
    while True:
        char = proc.stdout.read(1)
        chunks.put(char)
        if not char:
            break

threading.Thread(target=read_output, daemon=True).start()
output = bytearray()
sent = False
completed = False
deadline = time.monotonic() + args.timeout
try:
    while time.monotonic() < deadline:
        try:
            char = chunks.get(timeout=0.2)
        except queue.Empty:
            continue
        if not char:
            break
        output.extend(char)
        if output.endswith(b"$ "):
            if sent:
                completed = True
                break
            proc.stdin.write((args.command + "\n").encode())
            proc.stdin.flush()
            sent = True
finally:
    if proc.poll() is None:
        try:
            proc.stdin.write(b"\x01x")
            proc.stdin.flush()
            proc.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            if os.name == "nt" and args.make:
                subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif os.name != "nt":
                import signal
                os.killpg(proc.pid, signal.SIGKILL)
            else:
                proc.kill()
            proc.wait()
    path = Path(args.log)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)
    print(output.decode(errors="replace"))
if not completed:
    raise SystemExit("FAIL: xv6 did not return to its shell before exit/timeout")
