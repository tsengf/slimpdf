#include "macos_about.h"
#import <AppKit/AppKit.h>

void showMacOSAboutPanel(const QString &version, const QString &description,
                        const QString &credits, const QString &license,
                        const QString &website)
{
    NSString *url = website.toNSString();
    NSString *text = [NSString stringWithFormat:@"%@\n\n%@\n\n%@\n\n%@",
        description.toNSString(), credits.toNSString(), license.toNSString(), url];
    NSMutableAttributedString *content = [[NSMutableAttributedString alloc] initWithString:text];
    [content addAttribute:NSLinkAttributeName value:[NSURL URLWithString:url]
                   range:[text rangeOfString:url]];
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName: @"SlimPDF",
        NSAboutPanelOptionApplicationVersion: version.toNSString(),
        NSAboutPanelOptionVersion: @"",
        NSAboutPanelOptionCredits: content
    }];
    [NSApp activateIgnoringOtherApps:YES];
}
