#!/usr/bin/env python3
"""CSI 运动拍照链看护（2026-09-09，目标：6h 持续监控）。

观察对象：ai-thinker 板（CSI 触发模式固件，ota_1）三条线
  1. /api/status 30s 轮询：堆水位 / stream_clients（NVR 是否占槽）/
     motion_events 计数 / csi 快照 / 场景亮度。重启检测 = uptime 回退。
  2. 串口日志跟随（tools/overnight_serial.log，采集器持有端口）：
     MOTION 事件 → 拍照链（探针/闪光/落盘）→ 崩溃签名。
  3. 周期短流探针（默认 30min 一次 × 20s）：帧数/fps/字节。故意不常驻
     ——板为单槽（stream_clients_max=1），常驻会与 NVR 永久互踢。

产物（--out 目录）：status.jsonl / events.jsonl / stream_probes.jsonl /
report.md（滚动汇总）。停止：放 STOP 文件或 kill。

Usage: csi_photo_watch.py <ip> [--hours 6] [--out DIR] [--probe-every 1800]
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.request

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# 串口签名 → (事件类型, 提取组)
SERIAL_PATTERNS = [
    ("csi_motion_state", re.compile(r"motion=(MOTION|IDLE) score=([\d.]+)")),
    ("calibration_ok", re.compile(r"calibration OK \(thr=([\d.]+)\)")),
    ("calibration_failed", re.compile(r"calibration FAILED")),
    ("runtime_fault", re.compile(r"runtime fault: (.*)")),
    ("motion_chain_start", re.compile(r"Motion detected!(.*?) \(scene (DARK|bright)\)")),
    ("flash_warmup", re.compile(r"Dark scene — flash photo")),
    ("photo_saved", re.compile(r"Motion photo saved: ([\w.\-]+)")),
    ("photo_fail", re.compile(r"Failed to (?:save motion photo|capture)")),
    ("probe_result", re.compile(r"probe result(?: \(on-demand\))?: (\d+)% dark=(YES|no)")),
    ("probe_raw", re.compile(r"probe: locked-exposure luma=(\d+)")),
    ("boot_banner", re.compile(r"MiBee Cam\s*$")),
    ("espectre_start", re.compile(r"ESPectre sensing started")),
    ("sd_unavailable", re.compile(r"SD card not available")),
]
CRASH_PATTERNS = [
    ("guru", re.compile(r"Guru Meditation")),
    ("abort", re.compile(r"abort\(\)")),
    ("assert", re.compile(r"assert failed")),
    ("wdt", re.compile(r"(?:TASK|INT) WDT(?: is not| triggered| triggered in)" )),
    ("rst", re.compile(r"rst:0x[0-9a-f]+")),
    ("backtrace", re.compile(r"Backtrace:")),
    ("sock_err", re.compile(r"httpd_sock_err: send/recv \d+")),
]


class StatusPoller(threading.Thread):
    def __init__(self, ip, out, interval=30.0):
        super().__init__(daemon=True)
        self.ip, self.out, self.interval = ip, out, interval
        self.stop_ev = threading.Event()
        self.last_uptime = None
        self.alerts = []

    def alert(self, msg):
        line = f"[{time.strftime('%H:%M:%S')}] ALERT {msg}"
        self.alerts.append(line)
        with open(os.path.join(self.out, "events.jsonl"), "a") as f:
            f.write(json.dumps({"t": time.time(), "kind": "alert", "msg": msg}) + "\n")
        print(line, flush=True)

    def run(self):
        while not self.stop_ev.is_set():
            try:
                with urllib.request.urlopen(f"http://{self.ip}/api/status", timeout=10) as r:
                    d = json.loads(r.read())["data"]
                rec = {
                    "t": time.time(), "uptime": d.get("uptime"),
                    "free_heap": d.get("free_heap"), "min_heap": d.get("min_heap"),
                    "stream_clients": d.get("stream_clients"),
                    "motion_events": d.get("motion_events"),
                    "brightness_pct": d.get("brightness_pct"),
                    "scene_dark": d.get("scene_dark"),
                    "csi": d.get("csi"),
                }
                with open(os.path.join(self.out, "status.jsonl"), "a") as f:
                    f.write(json.dumps(rec) + "\n")
                up = rec["uptime"]
                if up is not None and self.last_uptime is not None and up < self.last_uptime:
                    self.alert(f"REBOOT detected: uptime {self.last_uptime}s -> {up}s")
                self.last_uptime = up
            except Exception as e:
                self.alert(f"status poll failed: {e}")
            self.stop_ev.wait(self.interval)


class SerialFollower(threading.Thread):
    def __init__(self, log_path, out):
        super().__init__(daemon=True)
        self.log_path, self.out = log_path, out
        self.stop_ev = threading.Event()
        self.counts = {}

    def bump(self, key):
        self.counts[key] = self.counts.get(key, 0) + 1

    def run(self):
        # 从当前末尾 -64KB 开始（覆盖重启横幅判定），跟随追加
        try:
            fh = open(self.log_path, "rb")
            fh.seek(0, os.SEEK_END)
            fh.seek(max(0, fh.tell() - 65536))
        except OSError as e:
            print(f"serial follower cannot open {self.log_path}: {e}", flush=True)
            return
        buf = ""
        while not self.stop_ev.is_set():
            line = fh.readline()
            if not line:
                time.sleep(1.0)
                continue
            try:
                text = ANSI_RE.sub("", line.decode("utf-8", "replace")).rstrip()
            except Exception:
                continue
            m = re.search(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\]", text)
            ts = m.group(1) if m else time.strftime("%Y-%m-%d %H:%M:%S")
            text = text[m.end():] if m else text
            for kind, pat in SERIAL_PATTERNS:
                mm = pat.search(text)
                if mm:
                    self.bump(kind)
                    with open(os.path.join(self.out, "events.jsonl"), "a") as f:
                        f.write(json.dumps({"t": ts, "kind": kind, "msg": mm.group(0)}) + "\n")
                    if kind in ("csi_motion_state", "photo_saved", "calibration_ok",
                                "calibration_failed", "runtime_fault"):
                        print(f"[{ts[11:]}] {mm.group(0)}", flush=True)
                    break
            for kind, pat in CRASH_PATTERNS:
                if pat.search(text):
                    self.bump("crash_" + kind)
                    with open(os.path.join(self.out, "events.jsonl"), "a") as f:
                        f.write(json.dumps({"t": ts, "kind": "crash_" + kind,
                                            "msg": text[:200]}) + "\n")
                    print(f"[{ts[11:]}] CRASH-SIG {kind}: {text[:160]}", flush=True)
                    break


def stream_probe_once(ip, dur_s):
    """短探针：连 :81/stream 数 SOI/EOI 帧。返回统计或错误。"""
    t0 = time.time()
    frames = bytes_total = 0
    try:
        s = socket.create_connection((ip, 81), timeout=6)
        s.sendall(b"GET /stream HTTP/1.0\r\nHost: x\r\n\r\n")
        s.settimeout(5)
        chunk = b""
        end = t0 + dur_s
        while time.time() < end:
            try:
                data = s.recv(8192)
            except socket.timeout:
                break
            if not data:
                break
            bytes_total += len(data)
            # EOI 计帧：只保留上一块最后 1 字节防止跨块 EOI 漏计/重计
            chunk = chunk[-1:] + data
            frames += chunk.count(b"\xff\xd9")
            chunk = chunk[-1:]
        s.close()
        dt = time.time() - t0
        return {"t": t0, "dur_s": round(dt, 1), "frames": frames,
                "fps": round(frames / dt, 2) if dt > 0 else 0,
                "kBps": round(bytes_total / dt / 1024, 1)}
    except Exception as e:
        return {"t": t0, "error": str(e)}


def write_report(out, start_t, poller, serial, probes):
    n = lambda k: serial.counts.get(k, 0)
    heaps = []
    clients = {}
    csi_states = {}
    try:
        with open(os.path.join(out, "status.jsonl")) as f:
            for line in f:
                r = json.loads(line)
                if r.get("free_heap"):
                    heaps.append(r["free_heap"])
                c = r.get("stream_clients")
                if c is not None:
                    clients[c] = clients.get(c, 0) + 1
                st = (r.get("csi") or {}).get("state")
                if st:
                    csi_states[st] = csi_states.get(st, 0) + 1
    except OSError:
        pass
    crashes = {k: v for k, v in serial.counts.items() if k.startswith("crash_")}
    lines = [
        "# CSI 运动拍照链 6h 看护 — 滚动报告",
        f"- 窗口：{time.strftime('%m-%d %H:%M', time.localtime(start_t))} → "
        f"{time.strftime('%m-%d %H:%M')}（{(time.time()-start_t)/3600:.2f}h）",
        f"- CSI MOTION 事件：{n('csi_motion_state')}（状态行含 MOTION/IDLE 转移）",
        f"- 拍照链：触发 {n('motion_chain_start')}，闪光路径 {n('flash_warmup')}，"
        f"落盘成功 {n('photo_saved')}，失败 {n('photo_fail')}",
        f"- 暗探针：周期/按需 {n('probe_result')} 次",
        f"- 重启（boot banner）：{n('boot_banner')}；崩溃签名：{crashes or '无'}",
        f"- 堆：free {min(heaps)/1e6:.2f}~{max(heaps)/1e6:.2f}MB"
        if heaps else "- 堆：无样本",
        f"- stream_clients 分布：{clients}",
        f"- csi.state 分布：{csi_states}",
        f"- 流探针：{len(probes)} 次，fps 序列 {[p.get('fps') for p in probes]}",
        f"- 告警：{len(poller.alerts)}",
    ]
    with open(os.path.join(out, "report.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--hours", type=float, default=6.0)
    ap.add_argument("--out", default="/home/mickey/Projects/esp-cam/soak/csi_photo")
    ap.add_argument("--serial-log",
                    default="/home/mickey/Projects/esp-cam/ai-thinker-esp32-cam/tools/overnight_serial.log")
    ap.add_argument("--probe-every", type=float, default=1800.0)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    stop_file = os.path.join(args.out, "STOP")
    if os.path.exists(stop_file):
        os.remove(stop_file)

    poller = StatusPoller(args.ip, args.out)
    serial = SerialFollower(args.serial_log, args.out)
    poller.start(); serial.start()

    print(f"watching {args.ip} for {args.hours}h, out={args.out}", flush=True)
    start_t = time.time()
    probes = []
    next_probe = start_t + 120.0   # 开机 2min 后首探
    end_t = start_t + args.hours * 3600
    while time.time() < end_t:
        if os.path.exists(stop_file):
            print("STOP file found — exiting", flush=True)
            break
        if time.time() >= next_probe:
            r = stream_probe_once(args.ip, 20.0)
            probes.append(r)
            with open(os.path.join(args.out, "stream_probes.jsonl"), "a") as f:
                f.write(json.dumps(r) + "\n")
            print(f"stream probe: {r}", flush=True)
            next_probe = time.time() + args.probe_every
        write_report(args.out, start_t, poller, serial, probes)
        time.sleep(30)

    poller.stop_ev.set(); serial.stop_ev.set()
    write_report(args.out, start_t, poller, serial, probes)
    print("=== FINAL ===", flush=True)
    print(open(os.path.join(args.out, "report.md")).read(), flush=True)


if __name__ == "__main__":
    main()
