#pragma once
#include <windows.h>
#include <cstdint>
namespace cdt::render
{
enum class PreflightFailure : uint32_t
{
    None, MissingImage, UnreadableImage, MalformedImage, SceneGlobalOutsideImage,
    HookOutsideExecutableSection, HookSignatureMismatch, ContextOutsideExecutableSection, ContextSignatureMismatch
};
struct PreflightResult
{
    PreflightFailure failure = PreflightFailure::None;
    uint32_t contextIndex = UINT32_MAX;
    explicit operator bool() const { return failure == PreflightFailure::None; }
};
// The exact same non-mutating preflight used by StartCapture before MinHook.
// Passing it does NOT grant compatibility: instruments::Run separately requires
// the generated exact EXE or the complete independently verified runtime contract.
PreflightResult CheckCapturePreflight(uint64_t moduleBase);
const char* PreflightFailureName(PreflightFailure failure);
bool StartCapture(uint64_t moduleBase, unsigned sampleRateHz, bool skyEnabled = false);
void PollCapture();
// True only after the managed host has validated a playable player/camera sample.
bool CaptureReady();
uint32_t CaptureFailureCode();
void StopCapture();
bool OwnsCodeAddress(uint64_t address);
}
