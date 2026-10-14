#include "XessD3D11.h"
#include "../../../../sdk/XeSS/include/xess.h"
#include "../../../../sdk/XeSS/include/xess_d3d11.h"
#include "UpscalerDiagnostics.h"
#include <cstdio>

namespace xess
{
using Microsoft::WRL::ComPtr;

namespace
{
// Runtime loading keeps libxess_dx11.dll optional: without it the game boots
// and only XeSS is unavailable.
struct Api
{
    HMODULE library = nullptr;
    xess_result_t (*D3D11CreateContext)(ID3D11Device*, xess_context_handle_t*) = nullptr;
    xess_result_t (*D3D11Init)(xess_context_handle_t, const xess_d3d11_init_params_t*) = nullptr;
    xess_result_t (*D3D11Execute)(xess_context_handle_t, const xess_d3d11_execute_params_t*) = nullptr;
    xess_result_t (*DestroyContext)(xess_context_handle_t) = nullptr;
    xess_result_t (*GetVersion)(xess_version_t*) = nullptr;
    xess_result_t (*SetLoggingCallback)(xess_context_handle_t, xess_logging_level_t, xess_app_log_callback_t) = nullptr;
    xess_result_t (*IsOptimalDriver)(xess_context_handle_t) = nullptr;
    xess_result_t (*SetVelocityScale)(xess_context_handle_t, float, float) = nullptr;

    dlss::D3D11Backend::Log sink = nullptr; // receives SDK warnings between Create and Destroy

    bool Load(dlss::D3D11Backend::Log log)
    {
        if (library) return D3D11CreateContext != nullptr;
        library = ::LoadLibraryW(L"libxess_dx11.dll");
        if (!library)
        {
            if (log) log("XeSS runtime is missing: place libxess_dx11.dll and libxess.dll beside the executable", unsigned(E_FAIL));
            return false;
        }
        struct Entry { void** target; const char* name; };
        const Entry entries[] = {
            {reinterpret_cast<void**>(&D3D11CreateContext), "xessD3D11CreateContext"},
            {reinterpret_cast<void**>(&D3D11Init), "xessD3D11Init"},
            {reinterpret_cast<void**>(&D3D11Execute), "xessD3D11Execute"},
            {reinterpret_cast<void**>(&DestroyContext), "xessDestroyContext"},
            {reinterpret_cast<void**>(&GetVersion), "xessGetVersion"},
            {reinterpret_cast<void**>(&SetLoggingCallback), "xessSetLoggingCallback"},
            {reinterpret_cast<void**>(&IsOptimalDriver), "xessIsOptimalDriver"},
            {reinterpret_cast<void**>(&SetVelocityScale), "xessSetVelocityScale"},
        };
        for (const auto& entry : entries)
            *entry.target = reinterpret_cast<void*&>(::GetProcAddress(library, entry.name));
        if (!D3D11CreateContext || !D3D11Init || !D3D11Execute || !DestroyContext)
        {
            if (log) log("libxess_dx11.dll does not export the expected XeSS entry points", unsigned(E_FAIL));
            return false;
        }
        return true;
    }
};
Api api;

const char* ResultText(xess_result_t result)
{
    switch (result)
    {
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return "Intel GPU required";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return "Intel driver is too old";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return "libxess.dll could not be loaded";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "out of GPU memory";
    default: return nullptr;
    }
}

void LogCallback(const char* message, xess_logging_level_t level)
{
    if (level < XESS_LOGGING_LEVEL_WARNING || !message || !api.sink) return;
    char line[192];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "XeSS SDK: %s", message);
    api.sink(line, unsigned(level));
}

class ContextScope
{
public:
    ContextScope(ID3D11DeviceContext1* context, ID3DDeviceContextState* state) : m_context(context)
    { m_context->SwapDeviceContextState(state, &m_previous); }
    ~ContextScope()
    {
        m_context->ClearState();
        m_context->SwapDeviceContextState(m_previous.Get(), nullptr);
    }
private:
    ID3D11DeviceContext1* m_context;
    ComPtr<ID3DDeviceContextState> m_previous;
};
}

bool D3D11Backend::Check(int result, const char* stage)
{
    if (result >= XESS_RESULT_SUCCESS) return true; // warnings pass
    if (m_log)
    {
        const char* text = ResultText(static_cast<xess_result_t>(result));
        if (text) { char line[256]; _snprintf_s(line, sizeof(line), _TRUNCATE, "%s (%s)", stage, text); m_log(line, unsigned(result)); }
        else m_log(stage, unsigned(result));
    }
    return false;
}

bool D3D11Backend::OptimalSize(unsigned quality, dlss::Size display, dlss::Size& render)
{
    static const float ratios[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    render = {};
    if (quality > 4 || !display.Valid()) return false;
    const float ratio = ratios[quality];
    render.width = std::max(1u, unsigned(display.width / ratio + 0.5f)) & ~1u;
    render.height = std::max(1u, unsigned(display.height / ratio + 0.5f)) & ~1u;
    return render.Valid() && render.width <= display.width && render.height <= display.height;
}

bool D3D11Backend::Create(ID3D11Device* device, unsigned quality, dlss::Size render, dlss::Size display, bool hdr, Log log)
{
    Destroy();
    m_log = log;
    if (!device || !render.Valid() || !display.Valid() || render.width > display.width || render.height > display.height ||
        quality > 4)
    {
        if (m_log) m_log("XeSS creation requires a D3D11 device, valid dimensions and quality", unsigned(E_INVALIDARG));
        return false;
    }
    if (!api.Load(m_log)) return false;
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
    {
        if (m_log) m_log("XeSS requires D3D feature level 11.0", unsigned(DXGI_ERROR_UNSUPPORTED));
        return false;
    }
    m_device = device;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device1));
    if (SUCCEEDED(hr)) hr = immediate.As(&m_context);
    const auto level = device->GetFeatureLevel();
    D3D_FEATURE_LEVEL selected;
    if (SUCCEEDED(hr)) hr = device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device), &selected, &m_state);
    if (FAILED(hr))
    {
        if (m_log) m_log("XeSS private D3D11 context state", hr);
        Destroy(); return false;
    }
    xess_version_t version{};
    if (api.GetVersion(&version) >= XESS_RESULT_SUCCESS)
    {
        char line[64];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "XeSS runtime version %u.%u.%u",
            version.major, version.minor, version.patch);
        m_log(line, 0);
    }
    api.sink = log;

    const auto result = api.D3D11CreateContext(device, &m_contextHandle);
    if (!Check(int(result), "XeSS context creation")) { Destroy(); return false; }
    api.SetLoggingCallback(m_contextHandle, XESS_LOGGING_LEVEL_WARNING, LogCallback);

    static const xess_quality_settings_t qualities[] = {
        XESS_QUALITY_SETTING_AA, XESS_QUALITY_SETTING_QUALITY, XESS_QUALITY_SETTING_BALANCED,
        XESS_QUALITY_SETTING_PERFORMANCE, XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,
    };
    xess_d3d11_init_params_t init{};
    init.outputResolution = {display.width, display.height};
    init.qualitySetting = qualities[quality];
    init.initFlags = hdr ? XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE : XESS_INIT_FLAG_LDR_INPUT_COLOR;
    if (!Check(int(api.D3D11Init(m_contextHandle, &init)), "XeSS initialization")) { Destroy(); return false; }

    D3D11_TEXTURE2D_DESC output{};
    output.Width = display.width; output.Height = display.height;
    output.MipLevels = output.ArraySize = output.SampleDesc.Count = 1;
    output.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    output.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    hr = device->CreateTexture2D(&output, nullptr, &m_output);
    if (SUCCEEDED(hr)) m_output->GetDevice(&m_outputDevice);
    if (FAILED(hr))
    {
        if (m_log) m_log("XeSS output allocation", hr);
        Destroy(); return false;
    }
    // SSS motion vectors are UV displacement; XeSS expects pixel velocity.
    api.SetVelocityScale(m_contextHandle, float(render.width), float(render.height));
    m_render = render; m_display = display;
    m_initialized = true;
    return true;
}

bool D3D11Backend::Validate(const Frame& frame) const
{
    if (!frame.color || !frame.depth || !frame.motion || frame.color == m_output.Get() ||
        !std::isfinite(frame.jitter.x) || !std::isfinite(frame.jitter.y)) return false;
    for (auto* input : {frame.color, frame.depth, frame.motion})
    {
        D3D11_TEXTURE2D_DESC desc;
        input->GetDesc(&desc);
        ComPtr<ID3D11Device> device;
        input->GetDevice(&device);
        // Compare against the output texture's device: proxy wrappers (ReShade
        // dxgi.dll) do not wrap textures, so m_device alone can mismatch.
        if (device.Get() != m_outputDevice.Get() || desc.Width != m_render.width || desc.Height != m_render.height ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return false;
    }
    D3D11_TEXTURE2D_DESC depth, motion;
    frame.depth->GetDesc(&depth); frame.motion->GetDesc(&motion);
    if (depth.Format != DXGI_FORMAT_R32_FLOAT && depth.Format != DXGI_FORMAT_R32_TYPELESS &&
        depth.Format != DXGI_FORMAT_R24G8_TYPELESS && depth.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS) return false;
    return motion.Format == DXGI_FORMAT_R16G16_FLOAT || motion.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        motion.Format == DXGI_FORMAT_R32G32_FLOAT;
}

bool D3D11Backend::Evaluate(const Frame& frame)
{
    if (!Ready()) return false;
    if (!Validate(frame))
    {
        dlss::LogEvaluationInputs(frame, m_render, m_output.Get(), m_log);
        if (m_log) m_log("Invalid XeSS inputs; disabling until vid_restart", unsigned(E_INVALIDARG));
        m_failed = true; m_history.Invalidate(); return false;
    }
    xess_d3d11_execute_params_t exec{};
    exec.pColorTexture = frame.color;
    exec.pDepthTexture = frame.depth; // hardware Z; low-res MV path requires depth
    exec.pVelocityTexture = frame.motion;
    exec.pOutputTexture = m_output.Get();
    exec.jitterOffsetX = frame.jitter.x;
    exec.jitterOffsetY = frame.jitter.y;
    exec.exposureScale = 1.0f;
    exec.resetHistory = m_history.Begin(frame.frame, frame.cameraCut) ? 1 : 0;
    exec.inputWidth = m_render.width;
    exec.inputHeight = m_render.height;
    ContextScope scope(m_context.Get(), m_state.Get());
    const auto result = api.D3D11Execute(m_contextHandle, &exec);
    if (result < XESS_RESULT_SUCCESS)
    {
        dlss::LogEvaluationInputs(frame, m_render, m_output.Get(), m_log, unsigned(result));
        Check(int(result), "XeSS evaluation failed; disabling until vid_restart");
        m_failed = true;
        m_history.Invalidate();
        return false;
    }
    m_history.Commit(frame.frame);
    return true;
}

void D3D11Backend::Destroy()
{
    api.sink = nullptr;
    if (m_contextHandle) api.DestroyContext(m_contextHandle);
    m_contextHandle = nullptr;
    m_initialized = m_failed = false;
    m_output.Reset(); m_outputDevice.Reset();
    m_state.Reset(); m_context.Reset(); m_device.Reset();
    m_render = m_display = {};
    m_history.Invalidate();
}

void D3D11Backend::Shutdown() { Destroy(); }
}
