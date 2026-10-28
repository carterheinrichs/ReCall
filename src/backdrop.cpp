#include "backdrop.h"
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <d2d1effects.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.graphics.effects.interop.h>
#include <windows.ui.composition.interop.h>

namespace recall {
namespace {
namespace composition=winrt::Windows::UI::Composition;
namespace effects=winrt::Windows::Graphics::Effects;
namespace abi_effects=ABI::Windows::Graphics::Effects;

// A real Gaussian blur effect over the live host backdrop, not a translucent
// color pretending to blur. The compositor owns and processes the pixels.
struct BackdropBlur : winrt::implements<BackdropBlur,effects::IGraphicsEffect,
    effects::IGraphicsEffectSource,abi_effects::IGraphicsEffectD2D1Interop> {
    effects::IGraphicsEffectSource source{nullptr};
    winrt::hstring Name() const { return L"ReCallBackdropBlur"; }
    void Name(const winrt::hstring&) {}
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
        if(!id) return E_POINTER; *id=CLSID_D2D1GaussianBlur; return S_OK;
    }
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR,UINT*,abi_effects::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override {
        return E_INVALIDARG;
    }
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
        if(!count) return E_POINTER; *count=3; return S_OK;
    }
    HRESULT __stdcall GetProperty(UINT index,ABI::Windows::Foundation::IPropertyValue** value) noexcept override try {
        if(!value) return E_POINTER;
        *value=nullptr;
        using winrt::Windows::Foundation::PropertyValue;
        winrt::Windows::Foundation::IInspectable property{nullptr};
        switch(index) {
        case 0: property=PropertyValue::CreateSingle(18.f); break;
        case 1: property=PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED); break;
        case 2: property=PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD); break;
        default: return E_INVALIDARG;
        }
        return winrt::get_unknown(property)->QueryInterface(__uuidof(ABI::Windows::Foundation::IPropertyValue),reinterpret_cast<void**>(value));
    } catch(...) { return winrt::to_hresult(); }
    HRESULT __stdcall GetSource(UINT index,abi_effects::IGraphicsEffectSource** value) noexcept override {
        if(!value) return E_POINTER;
        *value=nullptr;
        if(index||!source) return E_INVALIDARG;
        return winrt::get_unknown(source)->QueryInterface(winrt::guid_of<effects::IGraphicsEffectSource>(),reinterpret_cast<void**>(value));
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
        if(!count) return E_POINTER; *count=1; return S_OK;
    }
};
LRESULT CALLBACK backdrop_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd,message,wp,lp);
}
bool transparency_enabled() {
    DWORD enabled=1,size=sizeof(enabled);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&enabled,&size);
    BOOL composition=FALSE; DwmIsCompositionEnabled(&composition);
    return enabled&&composition;
}
}
struct GlassBackdrop::State {
    HWND window{};
    bool enabled=false;
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    composition::Compositor compositor{nullptr};
    composition::Desktop::DesktopWindowTarget target{nullptr};
    composition::SpriteVisual visual{nullptr};
    composition::CompositionRoundedRectangleGeometry geometry{nullptr};
    ~State() {
        if(target) target.Root(nullptr);
        target=nullptr; visual=nullptr; geometry=nullptr; compositor=nullptr;
        if(window) DestroyWindow(window);
        if(queue) queue.ShutdownQueueAsync();
    }
    void initialize() {
        if(compositor) return;
        if(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};
            winrt::check_hresult(CreateDispatcherQueueController(options,
                reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(queue))));
        }
        WNDCLASSW cls{}; cls.lpfnWndProc=backdrop_proc; cls.hInstance=GetModuleHandleW(nullptr);
        cls.lpszClassName=L"ReCall.GlassBackdrop";
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) winrt::throw_last_error();
        window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            cls.lpszClassName,L"ReCall glass backdrop",WS_POPUP,0,0,1,1,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window) winrt::throw_last_error();
        const BOOL host=TRUE;
        winrt::check_hresult(DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&host,sizeof(host)));
        // Feed the desktop into this composition-only HWND. It deliberately
        // has neither layered opacity nor a native clipping region.
        struct AccentPolicy { int state; DWORD flags,color,animation; } policy{5,0,0,0};
        struct CompositionData { int attribute; void* data; SIZE_T size; } data{19,&policy,sizeof(policy)};
        using SetCompositionAttribute=BOOL(WINAPI*)(HWND,CompositionData*);
        auto set=reinterpret_cast<SetCompositionAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
        if(!set||!set(window,&data)) throw std::runtime_error("Cannot enable live host backdrop");
        const int corner=DWMWCP_DONOTROUND;
        const COLORREF border=DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));
        DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
        compositor=composition::Compositor();
        auto desktop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(desktop->CreateDesktopWindowTarget(window,TRUE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target))));
        auto blur=winrt::make_self<BackdropBlur>();
        blur->source=composition::CompositionEffectSourceParameter(L"Desktop");
        auto brush=compositor.CreateEffectFactory(*blur).CreateBrush();
        brush.SetSourceParameter(L"Desktop",compositor.CreateHostBackdropBrush());
        visual=compositor.CreateSpriteVisual(); visual.Brush(brush);
        geometry=compositor.CreateRoundedRectangleGeometry();
        visual.Clip(compositor.CreateGeometricClip(geometry));
        visual.BorderMode(composition::CompositionBorderMode::Soft);
        target.Root(visual);
    }
};
GlassBackdrop::GlassBackdrop():state_(std::make_unique<State>()) {}
GlassBackdrop::~GlassBackdrop()=default;
bool GlassBackdrop::configure(bool enabled) {
    state_->enabled=enabled&&transparency_enabled();
    if(state_->enabled) {
        try { state_->initialize(); }
        catch(...) { state_->enabled=false; if(state_->window) ShowWindow(state_->window,SW_HIDE); throw; }
    } else if(state_->window) ShowWindow(state_->window,SW_HIDE);
    return state_->enabled;
}
void GlassBackdrop::sync(HWND palette,int dpi,bool visible) {
    if(!state_->window) return;
    if(!visible||!state_->enabled) { ShowWindow(state_->window,SW_HIDE); return; }
    RECT rect{}; GetWindowRect(palette,&rect);
    const auto width=static_cast<float>(rect.right-rect.left),height=static_cast<float>(rect.bottom-rect.top);
    state_->visual.Size({width,height}); state_->geometry.Size({width,height});
    const float radius=static_cast<float>(MulDiv(28,dpi,96));
    state_->geometry.CornerRadius({radius,radius});
    // hWndInsertAfter=palette places this surface directly behind the palette
    // in the same topmost band, without ever taking keyboard or mouse focus.
    SetWindowPos(state_->window,palette,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,
        SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
}
