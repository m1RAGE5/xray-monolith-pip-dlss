#pragma once
#include "DlssD3D11.h"
#include <d3d11_1.h>
#include <wrl/client.h>

struct _xess_context_handle_t;

namespace xess
{
// Intel XeSS-SR (XeSS 3 SDK) via libxess_dx11.dll, loaded at runtime so a
// missing DLL only disables XeSS. The D3D11 backend is Intel-GPU only; other
// vendors fail cleanly at context creation and fall back to DLSS/FSR3.
class D3D11Backend
{
public:
    using Log = dlss::D3D11Backend::Log;
    using Frame = dlss::D3D11Backend::Frame;
    ~D3D11Backend() { Destroy(); }
    D3D11Backend() = default;
    D3D11Backend(const D3D11Backend&) = delete;
    D3D11Backend& operator=(const D3D11Backend&) = delete;

    // Documented XeSS scale ratios: native_aa 1.0x, quality 1.5x, balanced 1.7x,
    // performance 2.0x, ultra_performance 3.0x.
    static bool OptimalSize(unsigned quality, dlss::Size display, dlss::Size& render);
    bool Create(ID3D11Device* device, unsigned quality, dlss::Size render, dlss::Size display, bool hdr, Log log);
    void Destroy();
    void Shutdown();
    bool Evaluate(const Frame& frame);
    void ResetHistory() { m_history.Invalidate(); }
    bool Ready() const { return m_context != nullptr && m_initialized && !m_failed; }
    ID3D11Texture2D* Output() const { return m_output.Get(); }

private:
    bool Check(int result, const char* stage);
    bool Validate(const Frame& frame) const;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_context;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> m_state;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_output;
    Microsoft::WRL::ComPtr<ID3D11Device> m_outputDevice;
    _xess_context_handle_t* m_contextHandle = nullptr;
    dlss::Size m_render, m_display;
    dlss::History m_history;
    Log m_log = nullptr;
    bool m_initialized = false, m_failed = false;
};
}
