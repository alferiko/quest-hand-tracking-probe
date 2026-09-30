// Standalone diagnostic. No game hooks or game files. Data stays on the device.
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <ctime>

static FILE* events = nullptr;
static void note(const char* fmt, ...) {
    char buf[2048]; va_list args; va_start(args,fmt); vsnprintf(buf,sizeof(buf),fmt,args); va_end(args);
    __android_log_print(ANDROID_LOG_INFO,"QuestHandProbe","%s",buf);
    if(events) { fprintf(events,"%s\n",buf); fflush(events); }
}
static void check(XrResult r,const char* call) {
    if(XR_FAILED(r)) { note("ERROR %s result=%d",call,r); throw std::runtime_error(call); }
}
#define XR(call) check((call),#call)
template<class T> static T proc(XrInstance instance,const char* name) {
    PFN_xrVoidFunction f{}; XR(xrGetInstanceProcAddr(instance,name,&f)); return reinterpret_cast<T>(f);
}
static XrPosef identity() { XrPosef p{}; p.orientation.w=1; return p; }

// Small, dependency-free bitmap dashboard. RGBA bytes match GL_RGBA.
constexpr int W=1280,H=960;
constexpr uint32_t BG=0xff17110c, WHITE=0xffe9e3dd, GRAY=0xffaaa096,
                   GREEN=0xff87ee6a, ORANGE=0xff55baff, RED=0xff7777ff, CYAN=0xffffd775;
struct Canvas {
    std::vector<uint32_t> pixels=std::vector<uint32_t>(W*H,BG);
    void rect(int x,int y,int w,int h,uint32_t c) {
        for(int yy=std::max(0,y);yy<std::min(H,y+h);++yy)
            for(int xx=std::max(0,x);xx<std::min(W,x+w);++xx) pixels[(H-1-yy)*W+xx]=c;
    }
    void line(int x0,int y0,int x1,int y1,uint32_t c) {
        // Clamp diagnostic projections before rasterizing unusual runtime data.
        x0=std::clamp(x0,0,W-1);x1=std::clamp(x1,0,W-1);
        y0=std::clamp(y0,0,H-1);y1=std::clamp(y1,0,H-1);
        int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
        for(;;) {rect(x0-1,y0-1,3,3,c); if(x0==x1&&y0==y1)break; int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
    }
    void text(int x,int y,const std::string& s,uint32_t color=WHITE,int scale=3) {
        // Five column glyphs, bits bottom-to-top within seven rows.
        static const char* chars="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/=?+()";
        static const uint8_t font[][5]={
            {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
            {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
            {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
            {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
            {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
            {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
            {7,8,112,8,7},{97,81,73,69,67},{62,81,73,69,62},{0,66,127,64,0},
            {66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},
            {60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
            {8,8,8,8,8},{0,96,96,0,0},{0,54,54,0,0},{32,16,8,4,2},
            {20,20,20,20,20},{2,1,81,9,6},{8,8,62,8,8},{0,28,34,65,0},{0,65,34,28,0}};
        for(char c:s) {
            const char* f=strchr(chars,c);
            if(c!=' '&&f) {
                for(int col=0;col<5;++col)for(int row=0;row<7;++row)
                    if(font[f-chars][col]&(1<<row))rect(x+col*scale,y+row*scale,scale,scale,color);
            }
            x+=6*scale;
        }
    }
};
struct Tracker {
    XrHandTrackerEXT handle{};
    XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
    XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
    XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    XrResult result=XR_ERROR_FEATURE_UNSUPPORTED;
    int valid=0,tracked=0;
    const char* label(bool hasSource) const {
        if(!handle)return "UNAVAILABLE";
        if(XR_FAILED(result))return "ERROR";
        if(!locations.isActive)return "INACTIVE";
        if(!hasSource||!source.isActive)return "UNKNOWN SOURCE";
        if(source.dataSource==XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT)return "CAMERA";
        if(source.dataSource==XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT)return "CONTROLLER";
        return "UNKNOWN SOURCE";
    }
};
static XrVector3f sub(XrVector3f a,XrVector3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static float dot(XrVector3f a,XrVector3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static XrVector3f unit(XrVector3f a){float l=sqrtf(dot(a,a));return l>0.00001f?XrVector3f{a.x/l,a.y/l,a.z/l}:XrVector3f{};}
static bool usable(const XrHandJointLocationEXT& j) {
    return (j.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)&&std::isfinite(j.pose.position.x)&&std::isfinite(j.pose.position.y)&&std::isfinite(j.pose.position.z);
}

struct Probe {
    android_app* app;
    XrInstance instance{};XrSystemId system{};XrSession session{};XrSpace viewSpace{},localSpace{};
    XrSwapchain swapchain{};XrActionSet actionSet{};XrAction grip{},trigger{},squeeze{};
    XrPath hands[2]{};XrSpace grips[2]{};
    XrActionStatePose gripActive[2]{{XR_TYPE_ACTION_STATE_POSE},{XR_TYPE_ACTION_STATE_POSE}};
    XrSpaceLocation gripLocations[2]{{XR_TYPE_SPACE_LOCATION},{XR_TYPE_SPACE_LOCATION}};
    float triggers[2]{},squeezes[2]{};bool triggerActive[2]{},squeezeActive[2]{};
    XrResult syncResult=XR_SESSION_NOT_FOCUSED;
    EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
    PFN_xrCreateHandTrackerEXT createHand{};PFN_xrDestroyHandTrackerEXT destroyHand{};PFN_xrLocateHandJointsEXT locateHand{};
    PFN_xrResumeSimultaneousHandsAndControllersTrackingMETA resume{};
    bool hasSource=false,hasHands=false,hasMulti=false,multiSupported=false,hasPlus=false;
    bool running=false,exit=false,resumed=false,focused=false;
    XrResult multiResult=XR_ERROR_FEATURE_UNSUPPORTED;
    Tracker trackers[2][2]; // [hand][0=auto, 1=camera only]
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<uint64_t> uploaded;
    uint64_t generation=0,samples=0;XrTime lastSample=0;
    FILE* csv{};Canvas canvas;std::string logPrefix;
    explicit Probe(android_app* a):app(a){}
    ~Probe() {
        if(csv)fclose(csv);
        for(auto& hand:trackers)for(auto& t:hand)if(t.handle&&destroyHand)destroyHand(t.handle);
        for(auto s:grips)if(s)xrDestroySpace(s);
        if(swapchain)xrDestroySwapchain(swapchain);
        if(viewSpace)xrDestroySpace(viewSpace);if(localSpace)xrDestroySpace(localSpace);
        if(session)xrDestroySession(session);if(actionSet)xrDestroyActionSet(actionSet);if(instance)xrDestroyInstance(instance);
        if(display!=EGL_NO_DISPLAY) {eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);eglTerminate(display);}
    }
    void init() {
        const char* dir=app->activity->externalDataPath?app->activity->externalDataPath:app->activity->internalDataPath;
        logPrefix=std::string(dir)+"/probe-"+std::to_string(time(nullptr));
        events=fopen((logPrefix+".txt").c_str(),"w");
        note("Quest Hand Probe v0.1 logs=%s",logPrefix.c_str());
        auto loader=proc<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE,"xrInitializeLoaderKHR");
        XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};li.applicationVM=app->activity->vm;li.applicationContext=app->activity->clazz;
        XR(loader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&li)));
        uint32_t n=0;XR(xrEnumerateInstanceExtensionProperties(nullptr,0,&n,nullptr));
        std::vector<XrExtensionProperties> props(n,{XR_TYPE_EXTENSION_PROPERTIES});XR(xrEnumerateInstanceExtensionProperties(nullptr,n,&n,props.data()));
        auto has=[&](const char* name){return std::any_of(props.begin(),props.end(),[&](auto& p){return strcmp(p.extensionName,name)==0;});};
        for(auto& p:props)note("EXT %s %u",p.extensionName,p.extensionVersion);
        std::vector<const char*> exts={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
        auto optional=[&](const char* name){bool yes=has(name);if(yes)exts.push_back(name);return yes;};
        hasHands=optional(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        hasSource=hasHands&&optional(XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);
        hasMulti=optional(XR_META_SIMULTANEOUS_HANDS_AND_CONTROLLERS_EXTENSION_NAME);
        hasPlus=optional(XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME);
        XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};ai.applicationVM=app->activity->vm;ai.applicationActivity=app->activity->clazz;
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};ci.next=&ai;strcpy(ci.applicationInfo.applicationName,"Quest Hand Probe");ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=exts.size();ci.enabledExtensionNames=exts.data();XR(xrCreateInstance(&ci,&instance));
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};XR(xrGetInstanceProperties(instance,&ip));note("RUNTIME %s version=%llu",ip.runtimeName,(unsigned long long)ip.runtimeVersion);
        XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO};gi.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XR(xrGetSystem(instance,&gi,&system));
        XrSystemHandTrackingPropertiesEXT hp{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
        XrSystemSimultaneousHandsAndControllersPropertiesMETA mp{XR_TYPE_SYSTEM_SIMULTANEOUS_HANDS_AND_CONTROLLERS_PROPERTIES_META};
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};if(hasHands){sp.next=&hp;if(hasMulti)hp.next=&mp;}else if(hasMulti)sp.next=&mp;
        XR(xrGetSystemProperties(instance,system,&sp));hasHands=hasHands&&hp.supportsHandTracking;multiSupported=hasMulti&&mp.supportsSimultaneousHandsAndControllers;
        note("SYSTEM %s hands=%d dataSource=%d multimodal=%d",sp.systemName,hasHands,hasSource,multiSupported);
        auto requirements=proc<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(instance,"xrGetOpenGLESGraphicsRequirementsKHR");
        XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};XR(requirements(instance,system,&req));
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(display,nullptr,nullptr))throw std::runtime_error("eglInitialize");
        const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config{};EGLint count=0;if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)throw std::runtime_error("eglChooseConfig");
        EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
        EGLint pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};surface=eglCreatePbufferSurface(display,config,pa);
        if(!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("eglMakeCurrent");
        XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};binding.display=display;binding.config=config;binding.context=context;
        XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO};si.next=&binding;si.systemId=system;XR(xrCreateSession(instance,&si,&session));
        XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rs.poseInReferenceSpace=identity();rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;XR(xrCreateReferenceSpace(session,&rs,&viewSpace));rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;XR(xrCreateReferenceSpace(session,&rs,&localSpace));
        setupInput();
        if(hasHands) {
            createHand=proc<PFN_xrCreateHandTrackerEXT>(instance,"xrCreateHandTrackerEXT");destroyHand=proc<PFN_xrDestroyHandTrackerEXT>(instance,"xrDestroyHandTrackerEXT");locateHand=proc<PFN_xrLocateHandJointsEXT>(instance,"xrLocateHandJointsEXT");
            for(int h=0;h<2;++h)for(int mode=0;mode<2;++mode) {
                if(mode==1&&!hasSource)continue;
                XrHandTrackingDataSourceEXT sources[]={XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT,XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT};
                XrHandTrackingDataSourceInfoEXT ds{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};ds.requestedDataSources=sources;ds.requestedDataSourceCount=mode?1:2;
                XrHandTrackerCreateInfoEXT hc{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};hc.next=hasSource?&ds:nullptr;hc.hand=h?XR_HAND_RIGHT_EXT:XR_HAND_LEFT_EXT;hc.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
                auto& t=trackers[h][mode];t.result=createHand(session,&hc,&t.handle);note("CREATE hand=%d mode=%d result=%d",h,mode,t.result);
            }
        }
        if(multiSupported)resume=proc<PFN_xrResumeSimultaneousHandsAndControllersTrackingMETA>(instance,"xrResumeSimultaneousHandsAndControllersTrackingMETA");
        XR(xrEnumerateSwapchainFormats(session,0,&n,nullptr));std::vector<int64_t> formats(n);XR(xrEnumerateSwapchainFormats(session,n,&n,formats.data()));
        int64_t format=0;for(auto f:formats)if(f==GL_RGBA8||f==GL_SRGB8_ALPHA8){format=f;if(f==GL_SRGB8_ALPHA8)break;}
        if(!format)throw std::runtime_error("No RGBA swapchain format");
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.usageFlags=XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;sc.format=format;sc.sampleCount=1;sc.width=W;sc.height=H;sc.faceCount=1;sc.arraySize=1;sc.mipCount=1;XR(xrCreateSwapchain(session,&sc,&swapchain));
        XR(xrEnumerateSwapchainImages(swapchain,0,&n,nullptr));images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});XR(xrEnumerateSwapchainImages(swapchain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));uploaded.resize(n,0);
        csv=fopen((logPrefix+".csv").c_str(),"w");
        if(csv)fprintf(csv,"sample,xr_time,focused,multi_result,sync_result,hand,mode,locate_result,hand_active,source_active,source,grip_active,grip_flags,trigger_active,trigger,squeeze_active,squeeze,joint,flags,x,y,z,qx,qy,qz,qw\n");
        note("READY csv=%d",csv!=nullptr);
    }
    XrPath path(const char* str){XrPath p{};XR(xrStringToPath(instance,str,&p));return p;}
    void setupInput() {
        hands[0]=path("/user/hand/left");hands[1]=path("/user/hand/right");
        XrActionSetCreateInfo ac{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy(ac.actionSetName,"probe");strcpy(ac.localizedActionSetName,"Probe");XR(xrCreateActionSet(instance,&ac,&actionSet));
        auto make=[&](const char* name,XrActionType type,XrAction& out){XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};strcpy(a.actionName,name);strcpy(a.localizedActionName,name);a.actionType=type;a.countSubactionPaths=2;a.subactionPaths=hands;XR(xrCreateAction(actionSet,&a,&out));};
        make("grip_pose",XR_ACTION_TYPE_POSE_INPUT,grip);make("trigger",XR_ACTION_TYPE_FLOAT_INPUT,trigger);make("squeeze",XR_ACTION_TYPE_FLOAT_INPUT,squeeze);
        std::vector<XrActionSuggestedBinding> bindings;
        for(int h=0;h<2;++h){std::string base=h?"/user/hand/right":"/user/hand/left";bindings.push_back({grip,path((base+"/input/grip/pose").c_str())});bindings.push_back({trigger,path((base+"/input/trigger/value").c_str())});bindings.push_back({squeeze,path((base+"/input/squeeze/value").c_str())});}
        auto suggest=[&](const char* profile){XrInteractionProfileSuggestedBinding b{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};b.interactionProfile=path(profile);b.countSuggestedBindings=bindings.size();b.suggestedBindings=bindings.data();XR(xrSuggestInteractionProfileBindings(instance,&b));};
        suggest("/interaction_profiles/oculus/touch_controller");if(hasPlus)suggest("/interaction_profiles/meta/touch_controller_plus");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actionSet;XR(xrAttachSessionActionSets(session,&attach));
        for(int h=0;h<2;++h){XrActionSpaceCreateInfo as{XR_TYPE_ACTION_SPACE_CREATE_INFO};as.action=grip;as.subactionPath=hands[h];as.poseInActionSpace=identity();XR(xrCreateActionSpace(session,&as,&grips[h]));}
    }
    void poll() {
        XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER};XrResult r;
        while((r=xrPollEvent(instance,&e))==XR_SUCCESS) {
            if(e.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto* s=reinterpret_cast<XrEventDataSessionStateChanged*>(&e);note("SESSION state=%d",s->state);focused=s->state==XR_SESSION_STATE_FOCUSED;
                if(s->state==XR_SESSION_STATE_READY){XrSessionBeginInfo b{XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;XR(xrBeginSession(session,&b));running=true;if(resume){XrSimultaneousHandsAndControllersTrackingResumeInfoMETA mi{XR_TYPE_SIMULTANEOUS_HANDS_AND_CONTROLLERS_TRACKING_RESUME_INFO_META};multiResult=resume(session,&mi);note("MULTIMODAL resume=%d",multiResult);}}
                if(s->state==XR_SESSION_STATE_STOPPING){XR(xrEndSession(session));running=false;}
                if(s->state==XR_SESSION_STATE_EXITING||s->state==XR_SESSION_STATE_LOSS_PENDING)exit=true;
            } else if(e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)exit=true;
            else if(e.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)for(int h=0;h<2;++h){XrInteractionProfileState p{XR_TYPE_INTERACTION_PROFILE_STATE};XR(xrGetCurrentInteractionProfile(session,hands[h],&p));char name[XR_MAX_PATH_LENGTH]="none";uint32_t n=0;if(p.interactionProfile)XR(xrPathToString(instance,p.interactionProfile,sizeof(name),&n,name));note("PROFILE hand=%d %s",h,name);}
            e={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(r!=XR_EVENT_UNAVAILABLE)check(r,"xrPollEvent");
    }
    void sample(XrTime time) {
        XrActiveActionSet active{actionSet,XR_NULL_PATH};XrActionsSyncInfo sy{XR_TYPE_ACTIONS_SYNC_INFO};sy.countActiveActionSets=1;sy.activeActionSets=&active;syncResult=xrSyncActions(session,&sy);
        for(int h=0;h<2;++h) {
            gripActive[h]={XR_TYPE_ACTION_STATE_POSE};gripLocations[h]={XR_TYPE_SPACE_LOCATION};triggers[h]=squeezes[h]=0;triggerActive[h]=squeezeActive[h]=false;
            if(syncResult==XR_SUCCESS){XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=hands[h];get.action=grip;
                if(XR_SUCCEEDED(xrGetActionStatePose(session,&get,&gripActive[h]))&&gripActive[h].isActive)xrLocateSpace(grips[h],localSpace,time,&gripLocations[h]);
                XrActionStateFloat v{XR_TYPE_ACTION_STATE_FLOAT};get.action=trigger;if(XR_SUCCEEDED(xrGetActionStateFloat(session,&get,&v))&&v.isActive){triggers[h]=v.currentState;triggerActive[h]=true;}
                v={XR_TYPE_ACTION_STATE_FLOAT};get.action=squeeze;if(XR_SUCCEEDED(xrGetActionStateFloat(session,&get,&v))&&v.isActive){squeezes[h]=v.currentState;squeezeActive[h]=true;}
            }
            for(int mode=0;mode<2;++mode){auto& t=trackers[h][mode];t.valid=t.tracked=0;t.source={XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};t.locations={XR_TYPE_HAND_JOINT_LOCATIONS_EXT};t.locations.next=hasSource?&t.source:nullptr;t.locations.jointCount=XR_HAND_JOINT_COUNT_EXT;t.locations.jointLocations=t.joints;memset(t.joints,0,sizeof(t.joints));
                if(t.handle){XrHandJointsLocateInfoEXT l{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};l.baseSpace=localSpace;l.time=time;t.result=locateHand(t.handle,&l,&t.locations);}
                if(XR_SUCCEEDED(t.result)&&t.locations.isActive)for(auto& j:t.joints){if(usable(j))++t.valid;if(usable(j)&&(j.locationFlags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT))++t.tracked;}
            }
        }
        if(!lastSample||time-lastSample>=100000000){lastSample=time;++samples;dashboard();++generation;
            for(int h=0;h<2;++h)for(int m=0;m<2;++m){auto& t=trackers[h][m];
                if(samples%10==1)note("SAMPLE %llu hand=%d mode=%d %s valid=%d tracked=%d grip=%d flags=%llu trigger=%.2f squeeze=%.2f focus=%d multi=%d",(unsigned long long)samples,h,m,t.label(hasSource),t.valid,t.tracked,gripActive[h].isActive,(unsigned long long)gripLocations[h].locationFlags,triggers[h],squeezes[h],focused,multiResult);
                // Raw logging is bounded to 30 minutes at 10 Hz; UI remains live.
                if(csv&&samples<=18000)for(int j=0;j<XR_HAND_JOINT_COUNT_EXT;++j){auto& v=t.joints[j];fprintf(csv,"%llu,%lld,%d,%d,%d,%d,%d,%d,%u,%u,%d,%u,%llu,%d,%.5f,%d,%.5f,%d,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",(unsigned long long)samples,(long long)time,focused,multiResult,syncResult,h,m,t.result,t.locations.isActive,t.source.isActive,int(t.source.dataSource),gripActive[h].isActive,(unsigned long long)gripLocations[h].locationFlags,triggerActive[h],triggers[h],squeezeActive[h],squeezes[h],j,(unsigned long long)v.locationFlags,v.pose.position.x,v.pose.position.y,v.pose.position.z,v.pose.orientation.x,v.pose.orientation.y,v.pose.orientation.z,v.pose.orientation.w);}
            }
            if(csv)fflush(csv);
        }
    }
    void skeleton(Tracker& t,int ox,int oy,uint32_t color) {
        if(!t.locations.isActive||XR_FAILED(t.result))return;
        if(!usable(t.joints[1])||!usable(t.joints[7])||!usable(t.joints[12])||!usable(t.joints[22]))return;
        auto origin=t.joints[1].pose.position;
        auto up=unit(sub(t.joints[12].pose.position,origin));
        auto side=sub(t.joints[7].pose.position,t.joints[22].pose.position);float along=dot(side,up);side=unit({side.x-along*up.x,side.y-along*up.y,side.z-along*up.z});
        auto xy=[&](int j){auto d=sub(t.joints[j].pose.position,origin);float x=dot(d,side),y=dot(d,up);return std::array<int,2>{ox+int(std::clamp(x,-.24f,.24f)*1000),oy-int(std::clamp(y,-.02f,.24f)*1000)};};
        const int parent[26]={1,-1,1,2,3,4,1,6,7,8,9,1,11,12,13,14,1,16,17,18,19,1,21,22,23,24};
        for(int j=0;j<26;++j)if(usable(t.joints[j])){auto p=xy(j);int b=parent[j];if(b>=0&&usable(t.joints[b])){auto q=xy(b);canvas.line(p[0],p[1],q[0],q[1],color);}canvas.rect(p[0]-3,p[1]-3,7,7,(t.joints[j].locationFlags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)?WHITE:GRAY);}
    }
    void dashboard() {
        std::fill(canvas.pixels.begin(),canvas.pixels.end(),BG);char b[200];
        canvas.text(28,22,"QUEST HAND PROBE 0.1",WHITE,4);
        snprintf(b,sizeof(b),"MULTIMODAL %s  SOURCE API %s  FOCUS %s",multiResult==XR_SUCCESS?"ON":"OFF",hasSource?"YES":"NO",focused?"YES":"NO");canvas.text(28,66,b,multiResult==XR_SUCCESS?GREEN:ORANGE);
        canvas.text(28,104,"GREEN CAMERA / ORANGE CONTROLLER / GRAY UNKNOWN",GRAY,3);
        for(int h=0;h<2;++h){int x=28+h*632;canvas.text(x,152,h?"RIGHT HAND":"LEFT HAND",CYAN,4);
            snprintf(b,sizeof(b),"CTRL %s  TRIG %.2f GRIP %.2f",gripActive[h].isActive?"ON":"OFF",triggers[h],squeezes[h]);canvas.text(x,192,b,GRAY,2);
            for(int mode=0;mode<2;++mode){int y=230+mode*326;auto& t=trackers[h][mode];const char* label=t.label(hasSource);uint32_t c=strcmp(label,"CAMERA")==0?GREEN:strcmp(label,"CONTROLLER")==0?ORANGE:GRAY;
                canvas.text(x,y,std::string(mode?"CAMERA ONLY: ":"AUTO: ")+label,c,2);
                snprintf(b,sizeof(b),"VALID %d/26 TRACKED %d/26",t.valid,t.tracked);canvas.text(x,y+25,b,GRAY,2);
                skeleton(t,x+296,y+292,c);
            }
        }
        canvas.text(28,886,"SCHEMATIC PALM VIEW / RAW JOINTS LOGGED AT 10 HZ",GRAY,2);
        snprintf(b,sizeof(b),"SAMPLE %llu  CSV %s  MOVE FINGERS WHILE HOLDING CONTROLLERS",(unsigned long long)samples,csv?(samples<=18000?"ON":"LIMIT"):"ERROR");canvas.text(28,919,b,csv?GRAY:RED,2);
    }
    void frame() {
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};XR(xrWaitFrame(session,&wi,&fs));XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};XR(xrBeginFrame(session,&bi));
        sample(fs.predictedDisplayTime);
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=viewSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose=identity();quad.pose.position.z=-1.2f;quad.size={1.28f,.96f};quad.subImage.swapchain=swapchain;quad.subImage.imageRect={{0,0},{W,H}};
        if(fs.shouldRender){uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XR(xrAcquireSwapchainImage(swapchain,&ai,&index));XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;XR(xrWaitSwapchainImage(swapchain,&wait));
            if(uploaded[index]!=generation){glBindTexture(GL_TEXTURE_2D,images[index].image);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,canvas.pixels.data());GLenum e=glGetError();if(e!=GL_NO_ERROR){note("GL upload error=%u",e);throw std::runtime_error("dashboard upload");}glFlush();uploaded[index]=generation;}
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage(swapchain,&ri));}
        const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=fs.shouldRender?1:0;end.layers=fs.shouldRender?layers:nullptr;XR(xrEndFrame(session,&end));
    }
};
static void command(android_app* app,int32_t cmd){auto* p=static_cast<Probe*>(app->userData);if(cmd==APP_CMD_RESUME)p->resumed=true;if(cmd==APP_CMD_PAUSE)p->resumed=false;}
void android_main(android_app* app) {
    JNIEnv* env=nullptr;app->activity->vm->AttachCurrentThread(&env,nullptr);
    {Probe probe(app);app->userData=&probe;app->onAppCmd=command;
        try {probe.init();while(!app->destroyRequested&&!probe.exit){int eventsCount;android_poll_source* source;int timeout=probe.running?0:50;
            while(ALooper_pollOnce(timeout,nullptr,&eventsCount,reinterpret_cast<void**>(&source))>=0){if(source)source->process(app,source);if(app->destroyRequested)break;timeout=0;}
            if(app->destroyRequested)break;probe.poll();if(probe.running&&!probe.exit)probe.frame();}
        }catch(const std::exception& e){note("FATAL %s",e.what());ANativeActivity_finish(app->activity);}
        app->userData=nullptr;
    }
    note("EXIT");if(events){fclose(events);events=nullptr;}app->activity->vm->DetachCurrentThread();
}
