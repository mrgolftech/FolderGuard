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
constexpr UINT kBrowseId=104, kRefreshId=105;
constexpr UINT kAclId=106, kSystemId=107, kHiddenId=108, kDeleteId=109;
constexpr float W=760.f, H=470.f;
D2D1_COLOR_F Color(unsigned rgb,float alpha=1.f){
    return D2D1::ColorF(float((rgb>>16)&255)/255.f,float((rgb>>8)&255)/255.f,float(rgb&255)/255.f,alpha);
}
struct Rect {
    float x,y,w,h;
    D2D1_RECT_F box()const{return D2D1::RectF(x,y,x+w,y+h);}
    bool contains(float mx,float my)const{return mx>=x && mx<=x+w && my>=y && my<=y+h;}
};
enum class Control { Acl, DenyDelete, System, Hidden, Apply, GitHub };
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
        const auto co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        comInit_=SUCCEEDED(co);
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))return false;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
           reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))return false;
        MakeFormat(hero_,25,DWRITE_FONT_WEIGHT_BOLD);
        MakeFormat(section_,16,DWRITE_FONT_WEIGHT_SEMI_BOLD);
        MakeFormat(body_,14,DWRITE_FONT_WEIGHT_NORMAL);
        MakeFormat(bold_,15,DWRITE_FONT_WEIGHT_SEMI_BOLD);
        MakeFormat(small_,13,DWRITE_FONT_WEIGHT_NORMAL);
        MakeFormat(tiny_,12,DWRITE_FONT_WEIGHT_NORMAL);

        HDC dc=GetDC(nullptr);
        const int dpi=dc?GetDeviceCaps(dc,LOGPIXELSX):96;
        if(dc)ReleaseDC(nullptr,dc);
        RECT work{};
        if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0))
            work={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
        // Uniform scaling: respect system DPI, but fit on smaller Windows 7 screens.
        const float fitW=float(work.right-work.left-32)/790.f;
        const float fitH=float(work.bottom-work.top-32)/520.f;
        scale_=std::max(.8f,std::min(float(dpi)/96.f,std::min(fitW,fitH)));

        WNDCLASSEXW cls={sizeof(cls)};
        cls.hInstance=inst;
        cls.lpfnWndProc=&WndProc;
        cls.lpszClassName=kClass;
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hIcon=LoadIconW(nullptr,IDI_SHIELD);
        cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        if(!RegisterClassExW(&cls))return false;
        constexpr DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
        RECT outer={0,0,Px(W),Px(H)};
        AdjustWindowRectEx(&outer,style,FALSE,0);
        hwnd_=CreateWindowExW(0,kClass,L"FolderGuard 0.0.1 — 文件夹保护",
           style,work.left+16,work.top+16,outer.right-outer.left,outer.bottom-outer.top,
           nullptr,nullptr,inst,this);
        if(!hwnd_)return false;

        edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",initialPath.c_str(),
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
           0,0,100,30,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathId)),inst,nullptr);
        browse_=CreateWindowExW(0,L"BUTTON",L"浏览…",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,90,30,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrowseId)),inst,nullptr);
        refresh_=CreateWindowExW(0,L"BUTTON",L"刷新状态",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,90,29,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshId)),inst,nullptr);
        acl_=CreateWindowExW(0,L"BUTTON",L"ACL 权限保护",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,200,27,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAclId)),inst,nullptr);
        deny_=CreateWindowExW(0,L"BUTTON",L"禁止删除",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,200,27,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDeleteId)),inst,nullptr);
        sys_=CreateWindowExW(0,L"BUTTON",L"系统属性 +S",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,200,27,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSystemId)),inst,nullptr);
        hidden_=CreateWindowExW(0,L"BUTTON",L"隐藏属性 +H",
           WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,200,27,
           hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHiddenId)),inst,nullptr);
        if(!edit_||!browse_||!refresh_||!acl_||!deny_||!sys_||!hidden_)return false;
        // Handle Enter within the actual edit HWND, including IME-aware native editing.
        SetWindowLongPtrW(edit_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
        oldEditProc_=reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&EditWndProc)));
        font_=CreateFontW(-Px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(HWND child:{edit_,browse_,refresh_,acl_,deny_,sys_,hidden_})
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        editBrush_=CreateSolidBrush(RGB(248,250,253));
        checkBrush_=CreateSolidBrush(RGB(255,255,255));
        LayoutControls();
        SyncControls();
        ShowWindow(hwnd_,SW_SHOW);
        UpdateWindow(hwnd_);
        if(!initialPath.empty())LoadSelected(false);
        return true;
    }
    ~App(){
        if(editBrush_)DeleteObject(editBrush_);
        if(checkBrush_)DeleteObject(checkBrush_);
        if(font_)DeleteObject(font_);
        if(comInit_)CoUninitialize();
    }
    int Run(){
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            // ENTER on the path field loads it; Tab/Space navigate real Win32 controls.
            if(msg.hwnd==edit_ && msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN){
                TranslateMessage(&msg);DispatchMessageW(&msg);continue;
            }
            if(!IsDialogMessageW(hwnd_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        return int(msg.wParam);
    }
private:
    HINSTANCE inst_{};
    HWND hwnd_{},edit_{},browse_{},refresh_{},acl_{},deny_{},sys_{},hidden_{};
    WNDPROC oldEditProc_{};
    float scale_=1.f;
    HFONT font_{};
    HBRUSH editBrush_{},checkBrush_{};
    bool comInit_=false,ready_=false,busy_=false,suppressChange_=false;
    bool draftAcl_=false,draftDeny_=false,draftSystem_=false,draftHidden_=false;
    bool modified_=false,success_=true;
    float mx_=-1.f,my_=-1.f;
    std::wstring selected_,notice_=L"浏览选择目录后自动读取；手动输入路径后按 Enter。";
    security::Status actual_{};
    std::vector<Hit> hits_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteTextFormat> hero_,section_,body_,bold_,small_,tiny_;

    int Px(float n)const{return int(std::lround(n*scale_));}
    float Width()const{RECT r{};GetClientRect(hwnd_,&r);return float(r.right)/scale_;}
    float Height()const{RECT r{};GetClientRect(hwnd_,&r);return float(r.bottom)/scale_;}
    void Place(HWND child,float x,float y,float w,float h){
        if(child)MoveWindow(child,Px(x),Px(y),Px(w),Px(h),TRUE);
    }
    void SyncControls(){
        if(!acl_)return;
        SendMessageW(acl_,BM_SETCHECK,draftAcl_?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(deny_,BM_SETCHECK,draftDeny_?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(sys_,BM_SETCHECK,draftSystem_?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(hidden_,BM_SETCHECK,draftHidden_?BST_CHECKED:BST_UNCHECKED,0);
        for(HWND check:{acl_,deny_,sys_,hidden_})EnableWindow(check,ready_&&!busy_);
        EnableWindow(refresh_,ready_&&!busy_);
        EnableWindow(browse_,!busy_);
    }
    void MakeFormat(ComPtr<IDWriteTextFormat>& out,float size,DWRITE_FONT_WEIGHT weight){
        if(SUCCEEDED(dwrite_->CreateTextFormat(L"Segoe UI",nullptr,weight,
           DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",out.GetAddressOf()))){
            out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    static LRESULT CALLBACK EditWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(self && msg==WM_KEYDOWN && wp==VK_RETURN){
            self->LoadSelected(true);
            return 0;
        }
        return self && self->oldEditProc_?
            CallWindowProcW(self->oldEditProc_,hwnd,msg,wp,lp):
            DefWindowProcW(hwnd,msg,wp,lp);
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
            mm->ptMinTrackSize.x=Px(700);
            mm->ptMinTrackSize.y=Px(490);
            return 0;
        }
        case WM_SIZE:
            if(target_)target_->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));
            LayoutControls();InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        case WM_MOUSEMOVE:{
            TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd_,0};TrackMouseEvent(&t);
            mx_=float(GET_X_LPARAM(lp))/scale_;
            my_=float(GET_Y_LPARAM(lp))/scale_;
            InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        }
        case WM_MOUSELEAVE:mx_=my_=-1;InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:{
            float x=float(GET_X_LPARAM(lp))/scale_;
            float y=float(GET_Y_LPARAM(lp))/scale_;
            if(!busy_)for(auto hit:hits_)if(hit.enabled && hit.rect.contains(x,y)){
                Click(hit.id);break;
            }
            return 0;
        }
        case WM_COMMAND:{
            const UINT id=LOWORD(wp),code=HIWORD(wp);
            if(id==kPathId && code==EN_CHANGE && !suppressChange_ && !busy_){
                ready_=false;selected_.clear();modified_=false;actual_=security::Status{};
                draftAcl_=draftDeny_=draftSystem_=draftHidden_=false;
                notice_=L"路径已修改。按 Enter 载入，再设置权限。";
                SyncControls();InvalidateRect(hwnd_,nullptr,FALSE);
            }else if(code==BN_CLICKED && !busy_){
                if(id==kBrowseId)Browse();
                else if(id==kRefreshId)Refresh();
                else if(id==kAclId)Click(Control::Acl);
                else if(id==kDeleteId)Click(Control::DenyDelete);
                else if(id==kSystemId)Click(Control::System);
                else if(id==kHiddenId)Click(Control::Hidden);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:{
            auto dc=reinterpret_cast<HDC>(wp);
            SetBkColor(dc,RGB(255,255,255));
            SetTextColor(dc,RGB(38,55,80));
            return reinterpret_cast<LRESULT>(checkBrush_);
        }
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
    void LayoutControls(){
        if(!edit_)return;
        const float w=Width();
        const float col=(w-72.f)/2.f;
        Place(edit_,36,95,w-172,33);
        Place(browse_,w-124,95,88,33);
        Place(refresh_,w-126,161,90,29);
        Place(acl_,40,302,col-20,28);
        Place(deny_,40+col,302,col-20,28);
        Place(sys_,40,344,col-20,28);
        Place(hidden_,40+col,344,col-20,28);
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
        draftDeny_=ready_ && actual_.denyDelete;
        draftSystem_=ready_ && actual_.system;
        draftHidden_=ready_ && actual_.hidden;
        modified_=false;
        SyncControls();
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
        if(path.empty()){notice_=L"请先选择文件夹。";ready_=false;AdoptActual();InvalidateRect(hwnd_,nullptr,FALSE);return;}
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
        if(!ready_||busy_)return;
        if(id==Control::Acl)draftAcl_=!draftAcl_;
        else if(id==Control::DenyDelete)draftDeny_=!draftDeny_;
        else if(id==Control::System)draftSystem_=!draftSystem_;
        else if(id==Control::Hidden)draftHidden_=!draftHidden_;
        else if(id==Control::Apply){ApplyDraft();return;}
        modified_=(draftAcl_!=actual_.aclProtected) || (draftDeny_!=actual_.denyDelete) ||
                  (draftSystem_!=actual_.system) || (draftHidden_!=actual_.hidden);
        notice_=modified_?L"设置已预选，磁盘状态尚未更改；点击红色“应用设置”后生效。":
                         L"设置项已与磁盘实际状态对齐。";
        success_=true;SyncControls();InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void ApplyDraft(){
        if(!ready_||busy_)return;
        const bool desiredAcl=draftAcl_,desiredDeny=draftDeny_;
        const bool desiredSystem=draftSystem_,desiredHidden=draftHidden_;
        if(actual_.aclProtected!=desiredAcl || actual_.denyDelete!=desiredDeny){
            if(MessageBoxW(hwnd_,
                L"将仅变更所选的 ACL 继承状态与禁止删除 ACE，不主动重置其他权限。\r\n"
                L"禁止删除规则不保证阻止具有父目录 DELETE_CHILD 权限的用户删除目录。\r\n\r\n确定继续？",
                L"权限变更确认",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
        }
        busy_=true;EnableWindow(edit_,FALSE);SyncControls();
        notice_=L"正在写入 NTFS 设置，请勿关闭程序…";
        InvalidateRect(hwnd_,nullptr,FALSE);
        const auto path=selected_;const auto notify=hwnd_;
        std::thread([path,notify,desiredAcl,desiredDeny,desiredSystem,desiredHidden](){
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
            bool ok=state.error.empty();
            if(!ok)final={false,L"无法读取初始 NTFS 状态："+state.error};
            if(ok && state.aclProtected!=desiredAcl)
                ok=perform(desiredAcl?security::Action::AclProtectOn:security::Action::AclProtectOff,
                           L"ACL 继承保护");
            if(ok && state.denyDelete!=desiredDeny)
                ok=perform(desiredDeny?security::Action::DenyDeleteOn:security::Action::DenyDeleteOff,
                           L"禁止删除");
            if(ok && state.system!=desiredSystem)
                ok=perform(desiredSystem?security::Action::SystemOn:security::Action::SystemOff,L"系统属性 +S");
            if(ok && state.hidden!=desiredHidden)
                ok=perform(desiredHidden?security::Action::HiddenOn:security::Action::HiddenOff,L"隐藏属性 +H");
            if(ok && (state.aclProtected!=desiredAcl || state.denyDelete!=desiredDeny ||
                      state.system!=desiredSystem || state.hidden!=desiredHidden))
                final={false,L"实际状态与目标状态不一致，已保留磁盘读取结果。"};
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
        const unsigned bg=enabled?(primary?(hover?0xAE2E3A:0xC73F4A):0xF9FBFF):0xE9EDF3;
        Shape(r,bg,9,primary?0:0xD9E2EF);
        Text(label,{r.x+10,r.y+8,r.w-20,r.h-12},bold_.Get(),
             enabled?(primary?0xFFFFFF:0x355273):0xA9B3C0,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void Chip(Rect r,const std::wstring& label,bool on){
        Shape(r,ready_?(on?0xEAF7F1:0xF4F6F9):0xF6F7FA,9,
              ready_?(on?0xC8E8D8:0xE2E7ED):0xEBEDF1);
        Dot(r.x+14,r.y+16,4.5f,ready_?(on?0x239469:0xB4BCC8):0xC9CED5);
        Text(label,{r.x+25,r.y+4,r.w-32,23},small_.Get(),0x3A506D);
        Text(!ready_?L"—":on?L"已开启":L"未开启",
             {r.x+25,r.y+25,r.w-32,22},small_.Get(),ready_&&on?0x217A56:0x64778E);
    }
    void Draw(){
        if(!EnsureRenderTarget())return;
        const float w=Width(),h=Height();
        const float margin=18.f,cardW=w-2*margin;
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(scale_,scale_));
        target_->Clear(Color(0xF4F7FB));hits_.clear();

        Text(L"FolderGuard",{24,14,320,35},hero_.Get(),0x20344F);
        Text(L"文件夹权限与属性管理",{330,21,w-358,25},small_.Get(),0x74869B,
             DWRITE_TEXT_ALIGNMENT_TRAILING);

        Shape({margin,55,cardW,90},0xFFFFFF,11,0xE0E7F0);
        Text(L"目标文件夹",{36,63,180,27},section_.Get(),0x263B59);
        Text(notice_,{37,131,w-74,20},tiny_.Get(),success_?0x59738D:0xB44B55);

        Shape({margin,155,cardW,101},0xFFFFFF,11,0xE0E7F0);
        Text(L"当前保护状态",{36,165,260,28},section_.Get(),0x263B59);
        const float gap=8.f,chipW=(w-72.f-3*gap)/4.f;
        Chip({36,196,chipW,48},L"ACL 权限",ready_&&actual_.aclProtected);
        Chip({36+chipW+gap,196,chipW,48},L"禁止删除",ready_&&actual_.denyDelete);
        Chip({36+(chipW+gap)*2,196,chipW,48},L"系统属性 +S",ready_&&actual_.system);
        Chip({36+(chipW+gap)*3,196,chipW,48},L"隐藏属性 +H",ready_&&actual_.hidden);

        Shape({margin,265,cardW,114},0xFFFFFF,11,0xE0E7F0);
        Text(L"设置选项",{36,274,180,27},section_.Get(),0x263B59);
        Text(modified_?L"有未应用的更改":L"与磁盘实际状态同步",
             {w-266,275,228,25},small_.Get(),modified_?0xB74B58:0x71859B,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float col=(w-72.f)/2.f;
        Text(L"仅更改顶层目录 ACL 继承",{63,326,col-25,20},tiny_.Get(),0x75869D);
        Text(L"独立添加 / 移除删除拒绝规则",{63+col,326,col-25,20},tiny_.Get(),0x75869D);
        Text(L"仅控制系统属性",{63,368,col-25,18},tiny_.Get(),0x75869D);
        Text(L"仅控制隐藏属性",{63+col,368,col-25,18},tiny_.Get(),0x75869D);

        Button(Control::Apply,{margin,h-84,cardW,44},
               busy_?L"正在应用设置…":L"应用设置",ready_&&!busy_,true);
        Text(L"v0.0.1  ·  Windows 7 / 10",{24,h-31,310,25},small_.Get(),0x60758F);
        hits_.push_back({Control::GitHub,{w-209,h-32,188,26},true});
        Text(L"GitHub / mrgolftech ↗",{w-225,h-31,205,25},small_.Get(),
             0x3265AC,DWRITE_TEXT_ALIGNMENT_TRAILING);
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
