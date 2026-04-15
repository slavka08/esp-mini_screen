#!/usr/bin/env python3

import argparse
import ctypes
import datetime as dt
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import List, Optional, Sequence, Tuple


LIBC = ctypes.CDLL("/usr/lib/libSystem.B.dylib")

CPU_STATE_MAX = 4
CPU_STATE_USER = 0
CPU_STATE_SYSTEM = 1
CPU_STATE_IDLE = 2
CPU_STATE_NICE = 3
PROCESSOR_CPU_LOAD_INFO = 2
HOST_VM_INFO64 = 4

natural_t = ctypes.c_uint
integer_t = ctypes.c_int
mach_msg_type_number_t = ctypes.c_uint
host_t = ctypes.c_uint
processor_flavor_t = ctypes.c_int
processor_info_array_t = ctypes.POINTER(integer_t)
vm_map_t = ctypes.c_uint
vm_address_t = ctypes.c_uint64
vm_size_t = ctypes.c_uint64
kern_return_t = ctypes.c_int


class vm_statistics64_data_t(ctypes.Structure):
    _fields_ = [
        ("free_count", ctypes.c_uint32),
        ("active_count", ctypes.c_uint32),
        ("inactive_count", ctypes.c_uint32),
        ("wire_count", ctypes.c_uint32),
        ("zero_fill_count", ctypes.c_uint64),
        ("reactivations", ctypes.c_uint64),
        ("pageins", ctypes.c_uint64),
        ("pageouts", ctypes.c_uint64),
        ("faults", ctypes.c_uint64),
        ("cow_faults", ctypes.c_uint64),
        ("lookups", ctypes.c_uint64),
        ("hits", ctypes.c_uint64),
        ("purgeable_count", ctypes.c_uint32),
        ("purges", ctypes.c_uint64),
        ("speculative_count", ctypes.c_uint32),
        ("decompressions", ctypes.c_uint64),
        ("compressions", ctypes.c_uint64),
        ("swapins", ctypes.c_uint64),
        ("swapouts", ctypes.c_uint64),
        ("compressor_page_count", ctypes.c_uint32),
        ("throttled_count", ctypes.c_uint32),
        ("external_page_count", ctypes.c_uint32),
        ("internal_page_count", ctypes.c_uint32),
        ("total_uncompressed_pages_in_compressor", ctypes.c_uint64),
    ]


LIBC.mach_host_self.restype = host_t
LIBC.host_processor_info.argtypes = [
    host_t,
    processor_flavor_t,
    ctypes.POINTER(natural_t),
    ctypes.POINTER(processor_info_array_t),
    ctypes.POINTER(mach_msg_type_number_t),
]
LIBC.host_processor_info.restype = kern_return_t
LIBC.vm_deallocate.argtypes = [vm_map_t, vm_address_t, vm_size_t]
LIBC.vm_deallocate.restype = kern_return_t
LIBC.host_statistics64.argtypes = [
    host_t,
    ctypes.c_int,
    ctypes.POINTER(integer_t),
    ctypes.POINTER(mach_msg_type_number_t),
]
LIBC.host_statistics64.restype = kern_return_t
LIBC.host_page_size.argtypes = [host_t, ctypes.POINTER(ctypes.c_uint)]
LIBC.host_page_size.restype = kern_return_t
LIBC.sysctlbyname.argtypes = [
    ctypes.c_char_p,
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.c_void_p,
    ctypes.c_size_t,
]
LIBC.sysctlbyname.restype = ctypes.c_int

MACH_TASK_SELF = ctypes.c_uint.in_dll(LIBC, "mach_task_self_").value


def sysctl_int(name: str) -> int:
    value = ctypes.c_uint64()
    size = ctypes.c_size_t(ctypes.sizeof(value))
    rc = LIBC.sysctlbyname(name.encode(), ctypes.byref(value), ctypes.byref(size), None, 0)
    if rc != 0:
        raise OSError(f"sysctlbyname failed for {name}")
    return int(value.value)


def sysctl_string(name: str) -> str:
    size = ctypes.c_size_t(0)
    rc = LIBC.sysctlbyname(name.encode(), None, ctypes.byref(size), None, 0)
    if rc != 0:
        raise OSError(f"sysctlbyname failed for {name}")
    buf = ctypes.create_string_buffer(size.value)
    rc = LIBC.sysctlbyname(name.encode(), buf, ctypes.byref(size), None, 0)
    if rc != 0:
        raise OSError(f"sysctlbyname failed for {name}")
    return buf.value.decode("utf-8", errors="replace")


def format_gib(value: float) -> str:
    return f"{value / (1024 ** 3):.1f}"


class MacSampler:
    def __init__(self) -> None:
        self.host = LIBC.mach_host_self()
        self.page_size = self._read_page_size()
        self.total_memory = sysctl_int("hw.memsize")
        self.brand = sysctl_string("machdep.cpu.brand_string")
        self.gpu_label = "GPU"
        self.gpu_supported = True

    def _read_page_size(self) -> int:
        page_size = ctypes.c_uint()
        rc = LIBC.host_page_size(self.host, ctypes.byref(page_size))
        if rc != 0:
            raise RuntimeError(f"host_page_size failed: {rc}")
        return int(page_size.value)

    def read_cpu_ticks(self) -> List[Tuple[int, int, int, int]]:
        cpu_count = natural_t()
        info = processor_info_array_t()
        info_count = mach_msg_type_number_t()

        rc = LIBC.host_processor_info(
            self.host,
            PROCESSOR_CPU_LOAD_INFO,
            ctypes.byref(cpu_count),
            ctypes.byref(info),
            ctypes.byref(info_count),
        )
        if rc != 0:
            raise RuntimeError(f"host_processor_info failed: {rc}")

        try:
            raw = [info[i] for i in range(info_count.value)]
        finally:
            address = ctypes.cast(info, ctypes.c_void_p).value or 0
            size = info_count.value * ctypes.sizeof(integer_t)
            if address and size:
                LIBC.vm_deallocate(MACH_TASK_SELF, address, size)

        ticks: List[Tuple[int, int, int, int]] = []
        for offset in range(0, len(raw), CPU_STATE_MAX):
            ticks.append(
                (
                    raw[offset + CPU_STATE_USER],
                    raw[offset + CPU_STATE_SYSTEM],
                    raw[offset + CPU_STATE_IDLE],
                    raw[offset + CPU_STATE_NICE],
                )
            )
        return ticks

    def read_memory(self) -> Tuple[int, str]:
        stats = vm_statistics64_data_t()
        count = mach_msg_type_number_t(ctypes.sizeof(vm_statistics64_data_t) // ctypes.sizeof(integer_t))
        rc = LIBC.host_statistics64(
            self.host,
            HOST_VM_INFO64,
            ctypes.cast(ctypes.byref(stats), ctypes.POINTER(integer_t)),
            ctypes.byref(count),
        )
        if rc != 0:
            raise RuntimeError(f"host_statistics64 failed: {rc}")

        used_pages = stats.active_count + stats.wire_count + stats.compressor_page_count
        used_bytes = used_pages * self.page_size
        percent = round((used_bytes / self.total_memory) * 100)
        text = f"{format_gib(used_bytes)}/{format_gib(self.total_memory)} GB"
        return int(percent), text

    def detect_core_layout(self) -> Tuple[int, int]:
        try:
            levels = sysctl_int("hw.nperflevels")
        except OSError:
            return sysctl_int("hw.logicalcpu"), 0

        performance = 0
        efficiency = 0
        for index in range(levels):
            name = sysctl_string(f"hw.perflevel{index}.name").strip().lower()
            count = sysctl_int(f"hw.perflevel{index}.logicalcpu")
            if name.startswith("performance"):
                performance = count
            elif name.startswith("efficiency"):
                efficiency = count

        if performance == 0 and efficiency == 0:
            return sysctl_int("hw.logicalcpu"), 0
        return performance, efficiency

    def _read_gpu_ioreg(self) -> Optional[str]:
        try:
            result = subprocess.run(
                ["ioreg", "-r", "-d", "1", "-w", "0", "-c", "AGXAccelerator"],
                check=False,
                capture_output=True,
                text=True,
                timeout=0.75,
            )
        except (OSError, subprocess.SubprocessError):
            return None

        if result.returncode != 0 or not result.stdout:
            return None
        return result.stdout

    def _update_gpu_label(self, output: str) -> None:
        if self.gpu_label != "GPU":
            return

        model_match = re.search(r'"model"\s*=\s*"([^"]+)"', output)
        core_match = re.search(r'"gpu-core-count"\s*=\s*(\d+)', output)
        model = model_match.group(1).strip() if model_match else ""
        if model.startswith("Apple "):
            model = model[len("Apple ") :]

        if model and core_match:
            self.gpu_label = f"{model} {core_match.group(1)}c"
        elif model:
            self.gpu_label = model
        elif core_match:
            self.gpu_label = f"{core_match.group(1)}c GPU"

    def read_gpu(self) -> Optional[Tuple[int, str]]:
        if not self.gpu_supported:
            return None

        output = self._read_gpu_ioreg()
        if not output:
            self.gpu_supported = False
            return None

        self._update_gpu_label(output)
        match = re.search(r'"Device Utilization %"\s*=\s*(\d+)', output)
        if not match:
            return None

        percent = max(0, min(int(match.group(1)), 100))
        return percent, self.gpu_label


def compute_loads(previous: Sequence[Tuple[int, int, int, int]], current: Sequence[Tuple[int, int, int, int]]) -> List[int]:
    if len(previous) != len(current):
        raise ValueError("CPU sample size changed")

    loads: List[int] = []
    for prev, cur in zip(previous, current):
        user = max(cur[CPU_STATE_USER] - prev[CPU_STATE_USER], 0)
        system = max(cur[CPU_STATE_SYSTEM] - prev[CPU_STATE_SYSTEM], 0)
        idle = max(cur[CPU_STATE_IDLE] - prev[CPU_STATE_IDLE], 0)
        nice = max(cur[CPU_STATE_NICE] - prev[CPU_STATE_NICE], 0)
        busy = user + system + nice
        total = busy + idle
        percent = round((busy / total) * 100) if total else 0
        loads.append(int(percent))
    return loads


def split_cores(
    loads: Sequence[int],
    performance_count: int,
    efficiency_count: int,
    cpu_order: str,
) -> List[int]:
    total = performance_count + efficiency_count
    if total <= 0:
        return []
    if total > len(loads):
        raise ValueError(
            f"Requested {total} cores, but macOS reported only {len(loads)} logical CPUs"
        )

    if cpu_order == "perf-first":
        return list(loads[:total])
    if cpu_order == "eff-first":
        eff_slice = list(loads[:efficiency_count])
        perf_slice = list(loads[efficiency_count : efficiency_count + performance_count])
        return perf_slice + eff_slice
    raise ValueError(f"Unsupported cpu order: {cpu_order}")


def normalize_counts(
    sampler: MacSampler,
    args: argparse.Namespace,
) -> Tuple[int, int]:
    detected_perf, detected_eff = sampler.detect_core_layout()
    performance = args.performance_cores if args.performance_cores is not None else detected_perf
    efficiency = args.efficiency_cores if args.efficiency_cores is not None else detected_eff

    if efficiency < 0 or performance < 0:
        raise ValueError("Core counts must be non-negative")
    if performance + efficiency <= 0:
        raise ValueError("At least one core must be visible")

    if args.max_visible_cores is not None and performance + efficiency > args.max_visible_cores:
        overflow = performance + efficiency - args.max_visible_cores
        if efficiency >= overflow:
            efficiency -= overflow
        else:
            performance = max(performance - (overflow - efficiency), 0)
            efficiency = 0

    if performance + efficiency <= 0:
        raise ValueError("No visible cores remain after max-visible-cores limit")

    return performance, efficiency


def build_payload(
    loads: Sequence[int],
    performance_count: int,
    efficiency_count: int,
    memory_percent: int,
    memory_text: str,
    gpu_sample: Optional[Tuple[int, str]],
) -> dict:
    payload = {
        "updatedAt": dt.datetime.now().strftime("%H:%M:%S"),
        "memoryText": memory_text,
        "memoryPercent": str(memory_percent),
        "performanceCount": str(performance_count),
        "efficiencyCount": str(efficiency_count),
        "coreLoads": ",".join(str(value) for value in loads),
    }
    if gpu_sample is not None:
        gpu_percent, gpu_text = gpu_sample
        payload["gpuText"] = gpu_text
        payload["gpuPercent"] = str(gpu_percent)
    return payload


def post_payload(url: str, payload: dict, timeout: float) -> str:
    data = urllib.parse.urlencode(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8", errors="replace")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send per-core CPU and RAM stats from macOS to esp_mini_screen_mac_stats."
    )
    parser.add_argument(
        "--device-url",
        required=True,
        help="ESP endpoint base URL, for example http://192.168.1.50 or http://192.168.1.50/stats",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="Sampling/upload interval in seconds (default: 1.0)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=3.0,
        help="HTTP timeout in seconds (default: 3.0)",
    )
    parser.add_argument(
        "--performance-cores",
        type=int,
        help="Override visible performance core count",
    )
    parser.add_argument(
        "--efficiency-cores",
        type=int,
        help="Override visible efficiency core count",
    )
    parser.add_argument(
        "--cpu-order",
        choices=("perf-first", "eff-first"),
        default="perf-first",
        help="How macOS orders logical CPUs before re-grouping for the screen (default: perf-first)",
    )
    parser.add_argument(
        "--max-visible-cores",
        type=int,
        default=16,
        help="Hard cap for visible cores on the 240x240 display (default: 16)",
    )
    parser.add_argument(
        "--no-gpu",
        action="store_true",
        help="Disable GPU sampling via ioreg",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Collect one sample and exit",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print payloads instead of POSTing them",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print device responses for each upload",
    )
    return parser.parse_args()


def normalize_device_url(raw_url: str) -> str:
    url = raw_url.rstrip("/")
    if url.endswith("/stats"):
        return url
    return f"{url}/stats"


def main() -> int:
    args = parse_args()
    if sys.platform != "darwin":
        print("This sender only works on macOS.", file=sys.stderr)
        return 1

    try:
        sampler = MacSampler()
        performance_count, efficiency_count = normalize_counts(sampler, args)
    except Exception as exc:
        print(f"Initialization failed: {exc}", file=sys.stderr)
        return 1

    endpoint = normalize_device_url(args.device_url)
    previous_ticks = sampler.read_cpu_ticks()

    if args.verbose:
        visible = performance_count + efficiency_count
        print(
            f"Detected Mac: {sampler.brand} | visible cores: {visible} "
            f"(P={performance_count}, E={efficiency_count}) | endpoint: {endpoint}"
        )

    try:
        while True:
            try:
                time.sleep(args.interval)
                current_ticks = sampler.read_cpu_ticks()
                loads = compute_loads(previous_ticks, current_ticks)
                previous_ticks = current_ticks

                visible_loads = split_cores(loads, performance_count, efficiency_count, args.cpu_order)
                memory_percent, memory_text = sampler.read_memory()
                gpu_sample = None if args.no_gpu else sampler.read_gpu()
                payload = build_payload(
                    visible_loads,
                    performance_count,
                    efficiency_count,
                    memory_percent,
                    memory_text,
                    gpu_sample,
                )

                if args.dry_run:
                    print(payload)
                else:
                    response = post_payload(endpoint, payload, args.timeout)
                    if args.verbose:
                        print(response)

                if args.once:
                    break
            except urllib.error.URLError as exc:
                print(f"Upload failed: {exc}", file=sys.stderr)
                if args.once:
                    return 1
            except Exception as exc:
                print(f"Sender iteration failed: {exc}", file=sys.stderr)
                if args.once:
                    return 1
    except KeyboardInterrupt:
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
