#include "macos_dock.h"
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <functional>
#include <QString>
#include <QStringList>

static std::function<void(const QString &)> g_openCb;
static NSMutableArray<NSString *> *g_files;

@interface _PDFDockTarget : NSObject
- (void)openFile:(NSMenuItem *)item;
@end

@implementation _PDFDockTarget
- (void)openFile:(NSMenuItem *)item {
    if (g_openCb)
        g_openCb(QString::fromNSString((NSString *)item.representedObject));
}
@end

static _PDFDockTarget *g_target;

// Called by macOS each time the user right-clicks the Dock icon.
// Build a fresh menu from the current file list.
static NSMenu *dockMenuImpl(id, SEL, NSApplication *) {
    NSMenu *top = [[NSMenu alloc] initWithTitle:@""];
    if (!g_files || g_files.count == 0) return top;

    NSMenu *sub = [[NSMenu alloc] initWithTitle:@"Open Recent"];
    for (NSString *path in g_files) {
        NSMenuItem *item = [[NSMenuItem alloc]
            initWithTitle:path.lastPathComponent
                   action:@selector(openFile:)
            keyEquivalent:@""];
        item.target = g_target;
        item.representedObject = path;
        [sub addItem:item];
    }

    NSMenuItem *parent = [[NSMenuItem alloc] initWithTitle:@"Open Recent"
                                                    action:nil
                                             keyEquivalent:@""];
    [parent setSubmenu:sub];
    [top addItem:parent];
    return top;
}

void setupMacOSDockMenu(std::function<void(const QString &)> openCallback)
{
    g_openCb = openCallback;
    g_files  = [[NSMutableArray alloc] init];
    g_target = [[_PDFDockTarget alloc] init];

    id delegate = [NSApp delegate];
    if (!delegate) return;

    SEL sel = @selector(applicationDockMenu:);
    Method m = class_getInstanceMethod([delegate class], sel);
    if (m)
        method_setImplementation(m, (IMP)dockMenuImpl);
    else
        class_addMethod([delegate class], sel, (IMP)dockMenuImpl, "@@:@");
}

void updateMacOSDockMenu(const QStringList &files)
{
    if (!g_files) return;
    [g_files removeAllObjects];
    for (const QString &path : files)
        [g_files addObject:path.toNSString()];
}
