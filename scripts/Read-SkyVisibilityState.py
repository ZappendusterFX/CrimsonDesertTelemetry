"""Read-only snapshot of the camera sky-visibility acquisition state.

Exact-build diagnostic for the published 2.2.3 ASI only. Reads, never writes:
the spatial_acquire.cpp statics (Start/Dispatch/OnSubmission/Poll), the render
submission hook that feeds OnSubmission, the game's dispatch hook bytes, and the
visibility block of the sky bridge mapping. Takes a bounded series of snapshots so
progression (or a stuck latch) is visible. No game calls, no hooks, no writes.

The static addresses were read from the disassembly of the exact 2.2.3 ASI
(SHA-256 below); the script refuses any other ASI. A refused or failed read is
NOT evidence that a stage is absent.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import datetime
import hashlib
import json
import math
import os
import struct
import sys
import time

ASI_SHA256 = "D53DD63956251E444C92F6F99FA8C65A373F7E7015E4285BA5A30971CA590179"
ASI_NAME = "crimsondeserttelemetry.asi"
GAME_NAME = "crimsondesert.exe"
DISPATCH_RVA = 0x389C000  # game, build 25477059
# ASI RVAs (image base 0x180000000), from dumpbin /disasm of the exact 2.2.3 ASI.
RVA = {
    "readbackBuffer": 0x1B33D0, "readbackFence": 0x1B33D8, "originalDispatch": 0x1B33E0,
    "dispatchTarget": 0x1B33E8, "gameBase": 0x1B33F0, "sampleAmbient": 0x1B33F8,
    "requestedSources": 0x1B33F9, "sampleSources": 0x1B33FA, "acquisitionInFlight": 0x1B33FB,
    "latestFrame": 0x1B33FC, "fenceValue": 0x1B3400, "activeRowPitch": 0x1B3408,
    "activeList": 0x1B3410, "activeGi": 0x1B3420,
    "submissionObserver": 0x173378, "renderPhase": 0x15D070, "renderExecuteEnabled": 0x172689,
    "renderExecuteTarget": 0x1732F8, "renderPendingList": 0x1727D8,
}
ON_SUBMISSION_RVA = 0x64480
# Code bytes that must match the file in memory: spatial::Start's global stores.
CODE_GUARD_RVA, CODE_GUARD_BYTES = 0x64B46, 0x60
RENDER_PHASES = ["Discover", "Found", "Preparing", "Ready", "Recorded", "Submitting",
                 "WaitingGpu", "Failed", "Stopped"]
VISIBILITY_STATES = {0: "Unavailable", 1: "Valid", 2: "Fallback"}

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_VM_READ, FILE_MAP_READ = 0x1000, 0x10, 0x4
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.OpenFileMappingW.restype = wt.HANDLE
k32.OpenFileMappingW.argtypes = [wt.DWORD, wt.BOOL, wt.LPCWSTR]
k32.MapViewOfFile.restype = ctypes.c_void_p
k32.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
k32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.GetTickCount64.restype = ctypes.c_uint64
psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD,
                                       ctypes.POINTER(wt.DWORD), wt.DWORD]
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]


class ModuleInfo(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("size", wt.DWORD), ("entry", ctypes.c_void_p)]


psapi.GetModuleInformation.argtypes = [wt.HANDLE, wt.HMODULE, ctypes.POINTER(ModuleInfo), wt.DWORD]


def find_pid():
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq CrimsonDesert.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = [int(line.split('","')[1]) for line in out.splitlines() if line.startswith('"CrimsonDesert.exe"')]
    if len(pids) != 1:
        raise SystemExit(f"expected one CrimsonDesert.exe, found {pids}")
    return pids[0]


def modules(process):
    needed = wt.DWORD()
    array = (wt.HMODULE * 1024)()
    if not psapi.EnumProcessModulesEx(process, array, ctypes.sizeof(array), ctypes.byref(needed), 3):
        raise SystemExit(f"EnumProcessModulesEx failed: {ctypes.get_last_error()}")
    result = []
    for handle in array[: needed.value // ctypes.sizeof(wt.HMODULE)]:
        name = ctypes.create_unicode_buffer(1024)
        psapi.GetModuleFileNameExW(process, handle, name, 1024)
        info = ModuleInfo()
        psapi.GetModuleInformation(process, handle, ctypes.byref(info), ctypes.sizeof(info))
        result.append((name.value, info.base or 0, info.size))
    return result


def read(process, address, size):
    buffer = ctypes.create_string_buffer(size)
    done = ctypes.c_size_t()
    if not address or not k32.ReadProcessMemory(process, ctypes.c_void_p(address), buffer, size, ctypes.byref(done)) \
            or done.value != size:
        return None
    return buffer.raw


def file_bytes_at_rva(path, rva, size):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optional
    for index in range(sections):
        _, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, table + index * 40)
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return data[rawptr + rva - vaddr: rawptr + rva - vaddr + size]
    return None


def module_of(address, loaded):
    for name, base, size in loaded:
        if base <= address < base + size:
            return f"{os.path.basename(name)}+0x{address - base:X}"
    return None


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def decode_reference(constants):
    """Status-only port of spatial_sample.cpp DecodeReference."""
    lane = lambda offset, index: struct.unpack_from("<f", constants, offset + index * 4)[0]
    inverse = [lane(0x10, a) for a in range(3)]
    wrapped = [lane(0x130, a) for a in range(3)]
    uv = [lane(0x2E0, a) for a in range(3)]
    for axis in range(3):
        if not (0.0 < inverse[axis] <= 1.0) or not math.isfinite(wrapped[axis]) or \
                not math.isfinite(uv[axis]) or abs(uv[axis]) >= 1e8:
            return {"status": "InvalidConstants", "inverse": inverse, "uv": uv}
    clipmap, radius = -1, (63, 31, 63)
    for level in range(1, 8):
        if clipmap >= 0:
            break
        scale = lane(0x140 + level * 16, 3)
        if not (0.0 < scale <= 1e4):
            return {"status": "InvalidClipmap", "level": level, "scale": scale}
        inside = True
        for axis in range(3):
            if not inside:
                break
            origin = lane(0x140 + level * 16, axis)
            relative = lane(0x240 + level * 16, axis)
            if not math.isfinite(origin) or not math.isfinite(relative):
                return {"status": "InvalidConstants", "level": level}
            low, high = int(f32(origin - radius[axis])), int(f32(origin + radius[axis]))
            cell = math.floor(f32(f32(wrapped[axis] * scale) + relative))
            inside = low <= cell < high
        if inside:
            clipmap = level
    world = [uv[a] / inverse[a] for a in range(3)]
    status = "Ok" if 0 <= clipmap <= 3 else "Fallback"
    return {"status": status, "clipmap": clipmap, "world": world}


def read_sky(pid):
    handle = k32.OpenFileMappingW(FILE_MAP_READ, False, f"Local\\CrimsonDesertTelemetry.Sky.{pid}")
    if not handle:
        return {"error": f"OpenFileMapping failed ({ctypes.get_last_error()})"}
    try:
        view = k32.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 128)
        if not view:
            return {"error": f"MapViewOfFile failed ({ctypes.get_last_error()})"}
        try:
            for _ in range(64):
                before = ctypes.string_at(view + 16, 8)
                header = ctypes.string_at(view, 128)
                after = ctypes.string_at(view + 16, 8)
                if before == after and struct.unpack("<q", before)[0] % 2 == 0:
                    break
            else:
                return {"error": "seqlock did not settle"}
        finally:
            k32.UnmapViewOfFile(view)
    finally:
        k32.CloseHandle(handle)
    magic, version = struct.unpack_from("<II", header, 0)
    state = struct.unpack_from("<I", header, 28)[0]
    sample_sequence, captured, published = struct.unpack_from("<QQQ", header, 40)
    frame = struct.unpack_from("<I", header, 64)[0]
    value, vis_state, vis_frame, vis_tick = struct.unpack_from("<dIIQ", header, 96)
    return {"magic": f"0x{magic:08X}", "version": version, "status": state, "skySampleSequence": sample_sequence,
            "skyFrame": frame, "skyPublishedTickMs": published, "visibilityValue": value,
            "visibilityState": VISIBILITY_STATES.get(vis_state, vis_state), "visibilityStateRaw": vis_state,
            "visibilityFrame": vis_frame, "visibilityTickMs": vis_tick}


def snapshot(process, pid, asi, game, loaded):
    raw = {}
    for name, rva in RVA.items():
        size = 768 if name == "activeGi" else 1 if name in (
            "sampleAmbient", "requestedSources", "sampleSources", "acquisitionInFlight", "renderExecuteEnabled") \
            else 4 if name in ("latestFrame", "renderPhase") else 8
        raw[name] = read(process, asi + rva, size)
    missing = [name for name, value in raw.items() if value is None]
    value = lambda name, fmt: struct.unpack(fmt, raw[name])[0] if raw[name] is not None else None
    result = {"tickMs": k32.GetTickCount64(), "unreadable": missing}
    for name in ("readbackBuffer", "readbackFence", "originalDispatch", "dispatchTarget", "gameBase",
                 "activeList", "submissionObserver", "renderExecuteTarget", "renderPendingList"):
        v = value(name, "<Q")
        result[name] = None if v is None else f"0x{v:X}"
    for name in ("sampleAmbient", "requestedSources", "sampleSources", "acquisitionInFlight", "renderExecuteEnabled"):
        result[name] = value(name, "<B")
    result["latestFrame"] = value("latestFrame", "<I")
    result["fenceValue"] = value("fenceValue", "<Q")
    result["activeRowPitch"] = value("activeRowPitch", "<Q")
    phase = value("renderPhase", "<I")
    result["renderPhase"] = RENDER_PHASES[phase] if phase is not None and phase < len(RENDER_PHASES) else phase
    observer = value("submissionObserver", "<Q")
    result["observerIsSpatialOnSubmission"] = observer == asi + ON_SUBMISSION_RVA if observer is not None else None
    hook = read(process, game + DISPATCH_RVA, 5)
    result["gameDispatchFirstBytes"] = hook.hex(" ") if hook else None
    target = value("dispatchTarget", "<Q")
    result["dispatchTargetMatchesGame"] = target == game + DISPATCH_RVA if target is not None else None
    active = value("activeList", "<Q")
    if active:
        vtable = read(process, active, 8)
        vtable = struct.unpack("<Q", vtable)[0] if vtable else None
        result["activeListVtable"] = None if vtable is None else f"0x{vtable:X}"
        result["activeListVtableModule"] = module_of(vtable, loaded) if vtable else None
    if raw["activeGi"] is not None and any(raw["activeGi"]):
        result["activeGiDecode"] = decode_reference(raw["activeGi"])
    result["sky"] = read_sky(pid)
    return result


def classify(series):
    first, last = series[0], series[-1]
    if not last["sampleAmbient"] or not last["dispatchTarget"] or last["dispatchTarget"] == "0x0":
        return "spatial-start-not-armed"
    if not last["observerIsSpatialOnSubmission"]:
        return "submission-observer-not-spatial"
    progressed = last["fenceValue"] != first["fenceValue"] or last["latestFrame"] != first["latestFrame"]
    if progressed:
        return "acquisition-cycling (state=%s)" % last["sky"].get("visibilityState")
    if not last["acquisitionInFlight"] and not last["fenceValue"] and last["readbackBuffer"] in (None, "0x0"):
        return "no-acquisition-ever-recorded (Dispatch never passed Resolve with sampleAmbient)"
    if last["acquisitionInFlight"] and last["activeList"] not in (None, "0x0") and not last["renderExecuteEnabled"]:
        # OnSubmission is only called from the render ExecuteHook, installed after
        # the playable-world gate; until then this list cannot be observed.
        return "recorded-before-submission-hook (latch precondition; clears only if this list is resubmitted later)"
    if last["acquisitionInFlight"] and last["activeList"] not in (None, "0x0") and \
            all(s["activeList"] == last["activeList"] for s in series):
        return "latched-awaiting-submission (activeList never observed in ExecuteCommandLists)"
    if last["acquisitionInFlight"] and last["activeList"] in (None, "0x0"):
        return "submitted-awaiting-fence-or-poll (fence completion not readable remotely)"
    return "not-progressing-unclassified"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pid", type=int)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--interval-ms", type=int, default=500)
    parser.add_argument("--out")
    args = parser.parse_args()
    if not 1 <= args.samples <= 20 or not 100 <= args.interval_ms <= 5000:
        raise SystemExit("samples 1-20, interval 100-5000 ms")
    if args.out and os.path.exists(args.out):
        raise SystemExit(f"refusing to overwrite {args.out}")
    pid = args.pid or find_pid()
    process = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, pid)
    if not process:
        raise SystemExit(f"OpenProcess({pid}) failed: {ctypes.get_last_error()}")
    try:
        loaded = modules(process)
        asi = [m for m in loaded if os.path.basename(m[0]).lower() == ASI_NAME]
        game = [m for m in loaded if os.path.basename(m[0]).lower() == GAME_NAME]
        if len(asi) != 1 or len(game) != 1:
            raise SystemExit(f"module lookup failed: asi={asi} game={game}")
        asi_path, asi_base, _ = asi[0]
        digest = hashlib.sha256(open(asi_path, "rb").read()).hexdigest().upper()
        if digest != ASI_SHA256:
            raise SystemExit(f"refused: loaded ASI {asi_path} is {digest}, reader is only valid for {ASI_SHA256}")
        memory = read(process, asi_base + CODE_GUARD_RVA, CODE_GUARD_BYTES)
        if memory is None or memory != file_bytes_at_rva(asi_path, CODE_GUARD_RVA, CODE_GUARD_BYTES):
            raise SystemExit("refused: in-memory spatial::Start code does not match the ASI file")
        series = []
        for index in range(args.samples):
            if index:
                time.sleep(args.interval_ms / 1000)
            series.append(snapshot(process, pid, asi_base, game[0][1], loaded))
    finally:
        k32.CloseHandle(process)
    report = {"createdUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "pid": pid,
              "asiPath": asi_path, "asiSha256": digest, "asiBase": f"0x{asi_base:X}",
              "gameBase": f"0x{game[0][1]:X}", "classification": classify(series), "snapshots": series}
    text = json.dumps(report, indent=2)
    if args.out:
        with open(args.out, "x", encoding="utf-8") as handle:
            handle.write(text)
    print(text)


if __name__ == "__main__":
    main()
