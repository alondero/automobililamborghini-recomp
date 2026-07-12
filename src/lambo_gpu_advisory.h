// GPU driver advisory (issue #109): old Intel drivers trip RT64's
// force-Vulkan workaround, and on Iris Xe that Vulkan driver loses the device
// (VK_ERROR_DEVICE_LOST) -> silent black screen. Detect the condition after
// renderer setup and tell the user what to do about it.
//
// Pure logic (no RT64/plume types) so it is host-testable: see
// tests/test_gpu_advisory.cpp.
#ifndef LAMBO_GPU_ADVISORY_H
#define LAMBO_GPU_ADVISORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAMBO_GPU_VENDOR_INTEL 0x8086u

enum {
    LAMBO_GPU_ADVISORY_NONE = 0,
    LAMBO_GPU_ADVISORY_INFO = 1,   // affected driver, but not on the failing API path
    LAMBO_GPU_ADVISORY_SEVERE = 2, // affected driver on Vulkan: expect device loss / black screen
};

// Render a packed driver version (four 16-bit words, AA.BB.CCC.DDDD) as text.
void lambo_gpu_format_driver_version(uint64_t driver_version, char* out, size_t out_size);

// Mirrors RT64's BrokenIntelDriverD3D12 gate (rt64_application.cpp): drivers at
// or below 31.0.101.2115 are forced onto Vulkan when the API choice is automatic.
int lambo_gpu_intel_driver_predates_fix(uint64_t driver_version);

// True for Gen12+ Intel iGPUs/dGPUs (Iris Xe, Arc), identified by the "Xe"/"Arc"
// substring in the device name. These have no D3D12 device-removal bug, so RT64's
// old-driver force-Vulkan (written for 6th-gen HD Graphics that DO device-remove on
// D3D12) is wrong for them: it pushes them onto the Vulkan path that device-LOSES
// (issue #109). patches/0010 uses the same substring test to keep them on D3D12.
// NULL/empty name -> 0 (unknown, keep RT64's conservative fallback).
int lambo_gpu_name_is_modern_intel(const char* device_name);

// Classify the post-setup device state. running_vulkan reflects the API that was
// actually chosen (after any RT64 fallback), not what the config asked for.
// device_name distinguishes a modern Intel iGPU wrongly on Vulkan (SEVERE: the #109
// device-loss black screen) from a 6th-gen part correctly on Vulkan (INFO: working,
// driver merely old). NULL name is treated as not-modern.
int lambo_gpu_advisory_severity(uint32_t vendor, uint64_t driver_version,
                                int running_vulkan, const char* device_name);

// Build the user-facing advisory text for a non-NONE severity. config_path is the
// absolute graphics.json path shown to the user for the api_option escape hatch.
void lambo_gpu_advisory_message(int severity, const char* device_name,
                                uint64_t driver_version, const char* config_path,
                                char* out, size_t out_size);

// One-slot cross-thread handoff for the popup: the gfx thread (renderer setup)
// posts the advisory text; the main thread's event pump takes it exactly once
// and owns showing the message box (UI must live on the window-owning thread --
// a detached MessageBox thread races process teardown into heap corruption).
void lambo_gpu_advisory_set_pending(const char* text);
const char* lambo_gpu_advisory_take_pending(void); // NULL when nothing pending

#ifdef __cplusplus
}
#endif

#endif
