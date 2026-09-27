#pragma once

#include <d3d12.h>

namespace cdt::render::device_identity
{
// ReShade 6.8 exposes the native D3D12 device behind its proxy through this
// optional interface. Native devices return E_NOINTERFACE and retain their
// ordinary COM identity. A different native device must never compare equal.
inline constexpr GUID ReShadeUnwrappedObject =
    {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};

inline HRESULT CanonicalDeviceIdentity(IUnknown* device, IUnknown** identity)
{
    if (!identity) return E_POINTER;
    *identity = nullptr;
    if (!device) return E_POINTER;

    IUnknown* unwrapped{};
    const HRESULT unwrap = device->QueryInterface(ReShadeUnwrappedObject,
        reinterpret_cast<void**>(&unwrapped));
    if (SUCCEEDED(unwrap))
    {
        if (!unwrapped) return E_UNEXPECTED;
        ID3D12Device* nativeDevice{};
        const HRESULT typed = unwrapped->QueryInterface(IID_PPV_ARGS(&nativeDevice));
        unwrapped->Release();
        if (FAILED(typed)) return typed;
        if (!nativeDevice) return E_UNEXPECTED;
        const HRESULT result = nativeDevice->QueryInterface(IID_PPV_ARGS(identity));
        nativeDevice->Release();
        return result;
    }
    if (unwrap != E_NOINTERFACE) return unwrap;
    return device->QueryInterface(IID_PPV_ARGS(identity));
}

template<class T>
HRESULT ChildDeviceIdentity(T* child, IUnknown** identity)
{
    if (!identity) return E_POINTER;
    *identity = nullptr;
    if (!child) return E_POINTER;
    ID3D12Device* device{};
    const HRESULT query = child->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(query)) return query;
    if (!device) return E_UNEXPECTED;
    const HRESULT result = CanonicalDeviceIdentity(device, identity);
    device->Release();
    return result;
}
}
