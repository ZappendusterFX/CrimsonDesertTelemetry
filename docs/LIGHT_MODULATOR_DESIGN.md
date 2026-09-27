# ManyLights GPU Modulator — Architecture & Design

**Branch:** `feature/lightshow-modulator`  
**Purpose:** Real-time per-light RGB and intensity modulation on the GPU for music lightshows and targeted light switching.

---

## 1. High-Level Flow

```
[ PC Audio (WASAPI Loopback) ]
             │
             ▼
[ Audio Analyzer (FFT) ]  -->  Bands: Sub-Bass, Bass, Mids, Highs, Beats
             │
             ▼ (Low-latency MMF / Shared Memory, 60+ Hz)
[ CDT Native ASI Modulator ]
             │
             ▼ (D3D12 Compute Shader Dispatch)
[ `g_manyLightsDataBuffer` (Resource 217 UAV) ]
             │
             ▼
[ Game Light Tree & Deferred Shading ]  -->  Ingame Visual Lightshow!
```

---

## 2. GPU Buffer Layout (`g_manyLightsDataBuffer`)

Written by `ProcessManyLightsCS` (Event 93), consumed by `BuildLightTreeLevel0CS` (Event 147).

* **Resource format:** Structured Buffer, stride 48 bytes, capacity 32,768 records.
* **Counter:** `g_structureCounterBufferUAV` (Resource 230).
  * `DWORD 0`: Input dispatch bound.
  * `DWORD 1`: Output valid light count (`validCount`, typically 20–150).
* **Record Structure (48 bytes):**
  ```cpp
  struct ManyLightRecord {
      float    relPos[3];      // +0: Camera-relative position (dx, dy, dz)
      float    pad0;           // +12
      float    rgb[3];         // +16: Linear RGB
      float    colorW;         // +28: Flags / particle indicator
      uint32_t packedDir0[2];  // +32
      uint32_t packedDir1[2];  // +40
  };
  static_assert(sizeof(ManyLightRecord) == 48);
  ```

---

## 3. Light Modulation Shader (`light_modulator.hlsl`)

```hlsl
struct ManyLightRecord {
    float3 relPos;
    float  pad0;
    float3 rgb;
    float  colorW;
    uint2  packedDir0;
    uint2  packedDir1;
};

RWStructuredBuffer<ManyLightRecord> g_lights  : register(u0);
ByteAddressBuffer                   g_counter : register(t0);

struct LightZone {
    float3 targetWorldPos;
    float  radius;
    float3 scaleRgb;
    float  pad;
};

cbuffer ModulatorCB : register(b0) {
    float3    cameraWorldPos;
    uint      zoneCount;
    LightZone zones[16];
};

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint totalLights = g_counter.Load(4); // DWORD 1 = valid count
    if (id.x >= totalLights) return;

    float3 worldPos = g_lights[id.x].relPos + cameraWorldPos;
    for (uint i = 0; i < zoneCount; ++i) {
        float dist = distance(worldPos, zones[i].targetWorldPos);
        if (dist <= zones[i].radius) {
            g_lights[id.x].rgb *= zones[i].scaleRgb;
        }
    }
}
```

---

## 4. D3D12 Root Signature & State Isolation

To prevent breaking subsequent engine passes (Event 94+):

1. **Root Signature:**
   * Parameter 0: CBV (b0) — `ModulatorCB`
   * Parameter 1: UAV (u0) — `g_lights` (Root UAV, no descriptor heap needed)
   * Parameter 2: SRV (t0) — `g_counter` (Root SRV, no descriptor heap needed)
2. **State Preservation:**
   * After the modulator dispatch, restore Root Signature `469` on the command list.
   * Subsequent engine commands immediately re-bind their own PSO and descriptor tables.
