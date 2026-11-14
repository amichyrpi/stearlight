#pragma once
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <openvr.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "standable-texture.h"
#include "standable-settings.h"

// Present the existing Windows settings window, without another compositor
// or Xwayland process. SteamVR owns the overlay's positioning and controls.
class StandableWindow {
    Display* display=nullptr;
    Window window=0;
    vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t thumbnail=vr::k_ulOverlayHandleInvalid;
    std::chrono::steady_clock::time_point nextDiscovery{};
    bool shown=false;
    bool closing=false;
    std::chrono::steady_clock::time_point closeAt{};
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> submitted;
    StandableTexture texture;
    int submittedWidth=0,submittedHeight=0;
    int inputWidth=0,inputHeight=0;
    bool displayReported=false;
    vr::EVROverlayError lastError=vr::VROverlayError_None;
    inline static Display* owned=nullptr;
    inline static XErrorHandler previous=nullptr;
    static int error(Display* d,XErrorEvent* e) {
        return d==owned?0:previous?previous(d,e):0;
    }
    void icon(vr::IVROverlay* api) {
        Atom type=0;int format=0;unsigned long count=0,remaining=0;unsigned char* data=nullptr;
        if(XGetWindowProperty(display,window,XInternAtom(display,"_NET_WM_ICON",False),
                0,1024*1024,False,XA_CARDINAL,&type,&format,&count,&remaining,&data)!=Success)return;
        if(data&&type==XA_CARDINAL&&format==32) {
            auto* values=reinterpret_cast<unsigned long*>(data);
            unsigned long best=0,offset=0;
            for(unsigned long i=0;i+2<=count;) {
                auto w=values[i],h=values[i+1];
                if(!w||!h||w>4096||h>4096||w*h>count-i-2)break;
                if(w<=512&&h<=512&&w*h>best){best=w*h;offset=i;}
                i+=2+w*h;
            }
            if(best){
                std::vector<unsigned char> rgba(best*4);
                for(unsigned long i=0;i<best;++i){auto p=values[offset+2+i];
                    rgba[i*4]=p>>16;rgba[i*4+1]=p>>8;rgba[i*4+2]=p;rgba[i*4+3]=p>>24;}
                auto result=api->SetOverlayRaw(thumbnail,rgba.data(),values[offset],values[offset+1],4);
                std::printf("QUEST_STANDABLE_ICON %s\n",api->GetOverlayErrorNameFromEnum(result));
            }
        }
        if(data)XFree(data);
    }
    static void stopSettings() {
        // A Windows GUI can minimize to its tray instead of exiting on
        // WM_DELETE_WINDOW. Close only the one owned settings process;
        // never signal its shared Wine server or the tracker driver host.
        quest_release_standable_settings();
    }
    Window find(Window parent,int depth) {
        if(depth>2)return 0;
        char* name=nullptr;
        if(XFetchName(display,parent,&name)&&name) {
            bool match=std::strstr(name,"Standable")!=nullptr;
            XFree(name);
            XWindowAttributes a{};
            if(match&&XGetWindowAttributes(display,parent,&a)&&a.width>100&&a.height>100)return parent;
        }
        Window root{},p{},*children=nullptr;unsigned count=0;Window result=0;
        if(XQueryTree(display,parent,&root,&p,&children,&count)) {
            for(unsigned i=0;i<count&&!result;++i)result=find(children[i],depth+1);
            if(children)XFree(children);
        }
        return result;
    }
    bool event(vr::IVROverlay* api,const vr::VREvent_t& event,int width,int height) {
        if(event.eventType==vr::VREvent_OverlayClosed) {
            // Close only the settings GUI through its normal window
            // protocol; the startup add-on keeps its trackers running.
            XEvent request{};request.xclient.type=ClientMessage;
            request.xclient.display=display;request.xclient.window=window;
            request.xclient.message_type=XInternAtom(display,"WM_PROTOCOLS",False);
            request.xclient.format=32;
            request.xclient.data.l[0]=XInternAtom(display,"WM_DELETE_WINDOW",False);
            request.xclient.data.l[1]=CurrentTime;
            XSendEvent(display,window,False,NoEventMask,&request);
            XFlush(display);
            api->HideOverlay(overlay);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_VisibleInDashboard,false);
            api->ShowDashboard("valve.steam.gamepadui.main");
            closing=true;closeAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            std::puts("QUEST_STANDABLE_WINDOW_CLOSE_REQUESTED");
            return true;
        }
        if(event.eventType!=vr::VREvent_MouseMove&&event.eventType!=vr::VREvent_MouseButtonDown&&
           event.eventType!=vr::VREvent_MouseButtonUp)return false;
        if(!width||!height)return false;
        int x=std::clamp(int(event.data.mouse.x),0,width-1);
        int y=std::clamp(height-1-int(event.data.mouse.y),0,height-1);
        XEvent out{};
        if(event.eventType==vr::VREvent_MouseMove) {
            out.xmotion.type=MotionNotify;out.xmotion.display=display;out.xmotion.window=window;
            out.xmotion.root=DefaultRootWindow(display);out.xmotion.x=x;out.xmotion.y=y;
            out.xmotion.same_screen=True;
            XSendEvent(display,window,True,PointerMotionMask,&out);
        } else {
            XSetInputFocus(display,window,RevertToParent,CurrentTime);
            out.xbutton.type=event.eventType==vr::VREvent_MouseButtonDown?ButtonPress:ButtonRelease;
            out.xbutton.display=display;out.xbutton.window=window;out.xbutton.root=DefaultRootWindow(display);
            out.xbutton.x=x;out.xbutton.y=y;out.xbutton.same_screen=True;
            out.xbutton.button=event.data.mouse.button==vr::VRMouseButton_Right?Button3:Button1;
            XSendEvent(display,window,True,out.type==ButtonPress?ButtonPressMask:ButtonReleaseMask,&out);
        }
        XFlush(display);
        return true;
    }
    void input(vr::IVROverlay* api,int width,int height) {
        vr::VREvent_t next{};
        while(api->PollNextOverlayEvent(overlay,&next,sizeof(next)))event(api,next,width,height);
    }
public:
    bool handleEvent(vr::VROverlayHandle_t handle,const vr::VREvent_t& next) {
        if(!display||overlay==vr::k_ulOverlayHandleInvalid||
           (handle!=overlay&&handle!=thumbnail))return false;
        return event(vr::VROverlay(),next,inputWidth,inputHeight);
    }
    ~StandableWindow(){if(display){XCloseDisplay(display);owned=nullptr;XSetErrorHandler(previous);}}
    void close() {
        if(overlay!=vr::k_ulOverlayHandleInvalid){vr::VROverlay()->DestroyOverlay(overlay);overlay=vr::k_ulOverlayHandleInvalid;}
        if(thumbnail!=vr::k_ulOverlayHandleInvalid){vr::VROverlay()->DestroyOverlay(thumbnail);thumbnail=vr::k_ulOverlayHandleInvalid;}
        std::vector<unsigned char>().swap(pixels);
        std::vector<unsigned char>().swap(submitted);
        submittedWidth=submittedHeight=0;
        shown=false;
    }
    void poll() {
        if(closing){
            if(std::chrono::steady_clock::now()>=closeAt){stopSettings();close();window=0;closing=false;std::puts("QUEST_STANDABLE_WINDOW_CLOSED");}
            return;
        }
        if(!display){display=XOpenDisplay(":0");if(!display){if(!displayReported)std::puts("QUEST_STANDABLE_X11_UNAVAILABLE");displayReported=true;return;}owned=display;previous=XSetErrorHandler(error);}
        XWindowAttributes a{};
        if(!window){
            auto now=std::chrono::steady_clock::now();
            if(now<nextDiscovery)return;
            nextDiscovery=now+std::chrono::seconds(1);
            window=find(DefaultRootWindow(display),0);
        }
        if(!window)return;
        if(!XGetWindowAttributes(display,window,&a)){window=0;close();return;}
        if(a.width<1||a.height<1||a.width>4096||a.height>4096)return;
        if(a.map_state!=IsViewable){close();window=0;return;}
        auto* api=vr::VROverlay();
        if(overlay==vr::k_ulOverlayHandleInvalid) {
            auto created=api->CreateDashboardOverlay("stearlight.standable.settings","Standable",&overlay,&thumbnail);
            if(created!=vr::VROverlayError_None){if(created!=lastError)std::printf("QUEST_STANDABLE_WINDOW_CREATE %s\n",api->GetOverlayErrorNameFromEnum(created));lastError=created;return;}
            api->SetOverlayWidthInMeters(overlay,1.2f);
            api->SetOverlayInputMethod(overlay,vr::VROverlayInputMethod_Mouse);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_VisibleInDashboard,true);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_EnableControlBar,true);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_EnableControlBarKeyboard,true);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_EnableControlBarClose,true);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_EnableClickStabilization,true);
            api->SetOverlayFlag(overlay,vr::VROverlayFlags_IsPremultiplied,true);
            icon(api);
            std::puts("QUEST_STANDABLE_WINDOW_CREATED");
        }
        vr::HmdVector2_t scale{{float(a.width),float(a.height)}};
        inputWidth=a.width;inputHeight=a.height;
        api->SetOverlayMouseScale(overlay,&scale);input(api,a.width,a.height);
        if(closing)return;
        // Retain input/close handling, but never read pixels for a hidden tab.
        if(shown&&!api->IsOverlayVisible(overlay))return;
        XImage* image=XGetImage(display,window,0,0,a.width,a.height,AllPlanes,ZPixmap);
        if(!image)return;
        pixels.resize(size_t(a.width)*a.height*4);
        const bool packed=image->bits_per_pixel==32&&image->byte_order==LSBFirst&&
            image->red_mask==0xff0000&&image->green_mask==0xff00&&image->blue_mask==0xff;
        for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x) {
            // The normal Xvfb image is packed BGRA. Avoid an Xlib indirect
            // function call for every pixel; retain its portable fallback.
            unsigned long value;
            if(packed){uint32_t pixel;std::memcpy(&pixel,image->data+y*image->bytes_per_line+x*4,4);value=pixel;}
            else value=XGetPixel(image,x,y);
            auto i=(size_t(y)*a.width+x)*4;
            pixels[i]=(value>>16)&255;pixels[i+1]=(value>>8)&255;pixels[i+2]=value&255;pixels[i+3]=255;
        }
        XDestroyImage(image);
        // Retain the existing surface for unchanged settings frames. In
        // particular, do not repeatedly show/reposition the dashboard window.
        if(submittedWidth==a.width&&submittedHeight==a.height&&submitted==pixels)return;
        if(!texture.update(pixels.data(),a.width,a.height)){
            if(!displayReported)std::puts("QUEST_STANDABLE_VULKAN_UPLOAD_FAILED");displayReported=true;return;
        }
        auto data=texture.frame();
        vr::Texture_t frame{&data,vr::TextureType_Vulkan,vr::ColorSpace_Gamma};
        auto result=api->SetOverlayTexture(overlay,&frame);
        if(result==vr::VROverlayError_None){
            submitted=pixels;submittedWidth=a.width;submittedHeight=a.height;
            if(!shown){api->ShowDashboard("stearlight.standable.settings");shown=true;std::puts("QUEST_STANDABLE_WINDOW_READY");}
        } else if(result!=lastError)std::printf("QUEST_STANDABLE_WINDOW_PIXELS %s\n",api->GetOverlayErrorNameFromEnum(result));
        lastError=result;
    }
};
