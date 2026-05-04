#!/usr/bin/env python3

"""
Run iperf2 tests against the host_framework_iperf_validation example.

The DUT exposes a tiny TCP remote-control protocol on port 22336.
This script uses that control socket to start iperf on the DUT and uses
the local iperf2 binary on the Mac/Linux host to drive the opposite side.

Typical usage:
    python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases tcp-rx,tcp-tx
    python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases all --time 60

Assumptions:
  - The host machine is already associated to the DUT SoftAP.
  - The DUT log already reports TRANSPORT_UP, CP_INIT, AP start,
    and iperf remote control ready.
  - iperf2 is installed locally. On macOS, `brew install iperf` is typical.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


THROUGHPUT_RE = re.compile(r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>[KMG])bits/sec")
DURATION_RE = re.compile(r"(?P<start>\d+(?:\.\d+)?)-(?P<end>\d+(?:\.\d+)?)\s+sec")
VALID_CASES = ("tcp-rx", "tcp-tx", "udp-rx", "udp-tx")


@dataclass
class CaseResult:
    case: str
    role: str
    status: str
    throughput_mbps: float | None
    actual_sec: float | None
    expected_sec: int
    note: str
    command: str
    output: str


class ScriptError(RuntimeError):
    pass


def format_case_output(output: str) -> str:
    cleaned = output.strip()
    if cleaned:
        return cleaned
    return "<no local iperf output captured>"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run iperf2 tests against hosted SoftAP DUT")
    parser.add_argument("--dut-ip", default="192.168.4.1", help="DUT SoftAP IP address")
    parser.add_argument("--control-port", type=int, default=22336, help="DUT remote-control TCP port")
    parser.add_argument("--iperf-port", type=int, default=5001, help="iPerf data port")
    parser.add_argument("--interval", type=int, default=3, help="iPerf report interval in seconds")
    parser.add_argument("--time", type=int, default=30, help="Test duration in seconds")
    parser.add_argument("--udp-bandwidth", type=int, default=20, help="UDP bandwidth in Mbps")
    parser.add_argument(
        "--cases",
        default="all",
        help="Comma-separated list from: tcp-rx,tcp-tx,udp-rx,udp-tx or 'all'",
    )
    parser.add_argument("--all", action="store_true", help="Run all four test cases sequentially")
    parser.add_argument("--host-ip", help="Host IP on the DUT SoftAP subnet; auto-detected by default")
    parser.add_argument("--iperf-bin", help="Path to local iperf2 binary; defaults to iperf or iperf2 in PATH")
    parser.add_argument("--status-poll-interval", type=float, default=1.0, help="Remote STATUS poll interval")
    parser.add_argument("--status-timeout", type=int, default=90, help="Remote STATUS wait timeout in seconds")
    parser.add_argument("--inter-case-cooldown", type=int, default=5, help="Cooldown between sequential cases")
    parser.add_argument("--continue-on-error", action="store_true", help="Continue with remaining cases after a failure")
    parser.add_argument("--quick-check", action="store_true", help="Run all cases with a short duration sweep")
    parser.add_argument("--quick-check-duration", type=int, default=10, help="Per-case duration used by --quick-check")
    parser.add_argument("--result-json", help="Optional JSON file path for structured results")
    return parser.parse_args()


def resolve_cases(case_text: str) -> list[str]:
    if case_text == "all":
        return list(VALID_CASES)

    cases = [item.strip().lower() for item in case_text.split(",") if item.strip()]
    invalid = [item for item in cases if item not in VALID_CASES]
    if invalid:
        raise ScriptError(f"unsupported cases: {', '.join(invalid)}")
    return cases


def normalize_args(args: argparse.Namespace) -> None:
    if args.quick_check:
        args.all = True
        args.cases = "all"
        args.time = args.quick_check_duration
        args.inter_case_cooldown = 3

    if args.time < 1:
        raise ScriptError("time must be >= 1")
    if args.interval < 1:
        raise ScriptError("interval must be >= 1")
    if args.time < args.interval:
        args.time = args.interval
    if args.udp_bandwidth <= 0:
        raise ScriptError("udp-bandwidth must be > 0")
    if args.inter_case_cooldown < 0:
        raise ScriptError("inter-case-cooldown must be >= 0")


def find_iperf_binary(explicit: str | None) -> str:
    candidates: Iterable[str]

    if explicit:
        candidates = (explicit,)
    else:
        candidates = ("iperf", "iperf2")

    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved

    raise ScriptError(
        "iperf2 binary not found. On macOS, install it with 'brew install iperf', "
        "or pass --iperf-bin explicitly."
    )


def detect_iperf_version(iperf_bin: str) -> str:
    completed = subprocess.run(
        [iperf_bin, "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=5,
        check=False,
    )
    output = completed.stdout.strip()
    lowered = output.lower()
    if "iperf version 3" in lowered or "iperf 3" in lowered:
        raise ScriptError("detected iperf3. This example only works with iperf 2.x")
    if "iperf" not in lowered:
        raise ScriptError(f"could not parse iperf version output: {output}")
    return output.splitlines()[0]


def detect_host_ip(peer_ip: str) -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((peer_ip, 9))
        return sock.getsockname()[0]
    finally:
        sock.close()


def choose_local_server_port(host_ip: str, preferred_port: int) -> tuple[int, str]:
    for candidate_port in (preferred_port, 0):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind((host_ip, candidate_port))
            selected_port = probe.getsockname()[1]
        except OSError:
            if candidate_port == preferred_port:
                continue
            raise ScriptError("failed to reserve a local TCP port for iperf server")
        finally:
            probe.close()

        if selected_port == preferred_port:
            return selected_port, ""

        return selected_port, f"本地端口 {preferred_port} 已被占用，已切换到空闲端口 {selected_port}。"

    raise ScriptError(f"local port {preferred_port} is unavailable and no fallback port could be reserved")


def send_control_command(host: str, port: int, command: str, timeout: float = 5.0) -> str:
    payload = f"{command.strip()}\n".encode()
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload)
        sock.shutdown(socket.SHUT_WR)

        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)

    response = b"".join(chunks).decode(errors="replace").strip()
    return response


def ensure_ok_response(response: str, context: str) -> None:
    if not response.startswith("OK"):
        raise ScriptError(f"{context} failed: {response}")


def wait_for_remote_idle(args: argparse.Namespace) -> None:
    deadline = time.time() + args.status_timeout
    while time.time() < deadline:
        response = send_control_command(args.dut_ip, args.control_port, "STATUS")
        ensure_ok_response(response, "STATUS")
        if response == "OK idle":
            return
        time.sleep(args.status_poll_interval)

    raise ScriptError("timed out waiting for DUT iperf state to become idle")


def ensure_remote_idle(args: argparse.Namespace) -> None:
    response = send_control_command(args.dut_ip, args.control_port, "STATUS")
    ensure_ok_response(response, "STATUS")
    if response == "OK idle":
        return

    send_control_command(args.dut_ip, args.control_port, "STOP")
    wait_for_remote_idle(args)


def run_local_command(command: list[str], timeout: int) -> tuple[int, str]:
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )
    return completed.returncode, completed.stdout


def collect_local_server_output(
    process: subprocess.Popen[str],
    capture_path: Path,
    wait_timeout: float = 5.0,
) -> tuple[int | None, str]:
    deadline = time.time() + wait_timeout

    while time.time() < deadline:
        if process.poll() is not None:
            break
        time.sleep(0.25)

    if process.poll() is None:
        try:
            process.terminate()
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    exit_code = process.poll()
    output = capture_path.read_text(encoding="utf-8", errors="replace")
    capture_path.unlink(missing_ok=True)
    return exit_code, output


def extract_throughput_mbps(output: str) -> float | None:
    matches = list(THROUGHPUT_RE.finditer(output))
    if not matches:
        return None

    value = float(matches[-1].group("value"))
    unit = matches[-1].group("unit")
    if unit == "K":
        return value / 1000.0
    if unit == "M":
        return value
    if unit == "G":
        return value * 1000.0
    return None


def extract_duration_seconds(output: str) -> float | None:
    matches = list(DURATION_RE.finditer(output))
    if not matches:
        return None

    end_value = float(matches[-1].group("end"))
    return round(end_value, 2)


def format_duration(actual_sec: float | None, expected_sec: int) -> str:
    if actual_sec is None:
        return "-"
    return f"{actual_sec:.1f}s/{expected_sec}s"


def format_megabytes_per_second(throughput_mbps: float | None) -> str:
    if throughput_mbps is None:
        return "-"
    return f"{throughput_mbps / 8.0:.3f}"


def join_notes(*notes: str) -> str:
    cleaned = [note.strip() for note in notes if note and note.strip()]
    return " ".join(cleaned)


def evaluate_case_status(
    exit_code: int | None,
    throughput_mbps: float | None,
    actual_sec: float | None,
    expected_sec: int,
    note: str = "",
    allow_nonzero_exit_with_output: bool = False,
) -> tuple[str, str]:
    if throughput_mbps is None:
        return "FAIL", join_notes(note, "未从本地 iperf 输出中解析到带宽值。")

    if exit_code not in (None, 0) and not allow_nonzero_exit_with_output:
        return "FAIL", join_notes(note, f"本地 iperf 退出码为 {exit_code}。")

    if actual_sec is not None and actual_sec < expected_sec * 0.8:
        return "WARN", join_notes(note, f"实际仅运行 {actual_sec:.1f}s / 预期 {expected_sec}s。")

    return "PASS", note


def case_role(case_name: str) -> str:
    if case_name in ("tcp-rx", "udp-rx"):
        return "dut-server/local-client"
    return "dut-client/local-server"


def run_rx_case(args: argparse.Namespace, iperf_bin: str, is_udp: bool) -> CaseResult:
    case_name = "udp-rx" if is_udp else "tcp-rx"
    start_command = f"START {'udp' if is_udp else 'tcp'}-server {args.iperf_port} {args.interval} {args.time}"
    response = send_control_command(args.dut_ip, args.control_port, start_command)
    ensure_ok_response(response, start_command)
    time.sleep(1.0)

    local_cmd = [iperf_bin, "-c", args.dut_ip, "-p", str(args.iperf_port), "-i", str(args.interval), "-t", str(args.time)]
    if is_udp:
        local_cmd.extend(["-u", "-b", f"{args.udp_bandwidth}M"])

    exit_code, output = run_local_command(local_cmd, timeout=args.time + 20)

    stop_note = ""
    try:
        send_control_command(args.dut_ip, args.control_port, "STOP")
        wait_for_remote_idle(args)
    except ScriptError as error:
        stop_note = f"DUT STOP/STATUS 检查失败: {error}"

    throughput_mbps = extract_throughput_mbps(output)
    actual_sec = extract_duration_seconds(output)
    status, note = evaluate_case_status(exit_code, throughput_mbps, actual_sec, args.time, note=stop_note)

    return CaseResult(
        case=case_name,
        role="dut-server/local-client",
        status=status,
        throughput_mbps=throughput_mbps,
        actual_sec=actual_sec,
        expected_sec=args.time,
        note=note,
        command=" ".join(local_cmd),
        output=output,
    )


def run_tx_case(args: argparse.Namespace, iperf_bin: str, is_udp: bool, host_ip: str) -> CaseResult:
    case_name = "udp-tx" if is_udp else "tcp-tx"
    server_port, port_note = choose_local_server_port(host_ip, args.iperf_port)
    local_cmd = [iperf_bin, "-s", "-1", "-B", host_ip, "-p", str(server_port), "-i", str(args.interval)]
    if is_udp:
        local_cmd = [iperf_bin, "-s", "-u", "-1", "-B", host_ip, "-p", str(server_port), "-i", str(args.interval)]

    capture_file = tempfile.NamedTemporaryFile(prefix=f"{case_name}-", suffix=".log", delete=False)
    capture_file.close()
    capture_path = Path(capture_file.name)
    capture_stream = capture_path.open("w")

    try:
        server_proc = subprocess.Popen(local_cmd, stdout=capture_stream, stderr=subprocess.STDOUT, text=True)
    finally:
        capture_stream.close()

    try:
        time.sleep(1.0)
        if is_udp:
            start_command = (
                f"START udp-client {host_ip} {server_port} {args.interval} "
                f"{args.time} {args.udp_bandwidth}"
            )
        else:
            start_command = f"START tcp-client {host_ip} {server_port} {args.interval} {args.time}"

        response = send_control_command(args.dut_ip, args.control_port, start_command)
        ensure_ok_response(response, start_command)
        wait_for_remote_idle(args)
    finally:
        exit_code, output = collect_local_server_output(server_proc, capture_path)

    throughput_mbps = extract_throughput_mbps(output)
    actual_sec = extract_duration_seconds(output)
    note = port_note
    if exit_code not in (None, 0) and throughput_mbps is not None:
        note = join_notes(port_note, f"本地 server 退出码为 {exit_code}，但已解析到有效带宽。")
    status, note = evaluate_case_status(
        exit_code,
        throughput_mbps,
        actual_sec,
        args.time,
        note=note,
        allow_nonzero_exit_with_output=True,
    )

    return CaseResult(
        case=case_name,
        role="dut-client/local-server",
        status=status,
        throughput_mbps=throughput_mbps,
        actual_sec=actual_sec,
        expected_sec=args.time,
        note=note,
        command=" ".join(local_cmd),
        output=output,
    )


def print_summary(results: list[CaseResult]) -> None:
    print("\n汇总 / Summary")
    print("case      status  Mbps       MB/s       duration      note")
    print("--------  ------  ---------  ---------  ------------  ------------------------------")
    for result in results:
        value = "-" if result.throughput_mbps is None else f"{result.throughput_mbps:.3f}"
        duration = format_duration(result.actual_sec, result.expected_sec)
        note = result.note if result.note else "-"
        print(
            f"{result.case:<8}  {result.status:<6}  {value:>9}  "
            f"{format_megabytes_per_second(result.throughput_mbps):>9}  {duration:>12}  {note}"
        )


def print_case_failure(result: CaseResult) -> None:
    print(f"[失败] {result.case} / {result.case}")
    if result.note:
        print(result.note)
    print("--- local iperf output ---")
    print(format_case_output(result.output))
    print("--- end local iperf output ---")


def print_case_result(result: CaseResult) -> None:
    if result.status == "FAIL":
        print_case_failure(result)
        return

    throughput = f"{result.throughput_mbps:.3f} Mbps ({result.throughput_mbps / 8.0:.3f} MB/s)"
    duration = format_duration(result.actual_sec, result.expected_sec)
    print(f"[结果] {result.case} = {throughput} [{duration}]")
    if result.note:
        prefix = "[警告]" if result.status == "WARN" else "[信息]"
        print(f"{prefix} {result.note}")


def main() -> int:
    args = parse_args()
    results: list[CaseResult] = []

    try:
        normalize_args(args)
        iperf_bin = find_iperf_binary(args.iperf_bin)
        iperf_version = detect_iperf_version(iperf_bin)
        cases = resolve_cases("all" if args.all else args.cases)

        if not args.host_ip:
            args.host_ip = detect_host_ip(args.dut_ip)

        ensure_ok_response(send_control_command(args.dut_ip, args.control_port, "PING"), "PING")
        ensure_ok_response(send_control_command(args.dut_ip, args.control_port, "STATUS"), "STATUS")
        ensure_remote_idle(args)

        print("SoftAP iperf2 自动测试 / SoftAP iperf2 automation")
        if args.quick_check:
            print("Mode / 模式: QuickCheck")
        print(f"DUT control endpoint / 控制端点: {args.dut_ip}:{args.control_port}")
        print(f"Detected local host IP / 本机 IP: {args.host_ip}")
        print(f"Using local iperf binary / 本地 iperf: {iperf_bin}")
        print(f"iperf version / 版本: {iperf_version}")
        print(f"Duration / 时长: {args.time}s")
        print(f"Interval / 间隔: {args.interval}s")
        print(f"UDP bandwidth / UDP 带宽: {args.udp_bandwidth} Mbps")
        print(f"Continue on error / 失败后继续: {'on' if args.continue_on_error else 'off'}")

        for index, case_name in enumerate(cases):
            if index > 0 and args.inter_case_cooldown > 0:
                print(f"\n[信息] 冷却 {args.inter_case_cooldown}s 后进入下一个用例 ...")
                time.sleep(args.inter_case_cooldown)
                ensure_remote_idle(args)

            print(f"\n=== Running {case_name} / 执行 {case_name} ===")
            try:
                if case_name == "tcp-rx":
                    result = run_rx_case(args, iperf_bin, is_udp=False)
                elif case_name == "tcp-tx":
                    result = run_tx_case(args, iperf_bin, is_udp=False, host_ip=args.host_ip)
                elif case_name == "udp-rx":
                    result = run_rx_case(args, iperf_bin, is_udp=True)
                else:
                    result = run_tx_case(args, iperf_bin, is_udp=True, host_ip=args.host_ip)
            except (ScriptError, subprocess.TimeoutExpired) as error:
                result = CaseResult(
                    case=case_name,
                    role=case_role(case_name),
                    status="FAIL",
                    throughput_mbps=None,
                    actual_sec=None,
                    expected_sec=args.time,
                    note=str(error),
                    command="",
                    output=str(error),
                )

            results.append(result)
            print_case_result(result)

            if result.status == "FAIL" and not args.continue_on_error:
                break

        print_summary(results)

        if args.result_json:
            output_path = Path(args.result_json)
            output_path.write_text(json.dumps([asdict(item) for item in results], indent=2), encoding="utf-8")
            print(f"\nWrote JSON results to {output_path}")

        failed_results = [item for item in results if item.status == "FAIL"]
        warn_results = [item for item in results if item.status == "WARN"]
        if warn_results:
            print(f"\n[警告] {len(warn_results)} 个用例运行时长不足，带宽仅反映部分传输。")
        if failed_results:
            failed_case_names = ", ".join(item.case for item in failed_results)
            print(f"\nFAILED cases / 失败用例: {failed_case_names}", file=sys.stderr)
            return 1

        return 0
    except ScriptError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())