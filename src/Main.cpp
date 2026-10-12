#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
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
#include "Security.h"

#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t kClass[]=L"FolderGuard.NativeDesktop";
constexpr wchar_t kGitHub[]=L"https://github.com/mrgolftech/FolderGuard";
constexpr UINT kDone=WM_APP+11;
constexpr UINT kPathId=103;
constexpr float W=1000.f, H=680.f;
D2D1_COLOR_F Color(unsigned rgb,float alpha=1.f){
    return D2D1::ColorF(float((rgb>>16)&255)/255.f,float((rgb>>8)&255)/255.f,float(rgb&255)/255.f,alpha);
}
struct Rect {
    float x,y,w,h;
    D2D1_RECT_F box()const{return D2D1::RectF(x,y,x+w,y+h);}
    bool contains(float mx,float my)const{return mx>=x && mx<=x+w && my>=y && my<=y+h;}
};
enum class Control { Load, Browse, Refresh, Acl, System, Hidden, Apply, GitHub };
struct Hit {Control id;Rect rect;bool enabled;};
struct Outcome {
    security::Result result;
    security::Status after;
    std::wstring actions;
    DWORD elapsed=0;
};
std::wstring StateWord(bool v){return v?L"已开启":L"未开启";}

class App {
public:
    bool Init(HINSTANCE inst,const std::wstring& initialPath) {
        inst_=inst;
        auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        comInit_=SUCCEEDED(co);
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))return false;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
           reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))return false;
        MakeFormat(hero_,27,DWRITE_FONT_WEIGHT_BOLD);
        MakeFormat(section_,17,DWRITE_FONT_WEIGHT_SEMI_BOLD);
        MakeFormat(body_,14,DWRITE_FONT_WEIGHT_NORMAL);
        MakeFormat(bold_,14,DWRITE_FONT_WEIGHT_SEMI_BOLD);
        MakeFormat(small_,12,DWRITE_FONT_WEIGHT_NORMAL);
        MakeFormat(tiny_,11,DWRITE_FONT_WEIGHT_NORMAL);
        WNDCLASSEXW cls={sizeof(cls)};
        cls.hInstance=inst;
        cls.lpfnWndProc=&WndProc;
        cls.lpszClassName=kClass;
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hIcon=LoadIconW(nullptr,IDI_SHIELD);
        cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        if(!RegisterClassExW(&cls))return false;
        hwnd_=CreateWindowExW(0,kClass,L"FolderGuard 3.2 — 文件夹保护",
           WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1020,745,
           nullptr,nullptr,inst,this);
        if(!hwnd_)return false;
        edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",initialPath.c_str(),
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
           0,0,100,34,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathId)),inst,nullptr);
        font_=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        editBrush_=CreateSolidBrush(RGB(248,250,253));
        LayoutEdit();
        ShowWindow(hwnd_,SW_SHOW);
        UpdateWindow(hwnd_);
        if(!initialPath.empty())LoadSelected(false);
        return true;
    }
    ~App(){
        if(editBrush_)DeleteObject(editBrush_);
        if(font_)DeleteObject(font_);
        if(comInit_)CoUninitialize();
    }
    int Run(){
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        return int(msg.wParam);
    }
private:
    HINSTANCE inst_{};
    HWND hwnd_{},edit_{};
    HFONT font_{};
    HBRUSH editBrush_{};
    bool comInit_=false,ready_=false,busy_=false,suppressChange_=false;
    bool draftAcl_=false,draftSystem_=false,draftHidden_=false;
    bool modified_=false,success_=true;
    float mx_=-1.f,my_=-1.f;
    std::wstring selected_,notice_=L"请选择或输入一个 NTFS 文件夹，然后点击“载入”。";
    security::Status actual_{};
    std::vector<Hit> hits_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteTextFormat> hero_,section_,body_,bold_,small_,tiny_;

    void MakeFormat(ComPtr<IDWriteTextFormat>& out,float size,DWRITE_FONT_WEIGHT weight){
        if(SUCCEEDED(dwrite_->CreateTextFormat(L"Segoe UI",nullptr,weight,
           DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",out.GetAddressOf()))){
            out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        App* self=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){
            auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
            self=static_cast<App*>(cs->lpCreateParams);
            self->hwnd_=hwnd;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        return self?self->Handle(msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
    }
    LRESULT Handle(UINT msg,WPARAM wp,LPARAM lp){
        switch(msg){
        case WM_GETMINMAXINFO:{
            auto* mm=reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x=810; mm->ptMinTrackSize.y=610;return 0;
        }
        case WM_SIZE:
            if(target_)target_->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));
            LayoutEdit();InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        case WM_MOUSEMOVE:{
            TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd_,0};TrackMouseEvent(&t);
            RECT rc{};GetClientRect(hwnd_,&rc);
            mx_=float(GET_X_LPARAM(lp))*W/std::max(1L,rc.right);
            my_=float(GET_Y_LPARAM(lp))*H/std::max(1L,rc.bottom);
            InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        }
        case WM_MOUSELEAVE:mx_=my_=-1;InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:{
            RECT rc{};GetClientRect(hwnd_,&rc);
            float x=float(GET_X_LPARAM(lp))*W/std::max(1L,rc.right);
            float y=float(GET_Y_LPARAM(lp))*H/std::max(1L,rc.bottom);
            if(!busy_)for(auto hit:hits_)if(hit.enabled && hit.rect.contains(x,y)){
                Click(hit.id);break;
            }
            return 0;
        }
        case WM_COMMAND:
            if(LOWORD(wp)==kPathId && HIWORD(wp)==EN_CHANGE && !suppressChange_ && !busy_){
                ready_=false; selected_.clear();modified_=false;actual_=security::Status{};
                draftAcl_=draftSystem_=draftHidden_=false;
                notice_=L"路径已修改。请点击“载入目录”，再配置权限。";
                InvalidateRect(hwnd_,nullptr,FALSE);
            }
            return 0;
        case WM_CTLCOLOREDIT:{
            auto dc=reinterpret_cast<HDC>(wp);
            SetTextColor(dc,RGB(32,48,76));SetBkColor(dc,RGB(248,250,253));
            return reinterpret_cast<LRESULT>(editBrush_);
        }
        case WM_PAINT:{
            PAINTSTRUCT ps{};BeginPaint(hwnd_,&ps);Draw();EndPaint(hwnd_,&ps);return 0;
        }
        case kDone:{
            std::unique_ptr<Outcome> out(reinterpret_cast<Outcome*>(lp));
            busy_=false;EnableWindow(edit_,TRUE);
            actual_=security::ReadStatus(selected_);
            ready_=actual_.exists && actual_.ntfs && actual_.error.empty();
            AdoptActual();
            success_=out->result.ok;
            notice_=success_?L"设置已完成，当前状态已从磁盘重新读取。":L"部分操作失败，请检查当前状态。";
            InvalidateRect(hwnd_,nullptr,FALSE);
            std::wstring report=(success_?L"应用完成":L"应用未完全成功");
            report+=L"\r\n\r\n"+out->actions+L"\r\n";
            report+=L"耗时："+std::to_wstring(out->elapsed)+L" 毫秒\r\n";
            report+=L"执行结果："+out->result.message+L"\r\n\r\n";
            report+=L"实际 ACL："+StateWord(actual_.aclProtected)+
                    L"\r\n禁止删除规则："+StateWord(actual_.denyDelete)+
                    L"\r\n系统属性 +S："+StateWord(actual_.system)+
                    L"\r\n隐藏属性 +H："+StateWord(actual_.hidden);
            if(!actual_.error.empty())report+=L"\r\n状态读取："+actual_.error;
            MessageBoxW(hwnd_,report.c_str(),L"FolderGuard · 应用设置结果",
                MB_OK|(success_?MB_ICONINFORMATION:MB_ICONWARNING));
            return 0;
        }
        case WM_CLOSE:
            if(busy_){MessageBoxW(hwnd_,L"权限修改尚未结束，请在收到结果后关闭程序。",
                       L"FolderGuard",MB_ICONINFORMATION);return 0;}
            DestroyWindow(hwnd_);return 0;
        case WM_DESTROY:target_.Reset();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }
    void LayoutEdit(){
        if(!edit_)return;
        RECT rc{};GetClientRect(hwnd_,&rc);
        float sx=float(rc.right)/W,sy=float(rc.bottom)/H;
        MoveWindow(edit_,int(66*sx),int(175*sy),int(632*sx),int(37*sy),TRUE);
    }
    std::wstring EditPath()const{
        int len=GetWindowTextLengthW(edit_);
        std::vector<wchar_t> chars(static_cast<size_t>(len)+2);
        GetWindowTextW(edit_,chars.data(),int(chars.size()));
        return chars.data();
    }
    void SetPath(const std::wstring& p){
        suppressChange_=true;SetWindowTextW(edit_,p.c_str());suppressChange_=false;
    }
    void AdoptActual(){
        // All controls are synchronized to the real NTFS state after load, refresh, or completion.
        draftAcl_=ready_ && actual_.aclProtected;
        draftSystem_=ready_ && actual_.system;
        draftHidden_=ready_ && actual_.hidden;
        modified_=false;
    }
    void Refresh(){
        if(!ready_)return;
        actual_=security::ReadStatus(selected_);
        ready_=actual_.exists && actual_.ntfs && actual_.error.empty();
        AdoptActual();
        success_=ready_;
        notice_=ready_?L"已刷新：设置项与磁盘实际权限和属性保持一致。":
             L"刷新失败："+actual_.error;
        InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void LoadSelected(bool prompt){
        std::wstring path=EditPath();
        if(path.empty()){notice_=L"请先选择文件夹。";ready_=false;InvalidateRect(hwnd_,nullptr,FALSE);return;}
        auto checked=security::PrepareDirectory(path,false);
        if(!checked.ok && prompt && checked.message==L"目录不存在。"){
            if(MessageBoxW(hwnd_,L"目录不存在，是否创建新目录？",L"创建目标文件夹",
                MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
                checked=security::PrepareDirectory(path,true);
        }
        if(!checked.ok){
            ready_=false;selected_.clear();actual_=security::Status{};
            AdoptActual();success_=false;notice_=checked.message;
            InvalidateRect(hwnd_,nullptr,FALSE);return;
        }
        selected_=path;
        actual_=security::ReadStatus(selected_);
        ready_=actual_.exists && actual_.ntfs && actual_.error.empty();
        AdoptActual();
        notice_=ready_?L"已载入文件夹。更改下方选项后，点击红色按钮应用设置。":
            L"无法读取该目录状态："+actual_.error;
        success_=ready_;
        InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void Browse(){
        ComPtr<IFileOpenDialog> dlg;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&dlg))))return;
        DWORD options=0;dlg->GetOptions(&options);
        dlg->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"选择要管理的 NTFS 数据目录");
        if(FAILED(dlg->Show(hwnd_)))return;
        ComPtr<IShellItem> item;
        if(FAILED(dlg->GetResult(&item)))return;
        PWSTR path=nullptr;
        if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)) && path){
            SetPath(path);CoTaskMemFree(path);LoadSelected(false);
        }
    }
    void Click(Control id){
        if(id==Control::GitHub){
            ShellExecuteW(hwnd_,L"open",kGitHub,nullptr,nullptr,SW_SHOWNORMAL);return;
        }
        if(id==Control::Browse){Browse();return;}
        if(id==Control::Load){LoadSelected(true);return;}
        if(!ready_||busy_)return;
        if(id==Control::Refresh){Refresh();return;}
        if(id==Control::Acl){
            draftAcl_=!draftAcl_;
            if(draftAcl_)draftSystem_=true; // Legacy invariant: quick lock auto-adds +S.
        }else if(id==Control::System){
            if(draftAcl_ && draftSystem_){
                notice_=L"快速 ACL 加锁必须同时开启 +S。要取消 +S，请先取消 ACL 锁定。";
                success_=false;InvalidateRect(hwnd_,nullptr,FALSE);return;
            }
            draftSystem_=!draftSystem_;
        }else if(id==Control::Hidden)draftHidden_=!draftHidden_;
        else if(id==Control::Apply){ApplyDraft();return;}
        modified_=(draftAcl_!=actual_.aclProtected) ||
                  (draftSystem_!=actual_.system) || (draftHidden_!=actual_.hidden) ||
                  (draftAcl_ && !actual_.denyDelete);
        notice_=modified_?L"设置已预选，磁盘状态尚未更改；点击红色“应用设置”后生效。":
                         L"设置项已与磁盘实际状态对齐。";
        success_=true;InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void ApplyDraft(){
        if(!ready_||busy_)return;
        const bool desiredAcl=draftAcl_,desiredSystem=draftSystem_,desiredHidden=draftHidden_;
        if(desiredAcl && !desiredSystem){
            MessageBoxW(hwnd_,L"快速 ACL 加锁会自动添加系统属性 +S。",
                        L"FolderGuard",MB_ICONINFORMATION);
            draftSystem_=true;return;
        }
        if(actual_.aclProtected!=desiredAcl || (desiredAcl && !actual_.denyDelete)){
            const wchar_t* warning=desiredAcl?
                L"快速加锁会替换目标顶层目录 ACL 并添加拒绝删除规则；它不会保护已知完整路径的子文件。\r\n\r\n确定继续？":
                L"快速解锁会重置目标顶层目录 ACL 为继承权限，原有自定义 ACE 可能丢失。\r\n\r\n确定继续？";
            if(MessageBoxW(hwnd_,warning,L"权限变更确认",
                MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
        }
        busy_=true;EnableWindow(edit_,FALSE);
        notice_=L"正在写入 NTFS 设置，请勿关闭程序…";
        InvalidateRect(hwnd_,nullptr,FALSE);
        const auto path=selected_;const auto notify=hwnd_;
        std::thread([path,notify,desiredAcl,desiredSystem,desiredHidden](){
            const DWORD begin=GetTickCount();
            security::Status state=security::ReadStatus(path);
            security::Result final{true,L"所有请求的设置均已处理。"};
            std::wstring operations;
            auto perform=[&](security::Action act,const wchar_t* title){
                auto r=security::Apply(path,act);
                operations+=title;
                operations+=(r.ok?L"：成功\r\n":L"：失败 — "+r.message+L"\r\n");
                if(!r.ok){final=r;return false;}
                state=security::ReadStatus(path);
                if(!state.error.empty()){
                    final={false,L"重新读取实际状态失败："+state.error};
                    return false;
                }
                return true;
            };
            bool ok=true;
            if(desiredAcl && (!state.aclProtected || !state.denyDelete))
                ok=perform(security::Action::QuickLock,L"快速 ACL 加锁");
            else if(!desiredAcl && state.aclProtected)
                ok=perform(security::Action::QuickUnlock,L"快速 ACL 解锁");
            if(ok && state.system!=desiredSystem)
                ok=perform(desiredSystem?security::Action::SystemOn:security::Action::SystemOff,L"系统属性 +S");
            if(ok && state.hidden!=desiredHidden)
                ok=perform(desiredHidden?security::Action::HiddenOn:security::Action::HiddenOff,L"隐藏属性 +H");
            if(operations.empty())operations=L"当前状态与目标设置一致，无须更改。\r\n";
            auto* result=new Outcome{final,security::ReadStatus(path),operations,GetTickCount()-begin};
            if(!PostMessageW(notify,kDone,0,reinterpret_cast<LPARAM>(result)))delete result;
        }).detach();
    }
    bool EnsureRenderTarget(){
        if(target_)return true;
        RECT rc{};GetClientRect(hwnd_,&rc);
        return SUCCEEDED(factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN,
          D2D1_ALPHA_MODE_UNKNOWN),96,96),D2D1::HwndRenderTargetProperties(
          hwnd_,D2D1::SizeU(std::max(1L,rc.right),std::max(1L,rc.bottom))),target_.GetAddressOf()));
    }
    void Shape(Rect r,unsigned fill,float rad=13.f,unsigned outline=0,float opacity=1.f){
        ComPtr<ID2D1SolidColorBrush> b;
        if(FAILED(target_->CreateSolidColorBrush(Color(fill,opacity),b.GetAddressOf())))return;
        auto shape=D2D1::RoundedRect(r.box(),rad,rad);
        target_->FillRoundedRectangle(shape,b.Get());
        if(outline){ComPtr<ID2D1SolidColorBrush> stroke;
            if(SUCCEEDED(target_->CreateSolidColorBrush(Color(outline),stroke.GetAddressOf())))
                target_->DrawRoundedRectangle(shape,stroke.Get(),1.f);}
    }
    void Text(const std::wstring& str,Rect r,IDWriteTextFormat* fmt,unsigned color,
              DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING){
        if(!fmt)return;fmt->SetTextAlignment(align);
        ComPtr<ID2D1SolidColorBrush> b;
        if(SUCCEEDED(target_->CreateSolidColorBrush(Color(color),b.GetAddressOf())))
            target_->DrawText(str.c_str(),UINT32(str.size()),fmt,r.box(),b.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Dot(float x,float y,float radius,unsigned color){
        ComPtr<ID2D1SolidColorBrush> b;
        if(SUCCEEDED(target_->CreateSolidColorBrush(Color(color),b.GetAddressOf())))
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius),b.Get());
    }
    void Button(Control id,Rect r,const std::wstring& label,bool enabled,bool primary=false){
        hits_.push_back({id,r,enabled});
        const bool hover=enabled && r.contains(mx_,my_) && !busy_;
        unsigned bg=enabled?(primary?(hover?0xAE2E3A:0xCA3E48):(hover?0xE8F0FD:0xF9FBFF)):0xE9EDF3;
        Shape(r,bg,10,primary?0:enabled?0xD9E2EF:0xE1E6ED);
        Text(label,{r.x+10,r.y+8,r.w-20,r.h-10},bold_.Get(),
             enabled?(primary?0xFFFFFF:0x355273):0xA9B3C0,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void Chip(Rect r,const std::wstring& label,bool on){
        Shape(r,on?0xE8F7F0:0xF3F5F8,11,on?0xC5E7D5:0xE3E8EE);
        Dot(r.x+20,r.y+23,5,on?0x20936C:0xB4BCC8);
        Text(label,{r.x+34,r.y+9,r.w-43,24},small_.Get(),on?0x277454:0x65758B);
        Text(on?L"已开启":L"未开启",{r.x+34,r.y+30,r.w-43,22},tiny_.Get(),on?0x288763:0x8491A1);
    }
    void Option(Control id,Rect r,const std::wstring& title,const std::wstring& subtitle,bool on){
        const bool enabled=ready_&&!busy_;
        hits_.push_back({id,r,enabled});
        bool hover=enabled && r.contains(mx_,my_);
        Shape(r,!enabled?0xF3F5F8:on?0xEAF2FF:(hover?0xF7FAFF:0xFFFFFF),
            12,!enabled?0xE7EBF0:on?0x7CA5E8:0xDAE2EC);
        Dot(r.x+21,r.y+28,9,!enabled?0xC7CDD5:on?0x386BCE:0xA6B2C2);
        Text(on?L"✓":L"",{r.x+12,r.y+16,18,24},bold_.Get(),0xFFFFFF);
        Text(title,{r.x+42,r.y+12,r.w-50,26},bold_.Get(),!enabled?0xA3ADBA:0x263955);
        Text(subtitle,{r.x+17,r.y+47,r.w-26,28},small_.Get(),!enabled?0xADB5C0:0x79889B);
    }
    void Draw(){
        if(!EnsureRenderTarget())return;
        RECT rc{};GetClientRect(hwnd_,&rc);
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(float(rc.right)/W,float(rc.bottom)/H));
        target_->Clear(Color(0xF4F7FB));hits_.clear();
        Text(L"FolderGuard",{42,27,390,45},hero_.Get(),0x20344F);
        Text(L"文件夹权限与属性管理",{44,73,465,30},body_.Get(),0x7B8B9E);
        Button(Control::Refresh,{825,42,132,43},L"↻  刷新状态",ready_&&!busy_);

        Shape({42,114,916,123},0xFFFFFF,16,0xE1E7F0);
        Text(L"目标文件夹",{65,131,230,30},section_.Get(),0x263B59);
        Text(L"支持 NTFS · 不会自动扫描其他文件夹",{640,134,293,26},small_.Get(),0x8896A8,DWRITE_TEXT_ALIGNMENT_TRAILING);
        Shape({65,170,640,48},0xF8FAFD,10,0xDCE5EF);
        Button(Control::Load,{715,170,109,47},L"载入目录",!busy_);
        Button(Control::Browse,{839,170,97,47},L"浏览…",!busy_);

        Shape({42,252,916,131},0xFFFFFF,16,0xE1E7F0);
        Text(L"当前保护状态",{65,266,250,31},section_.Get(),0x263B59);
        Text(ready_?L"以下四项均为磁盘当前实际状态":L"未载入目录，权限操作暂不可用",
            {486,270,450,25},small_.Get(),0x8292A6,DWRITE_TEXT_ALIGNMENT_TRAILING);
        float gap=12,chipW=(870.f-3*gap)/4.f;
        Chip({65,307,chipW,59},L"ACL 权限保护",ready_ && actual_.aclProtected);
        Chip({65+chipW+gap,307,chipW,59},L"禁止删除规则",ready_ && actual_.denyDelete);
        Chip({65+(chipW+gap)*2,307,chipW,59},L"系统属性 +S",ready_ && actual_.system);
        Chip({65+(chipW+gap)*3,307,chipW,59},L"隐藏属性 +H",ready_ && actual_.hidden);

        Shape({42,397,916,149},0xFFFFFF,16,0xE1E7F0);
        Text(L"设置选项",{65,410,250,31},section_.Get(),0x263B59);
        Text(modified_?L"有未应用的变更":L"选项与当前状态同步",
             {693,415,244,27},small_.Get(),modified_?0xB64D54:0x7D91A6,DWRITE_TEXT_ALIGNMENT_TRAILING);
        float wOpt=(870.f-24.f)/3.f;
        Option(Control::Acl,{65,454,wOpt,73},L"快速 ACL 保护",L"限制顶层目录 + 拒绝删除",draftAcl_);
        Option(Control::System,{65+wOpt+12,454,wOpt,73},L"系统属性 +S",L"ACL 加锁时会自动启用",draftSystem_);
        Option(Control::Hidden,{65+(wOpt+12)*2,454,wOpt,73},L"隐藏属性 +H",L"默认不在资源管理器显示",draftHidden_);

        Button(Control::Apply,{42,560,916,60},
               busy_?L"正在应用设置…":L"应用设置",ready_&&!busy_,true);
        Shape({42,629,916,35},success_?0xEDF5F1:0xFFF0F0,9);
        Text(notice_,{55,637,730,23},small_.Get(),success_?0x427461:0xA84B55);
        Text(L"v3.2.0  ·  Windows 7 / 10",{42,665,380,20},tiny_.Get(),0x93A1B0);
        hits_.push_back({Control::GitHub,{790,662,168,20},true});
        Text(L"GitHub  /  mrgolftech ↗",{777,663,181,21},small_.Get(),
             0x3768C1,DWRITE_TEXT_ALIGNMENT_TRAILING);
        if(FAILED(target_->EndDraw()))target_.Reset();
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int){
    std::wstring initial;
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv){
        for(int i=1;i+1<argc;++i)if(_wcsicmp(argv[i],L"--path")==0){
            initial=argv[i+1];break;
        }
        LocalFree(argv);
    }
    SetProcessDPIAware();
    App app;
    if(!app.Init(inst,initial)){
        MessageBoxW(nullptr,L"无法初始化 Direct2D / DirectWrite。",L"FolderGuard",MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
