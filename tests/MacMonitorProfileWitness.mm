#import <AppKit/AppKit.h>
#include <QByteArray>
#include <QWindow>

// Independent, read-only AppKit witness. In pinned Qt6.8.3 Cocoa winId is the
// NSView, so this follows the actual window's screen without a name/geometry map.
QByteArray nativeScreenIccWitness(QWindow *window) {
    if(!window)return {};
    @autoreleasepool {
        NSView *view=reinterpret_cast<NSView *>(window->winId());
        NSScreen *screen=view.window.screen;
        NSData *data=screen.colorSpace.ICCProfileData;
        if(!data||data.length==0||data.length>32*1024*1024)return {};
        return QByteArray(static_cast<const char *>(data.bytes),qsizetype(data.length));
    }
}
