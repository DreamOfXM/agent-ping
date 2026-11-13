#import <Cocoa/Cocoa.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "platform/textfield.h"

// 搜索框的原生 NSTextField 桥（动机见 textfield.h 头注释）。
//
// 只接管「正在聚焦的那颗框」：焦点进 → show（建/复用 + 设框 + firstResponder），
// 焦点出 → hide（摘下视图）。文字合成期（marked text 还挂在输入法里）不同步——
// 否则拼音字母会先漏进查询串再被 commit 冲掉，列表会闪一下假结果。

namespace {

void (*g_change_cb)(const char*, int) = nullptr;
void (*g_done_cb)(bool, int) = nullptr;
int g_tag = 0;

BOOL composing(NSTextField* f) {
    NSTextView* editor = (NSTextView*)f.currentEditor;
    return editor && editor.markedRange.length > 0;
}

}  // namespace

@interface APFieldBox : NSTextField
@end

@implementation APFieldBox
- (void)insertNewline:(id)sender {
    // 回车＝收工（面板语义：焦点交还，查询串保持）。组词中的回车由输入法先吃，
    // 走不到这里。
    if (g_done_cb) g_done_cb(true, g_tag);
}
- (void)cancelOperation:(id)sender {
    if (g_done_cb) g_done_cb(false, g_tag);
}
- (NSView*)hitTest:(NSPoint)p {
    // 右端 ~34pt 是面板画的 ✕ 清除钮：让点击落到下面的 GL 视图，别被本字段吞掉。
    // hitTest 收到的是 superview 坐标系的点，先翻进自己的。
    const NSPoint lp = [self convertPoint:p fromView:self.superview];
    if (lp.x > self.bounds.size.width - 34.0) return nil;
    return [super hitTest:p];
}
@end

@interface APFieldDelegate : NSObject <NSTextFieldDelegate>
@end

@implementation APFieldDelegate
- (void)controlTextDidChange:(NSNotification*)note {
    NSTextField* f = (NSTextField*)note.object;
    if (!f || composing(f) || !g_change_cb) return;
    g_change_cb(f.stringValue.UTF8String, g_tag);
}
@end

namespace {

APFieldBox* g_field = nil;
APFieldDelegate* g_delegate = nil;

}  // namespace

void ap_field_set_change_cb(void (*cb)(const char*, int)) { g_change_cb = cb; }
void ap_field_set_done_cb(void (*cb)(bool, int)) { g_done_cb = cb; }

bool ap_field_active(void) { return g_field != nil; }

void ap_field_set_text(const char* text) {
    if (!g_field) return;
    g_field.stringValue = @(text ? text : "");
}

void ap_field_show(void* glfw_window, float x, float y, float w, float h,
                   const char* placeholder, const char* text, int tag) {
    NSWindow* win = glfw_window ? (NSWindow*)glfwGetCocoaWindow((GLFWwindow*)glfw_window) : nil;
    if (!win) return;
    NSView* content = win.contentView;
    if (!content) return;
    if (!g_delegate) g_delegate = [[APFieldDelegate alloc] init];
    if (!g_field) {
        g_field = [[APFieldBox alloc] init];
        g_field.bordered = NO;
        g_field.drawsBackground = NO;   // 玻璃底是面板画的
        g_field.focusRingType = NSFocusRingTypeNone;
        g_field.delegate = g_delegate;
        g_field.font = [NSFont systemFontOfSize:13.0];
        g_field.textColor = [NSColor colorWithCalibratedRed:240.0 / 255.0 green:242.0 / 255.0
                                                     blue:247.0 / 255.0 alpha:1.0];
        // PlaceholderColor 走 k31Faint（#B0BAC8）一档
        g_field.placeholderAttributedString = nil;   // 每次 show 重设
        [content addSubview:g_field];
    }
    g_tag = tag;
    NSMutableAttributedString* ph =
        [[NSMutableAttributedString alloc] initWithString:@(placeholder ? placeholder : "")];
    [ph addAttribute:NSForegroundColorAttributeName
               value:[NSColor colorWithCalibratedRed:176.0 / 255.0 green:186.0 / 255.0
                                               blue:200.0 / 255.0 alpha:1.0]
               range:NSMakeRange(0, ph.length)];
    [ph addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:13.0]
               range:NSMakeRange(0, ph.length)];
    g_field.placeholderAttributedString = ph;
    g_field.stringValue = @(text ? text : "");
    g_field.editable = YES;
    g_field.selectable = YES;
    ap_field_frame(glfw_window, x, y, w, h);
    [win makeFirstResponder:g_field];
}

void ap_field_frame(void* glfw_window, float x, float y, float w, float h) {
    if (!g_field || !g_field.superview) return;
    NSView* content = g_field.superview;
    const CGFloat ch = content.bounds.size.height;
    // ImGui 空间左上原点；NSView 底左原点。x 再让 2pt：贴着面板画的玻璃框内沿。
    g_field.frame = NSMakeRect((CGFloat)x + 2.0, (CGFloat)ch - (CGFloat)y - (CGFloat)h + 3.0,
                               (CGFloat)w - 36.0, (CGFloat)h - 6.0);
    // 宽度扣掉的 36pt = 右端 ✕ 区（hitTest 让路的同一段）。
}

void ap_field_hide(void) {
    if (!g_field) return;
    NSWindow* win = g_field.window;
    if (win && win.firstResponder == g_field.currentEditor) [win makeFirstResponder:nil];
    [g_field removeFromSuperview];
    g_field = nil;   // 下次 show 重建，别留 stale frame
    g_tag = 0;
}
