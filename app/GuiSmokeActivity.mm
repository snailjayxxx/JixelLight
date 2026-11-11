#import <Foundation/Foundation.h>
// Controlled smoke diagnostic only. Keep the explicitly requested export active through
// ordinary database/cache teardown. Idle system sleep and preferences stay unchanged.
namespace {
struct Activity {
    id token=nil;
    ~Activity(){if(token){[[NSProcessInfo processInfo] endActivity:token];[token release];}}
};
Activity activity;
}
void beginGuiSmokeExportActivity(){if(!activity.token)activity.token=[[[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep reason:@"JixelLight controlled export validation"] retain];}
