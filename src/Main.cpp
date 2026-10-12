#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include "Security.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t kClass[] = L"FolderGuard.NativeDesktop";
constexpr UINT kDone = WM_APP + 11;
constexpr UINT kTick = 20;
constexpr int kPathEditId = 103;

D2D1_COLOR_F C(unsigned rgb, float alpha = 1.0f) {
    return D2D1::ColorF(float((rgb >> 16) & 0xff) / 255.f,
                         float((rgb >> 8) & 0xff) / 255.f,
                         float(rgb & 0xff) / 255.f, alpha);
}
struct Rect { float x{}, y{}, w{}, h{};
    D2D1_RECT_F box() const { return D2D1::RectF(x,y,x+w,y+h); }
    bool contains(float mx, float my) const { return mx >= x && my >= y && mx <= x+w && my <= y+h; }
};
enum class ButtonId { Browse, Refresh, Open, QuickLock, QuickUnlock, FullLock, FullUnlock, System, Hidden };
struct Button { ButtonId id; Rect rect; };
struct Outcome { security::Result result; unsigned long ms; };

class App {
public:
    bool Init(HINSTANCE instance) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized_ = SUCCEEDED(co);
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
             reinterpret_cast<IUnknown**>(write_.GetAddressOf())))) return false;
        Format(formatHero_, 31, DWRITE_FONT_WEIGHT_BOLD);
        Format(formatTitle_, 19, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Format(formatBody_, 14, DWRITE_FONT_WEIGHT_NORMAL);
        Format(formatBold_, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Format(formatSmall_, 12, DWRITE_FONT_WEIGHT_NORMAL);
        Format(formatTiny_, 11, DWRITE_FONT_WEIGHT_NORMAL);
        WNDCLASSEXW cls = {sizeof(cls)};
        cls.hInstance = instance;
        cls.lpfnWndProc = WindowProc;
        cls.lpszClassName = kClass;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr, IDI_SHIELD);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!RegisterClassExW(&cls)) return false;
        hwnd_ = CreateWindowExW(0, kClass, L"FolderGuard 3.0 — 文件保护工作台",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                1180, 835, nullptr, nullptr, instance, this);
        if (!hwnd_) return false;
        edit_ = CreateWindowExW(0, L"EDIT", L"E:\\FolderGuard-Test",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 100, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathEditId)), instance, nullptr);
        font_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        LayoutEdit();
        brushEdit_ = CreateSolidBrush(RGB(25,35,54));
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        Refresh();
        return true;
    }
    ~App() {
        if (brushEdit_) DeleteObject(brushEdit_);
        if (font_) DeleteObject(font_);
        if (comInitialized_) CoUninitialize();
    }
    int Run() {
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return static_cast<int>(msg.wParam);
    }
private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND edit_{};
    HFONT font_{};
    HBRUSH brushEdit_{};
    bool comInitialized_ = false;
    bool busy_ = false;
    unsigned long started_{};
    float hoverX_ = -1, hoverY_ = -1;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteTextFormat> formatHero_, formatTitle_, formatBody_, formatBold_, formatSmall_, formatTiny_;
    std::vector<Button> buttons_;
    std::wstring path_ = L"E:\\FolderGuard-Test";
    std::wstring notice_ = L"请选择一个独立的 NTFS 数据目录，开始配置保护。";
    bool success_ = true;
    security::Status status_;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<App*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) return self->Handle(msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    void Format(ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight) {
        if (SUCCEEDED(write_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                   DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", out.GetAddressOf()))) {
            out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = 860;
            mm->ptMinTrackSize.y = 640;
            return 0;
        }
        case WM_SIZE: {
            if (target_) target_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            LayoutEdit();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            RECT rc = {}; GetClientRect(hwnd_, &rc);
            hoverX_ = static_cast<float>(GET_X_LPARAM(lp)) * 1180.f / std::max(1L,rc.right);
            hoverY_ = static_cast<float>(GET_Y_LPARAM(lp)) * 820.f / std::max(1L,rc.bottom);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverX_ = hoverY_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            RECT rc = {}; GetClientRect(hwnd_, &rc);
            float mx = float(GET_X_LPARAM(lp)) * 1180.f / std::max(1L,rc.right);
            float my = float(GET_Y_LPARAM(lp)) * 820.f / std::max(1L,rc.bottom);
            if (!busy_) {
                for (const auto& b : buttons_) if (b.rect.contains(mx,my)) { OnButton(b.id); break; }
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kPathEditId && HIWORD(wp) == EN_CHANGE && !busy_) {
                // Read the path only on action/refresh, to avoid triggering a scan on every keystroke.
            }
            return 0;
        case WM_CTLCOLOREDIT: {
            auto dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(235,241,255));
            SetBkColor(dc, RGB(25,35,54));
            return reinterpret_cast<LRESULT>(brushEdit_);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == kTick) InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case kDone: {
            std::unique_ptr<Outcome> result(reinterpret_cast<Outcome*>(lp));
            busy_ = false;
            KillTimer(hwnd_, kTick);
            EnableWindow(edit_, TRUE);
            success_ = result->result.ok;
            notice_ = (success_ ? L"操作完成 · " : L"操作失败 · ") + result->result.message +
                      L"（" + std::to_wstring(result->ms) + L" ms）";
            Refresh();
            return 0;
        }
        case WM_CLOSE:
            if (busy_) { MessageBoxW(hwnd_, L"权限处理尚未完成，请等待操作结束。", L"FolderGuard", MB_ICONINFORMATION); return 0; }
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            target_.Reset();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    std::wstring EditPath() {
        const int length = GetWindowTextLengthW(edit_);
        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 2);
        GetWindowTextW(edit_, buffer.data(), static_cast<int>(buffer.size()));
        return std::wstring(buffer.data());
    }
    void LayoutEdit() {
        if (!edit_) return;
        RECT rc = {}; GetClientRect(hwnd_, &rc);
        const float sx = float(rc.right - rc.left) / 1180.f;
        const float sy = float(rc.bottom - rc.top) / 820.f;
        // Native EDIT control tracks the Direct2D virtual canvas.
        MoveWindow(edit_, int(294*sx), int(203*sy), int(586*sx), int(31*sy), TRUE);
    }
    void Refresh() {
        if (!busy_) {
            path_ = EditPath();
            status_ = security::ReadStatus(path_);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void OnButton(ButtonId id) {
        switch (id) {
        case ButtonId::Browse: Browse(); return;
        case ButtonId::Refresh: Refresh(); return;
        case ButtonId::Open: {
            path_ = EditPath();
            ShellExecuteW(hwnd_, L"open", path_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        default: break;
        }
        path_ = EditPath();
        auto prepared = security::PrepareDirectory(path_, true);
        if (!prepared.ok) { notice_ = prepared.message; success_ = false; Refresh(); return; }
        // A new path may have been typed without pressing Refresh.
        status_ = security::ReadStatus(path_);
        security::Action action;
        switch (id) {
        case ButtonId::QuickLock: action = security::Action::QuickLock; break;
        case ButtonId::QuickUnlock: action = security::Action::QuickUnlock; break;
        case ButtonId::FullLock: action = security::Action::FullLock; break;
        case ButtonId::FullUnlock: action = security::Action::FullUnlock; break;
        case ButtonId::System: action = status_.system ? security::Action::SystemOff : security::Action::SystemOn; break;
        case ButtonId::Hidden: action = status_.hidden ? security::Action::HiddenOff : security::Action::HiddenOn; break;
        default: return;
        }
        const bool full = action == security::Action::FullLock || action == security::Action::FullUnlock;
        const bool lock = action == security::Action::QuickLock || action == security::Action::FullLock;
        if (full) {
            const wchar_t* warning = action == security::Action::FullLock
                ? L"完整加锁将批量重置子项 ACL，原有自定义权限可能丢失。请先备份数据和 ACL。继续吗？"
                : L"完整解锁将递归重置所有子文件 ACL，不会恢复原来的独立权限。继续吗？";
            if (MessageBoxW(hwnd_, warning, L"高风险操作确认", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        } else if (lock && MessageBoxW(hwnd_, L"将限制目标目录访问并添加防删除规则，但父目录 DELETE_CHILD 可能仍允许删除。继续吗？",
                                      L"权限修改确认", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        busy_ = true;
        started_ = GetTickCount();
        EnableWindow(edit_, FALSE);
        notice_ = full ? L"正在批量处理子文件权限，请勿关闭程序…" : L"正在处理顶层目录权限…";
        SetTimer(hwnd_, kTick, 60, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
        const auto targetPath = path_;
        const HWND notify = hwnd_;
        std::thread([targetPath, action, notify]() {
            DWORD start = GetTickCount();
            auto res = security::Apply(targetPath, action);
            auto *out = new Outcome{res, GetTickCount() - start};
            if (!PostMessageW(notify, kDone, 0, reinterpret_cast<LPARAM>(out))) delete out;
        }).detach();
    }
    void Browse() {
        ComPtr<IFileOpenDialog> dlg;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
        DWORD options = 0;
        dlg->GetOptions(&options);
        dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"选择需要保护的 NTFS 数据目录");
        if (FAILED(dlg->Show(hwnd_))) return;
        ComPtr<IShellItem> item;
        if (FAILED(dlg->GetResult(&item))) return;
        PWSTR chosen = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)) && chosen) {
            SetWindowTextW(edit_, chosen);
            CoTaskMemFree(chosen);
            Refresh();
        }
    }
    void Shape(Rect r, unsigned fill, float radius = 16.f, unsigned stroke = 0, float opacity = 1.f) {
        if (!target_) return;
        ComPtr<ID2D1SolidColorBrush> b;
        target_->CreateSolidColorBrush(C(fill, opacity), b.GetAddressOf());
        if (!b) return;
        auto rounded = D2D1::RoundedRect(r.box(), radius, radius);
        target_->FillRoundedRectangle(rounded, b.Get());
        if (stroke) {
            ComPtr<ID2D1SolidColorBrush> line;
            target_->CreateSolidColorBrush(C(stroke), line.GetAddressOf());
            if (line) target_->DrawRoundedRectangle(rounded, line.Get(), 1.f);
        }
    }
    void Text(std::wstring s, Rect rect, IDWriteTextFormat* format, unsigned color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        if (!format || !target_) return;
        format->SetTextAlignment(align);
        ComPtr<ID2D1SolidColorBrush> brush;
        target_->CreateSolidColorBrush(C(color), brush.GetAddressOf());
        if (brush) target_->DrawText(s.c_str(), static_cast<UINT32>(s.size()), format, rect.box(), brush.Get(),
                                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Circle(float x, float y, float radius, unsigned color) {
        ComPtr<ID2D1SolidColorBrush> b;
        target_->CreateSolidColorBrush(C(color), b.GetAddressOf());
        if (b) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y), radius, radius), b.Get());
    }
    void ButtonDraw(ButtonId id, Rect r, const std::wstring& title, const std::wstring& desc,
                    unsigned accent, bool filled = false) {
        buttons_.push_back({id, r});
        bool hover = r.contains(hoverX_,hoverY_) && !busy_;
        unsigned bg = filled ? (hover ? 0x567CEF : 0x4067DB) : (hover ? 0x273954 : 0x1B2940);
        Shape(r, bg, 14, filled ? 0 : 0x34435C, busy_ ? 0.55f : 1.0f);
        Circle(r.x+25, r.y+29, 7, accent);
        Text(title, {r.x+44,r.y+15,r.w-55,27}, formatBold_.Get(), 0xF7FAFF);
        Text(desc, {r.x+20,r.y+54,r.w-30,40}, formatSmall_.Get(), filled ? 0xE5EBFF : 0xA5B4CC);
    }
    bool EnsureTarget() {
        if (target_) return true;
        RECT rc; GetClientRect(hwnd_, &rc);
        HRESULT hr = factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), 96, 96),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(
                std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top))), &target_);
        return SUCCEEDED(hr);
    }
    void Draw() {
        if (!EnsureTarget()) return;
        RECT raw; GetClientRect(hwnd_, &raw);
        const float W = 1180.f;
        const float H = 820.f;
        const float L = 250.f;
        const float X = L + 22.f;
        const float R = W - X - 26.f;
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(float(raw.right)/1180.f, float(raw.bottom)/820.f));
        target_->Clear(C(0x0B111E));
        buttons_.clear();
        Shape({0,0,L,H}, 0x111B2C, 0);
        Shape({21,30,44,44}, 0x4D72E8, 13);
        Text(L"◆", {32,33,30,40}, formatTitle_.Get(), 0xFFFFFF);
        Text(L"FolderGuard", {80,30,158,38}, formatTitle_.Get(), 0xF5F8FF);
        Text(L"NATIVE SECURITY WORKSPACE", {25,85,218,21}, formatTiny_.Get(), 0x8194B6);
        Shape({16,155,218,47}, 0x263C67, 10);
        Text(L"▣   文件保护工作台", {34,166,189,30}, formatBold_.Get(), 0xFFFFFF);
        Text(L"系统工具", {30,243,180,26}, formatSmall_.Get(), 0x7288A7);
        Text(L"Windows 7 / 10", {33,283,180,24}, formatBody_.Get(), 0xC2CEE2);
        Text(L"Win32 + Direct2D", {33,323,180,24}, formatBody_.Get(), 0xC2CEE2);
        Shape({20,H-132,211,105}, 0x1A2940, 14, 0x31425E);
        Text(L"LOCAL FIRST", {35,H-117,186,25}, formatBold_.Get(), 0x9FC0FF);
        Text(L"不联网 · 不上传文件", {35,H-83,183,25}, formatSmall_.Get(), 0xDDE6F6);
        Text(L"仅本机管理员可以修改 ACL", {35,H-58,184,25}, formatTiny_.Get(), 0x91A5C2);

        Text(L"文件夹保护中心", {X,31,R-200,48}, formatHero_.Get(), 0xF2F5FC);
        Text(L"清晰、安全、可恢复的本地权限管理", {X,83,R-200,29}, formatBody_.Get(), 0x93A5BE);
        ButtonDraw(ButtonId::Refresh, {X+R-134,45,132,48}, L"刷新状态", L"", 0x70C4FF);
        Shape({X,132,R,128}, 0x142035, 18, 0x243853);
        Text(L"目标 NTFS 文件夹", {X+21,147,R-42,29}, formatBold_.Get(), 0xC3D5EF);
        // EDIT is a true native control with normal clipboard and keyboard support.
        Shape({X+15,195,R-174,44}, 0x192336, 10, 0x34455F);
        ButtonDraw(ButtonId::Browse, {X+R-148,192,129,48}, L"选择目录", L"", 0xA9C4FF);

        Shape({X,276,R,109}, 0x15243A, 18, 0x2C4460);
        Circle(X+30,316,10, busy_ ? 0xF1B95C :
                     (status_.aclProtected ? 0x64D8AD : 0xE3AE67));
        const std::wstring state = busy_ ? L"正在处理权限…" : status_.aclProtected ? L"目录 ACL 已限制继承" :
                          status_.exists ? L"目录未开启顶层 ACL 隔离" : L"请先选择有效的 NTFS 目录";
        Text(state, {X+53,292,R-85,35}, formatTitle_.Get(), 0xF2F7FF);
        const std::wstring props = std::wstring(L"SYSTEM ") + (status_.system ? L"ON" : L"OFF") +
            L"    /    HIDDEN " + (status_.hidden ? L"ON" : L"OFF") +
            L"    /    DELETE DENY " + (status_.denyDelete ? L"ON" : L"OFF");
        Text(props, {X+53,340,R-85,27}, formatSmall_.Get(), 0x96B3D0);

        Text(L"权限控制", {X,404,300,32}, formatTitle_.Get(), 0xF3F6FF);
        Text(L"快速模式不遍历子文件", {X+R-240,411,240,26}, formatSmall_.Get(), 0x8398B8, DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float gap = 13;
        const float tile = (R-gap)/2;
        ButtonDraw(ButtonId::QuickLock,   {X,447,tile,101}, L"快速加锁", L"顶层 ACL  +  禁止删除  +S", 0x79D9BA, true);
        ButtonDraw(ButtonId::QuickUnlock, {X+tile+gap,447,tile,101}, L"快速解锁", L"顶层恢复继承，不改子文件", 0x7FA7FA);
        ButtonDraw(ButtonId::FullLock,    {X,561,tile,101}, L"完整加锁", L"递归修改子文件 ACL · 慎用", 0xF7BE79);
        ButtonDraw(ButtonId::FullUnlock,  {X+tile+gap,561,tile,101}, L"完整解锁", L"递归重置原权限 · 慎用", 0xE5A6A6);

        Shape({X,679,R,70}, 0x152035, 14, 0x283953);
        Text(L"目录属性", {X+18,691,143,28}, formatBold_.Get(), 0xD8E2F5);
        ButtonDraw(ButtonId::System, {X+164,688,190,52}, status_.system ? L"✓ System +S" : L"System -S", L"", 0xA7CBFF);
        ButtonDraw(ButtonId::Hidden, {X+366,688,190,52}, status_.hidden ? L"✓ Hidden +H" : L"Hidden -H", L"", 0xBDAAFF);
        ButtonDraw(ButtonId::Open, {X+R-139,688,122,52}, L"打开目录", L"", 0xA0D3E5);

        const float footerY = std::max(758.f, H-52.f);
        Shape({X,footerY,R,43}, success_ ? 0x172D36 : 0x39252C, 10);
        if (busy_) {
            float phase = (GetTickCount() - started_) / 500.f;
            Shape({X+6+std::fmod(phase*100.f, std::max(1.f,R-155.f)),footerY+36,100,3}, 0x68D0B5, 1.f);
        }
        Text(notice_, {X+13,footerY+7,R-22,30}, formatSmall_.Get(), success_ ? 0xB3D6CF : 0xF3ADBA);
        if (FAILED(target_->EndDraw())) target_.Reset();
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware(); // Available on Windows 7; D2D coordinates are explicitly 96 dpi pixels.
    App app;
    if (!app.Init(instance)) {
        MessageBoxW(nullptr, L"无法初始化 Direct2D / DirectWrite 窗口。", L"FolderGuard", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
