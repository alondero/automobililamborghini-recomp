// MinGW/GCC COM-GUID compatibility for the native Windows build (#68).
//
// Force-included (via -include) into every C++ TU of the D3D12-touching targets
// (plume, rt64, lamborghini_modern) when compiling with MinGW GCC. Why:
//
//  * MSVC attaches IIDs to interfaces at declaration time (__declspec(uuid(...)))
//    and __uuidof() reads them back. GCC has no such intrinsic; MinGW emulates it
//    with template specialisations of __mingw_uuidof<T>() emitted by the
//    __CRT_UUID_DECL(...) macro.
//  * We compile against Microsoft's DirectX-Headers d3d12.h (MinGW's bundled one
//    is years stale - no D3D12_HEAP_TYPE_GPU_UPLOAD, no ID3D12Device12), but that
//    header carries NO __CRT_UUID_DECLs at all: on non-MSVC the GUIDs live in the
//    companion <dxguids/dxguids.h> (WINADAPTER_IID -> __CRT_UUID_DECL). Any TU
//    that calls IID_PPV_ARGS/__uuidof without including dxguids.h compiles fine
//    but leaves an undefined __mingw_uuidof<T> reference at link time - which is
//    exactly what plume_d3d12.cpp and rt64's DXC users did.
//
// Including <directx/d3d12.h> first also wins the include-guard race (__d3d12_h__
// is shared with MinGW's d3d12.h), so every TU sees the modern header.
#pragma once

#if defined(__cplusplus) && defined(_WIN32) && !defined(_MSC_VER)

#include <directx/d3d12.h>
#include <dxguids/dxguids.h>

// Gap in DirectX-Headers v1.614.1 (the tag we pin): d3d12.h declares
// ID3D12Device12 but dxguids.h has no WINADAPTER_IID entry for it, and
// D3D12MemAlloc references it via IID_PPV_ARGS. GUID from the MIDL_INTERFACE
// line in that same d3d12.h. Drop this if the pin moves to a tag whose
// dxguids.h covers it (a duplicate __CRT_UUID_DECL is a redefinition error).
__CRT_UUID_DECL(ID3D12Device12, 0x5af5c532, 0x4c91, 0x4cd0, 0xb5, 0x41, 0x15, 0xa4, 0x05, 0x39, 0x5f, 0xc5)

// rt64's bundled dxcapi.h declares its interfaces via CROSS_PLATFORM_UUIDOF,
// whose default _WIN32 form is the MSVC-only `struct __declspec(uuid(spec))` -
// GCC ignores the uuid attribute and rt64's DXC call sites then leave undefined
// __mingw_uuidof<IDxc*> references. Pre-defining the macro here (before any
// dxcapi.h is seen) makes both dxcapi.h copies (rt64's bundled one and MinGW's
// system one - both guard with #ifndef CROSS_PLATFORM_UUIDOF) use a
// GUID-emitting form instead. Mechanism mirrors MinGW's own dxcapi.h: constexpr
// helpers parse the "xxxxxxxx-xxxx-..." spec string into __CRT_UUID_DECL args.
#ifndef CROSS_PLATFORM_UUIDOF
namespace lambo_uuid_compat {
constexpr unsigned char nyb(char c) {
    return (c >= '0' && c <= '9') ? (c - '0')
         : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
         : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
         : /* invalid hex digit */ 0xFF;
}
constexpr unsigned char byte_at(const char* s) { return (unsigned char)(nyb(s[0]) << 4 | nyb(s[1])); }
constexpr unsigned short short_at(const char* s, unsigned shift) { return (unsigned short)(byte_at(s) << shift); }
constexpr unsigned long word_at(const char* s, unsigned shift) { return (unsigned long)byte_at(s) << shift; }
}
#define CROSS_PLATFORM_UUIDOF(iface, spec)                                                    \
    struct iface;                                                                             \
    __CRT_UUID_DECL(iface,                                                                    \
        lambo_uuid_compat::word_at(spec, 24) | lambo_uuid_compat::word_at(spec + 2, 16) |     \
            lambo_uuid_compat::word_at(spec + 4, 8) | lambo_uuid_compat::word_at(spec + 6, 0),\
        lambo_uuid_compat::short_at(spec + 9, 8) | lambo_uuid_compat::short_at(spec + 11, 0), \
        lambo_uuid_compat::short_at(spec + 14, 8) | lambo_uuid_compat::short_at(spec + 16, 0),\
        lambo_uuid_compat::byte_at(spec + 19), lambo_uuid_compat::byte_at(spec + 21),         \
        lambo_uuid_compat::byte_at(spec + 24), lambo_uuid_compat::byte_at(spec + 26),         \
        lambo_uuid_compat::byte_at(spec + 28), lambo_uuid_compat::byte_at(spec + 30),         \
        lambo_uuid_compat::byte_at(spec + 32), lambo_uuid_compat::byte_at(spec + 34))
#endif

#endif
