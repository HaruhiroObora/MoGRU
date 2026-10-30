// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <Carbon/Carbon.h>
#include <objc/message.h>

#include <cmath>

#import "renderer/MoGRUCandidateUI-Swift.h"

#include "absl/log/log.h"
#include "base/coordinates.h"
#include "base/coordinates.h"
#include "protocol/commands.pb.h"
#include "renderer/mac/CandidateWindow.h"

@interface MoGRUCandidateClickHandler : NSObject <MoGRUCandidateSelectionDelegate>
@property(nonatomic, assign) mozc::client::SendCommandInterface *commandSender;
@end

@implementation MoGRUCandidateClickHandler
- (void)candidateView:(MoGRUCandidateView *)view didSelectCandidateWithID:(int32_t)candidateID {
  if (!self.commandSender) {
    return;
  }
  mozc::commands::SessionCommand command;
  command.set_type(mozc::commands::SessionCommand::SELECT_CANDIDATE);
  command.set_id(candidateID);
  mozc::commands::Output output;
  self.commandSender->SendCommand(command, &output);
}
@end

namespace mozc {
namespace renderer {
namespace mac {

CandidateWindow::CandidateWindow() : command_sender_(nullptr) {}

CandidateWindow::~CandidateWindow() {}

void CandidateWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  DLOG(INFO) << "CandidateWindow::SetSendCommandInterface()";
  command_sender_ = send_command_interface;

  if (view_) {
    MoGRUCandidateView *candidate_view = (MoGRUCandidateView *)view_;
    MoGRUCandidateClickHandler *handler = (MoGRUCandidateClickHandler *)candidate_view.selectionDelegate;
    handler.commandSender = send_command_interface;
  }
}

void CandidateWindow::InitWindow() {
  RendererBaseWindow::InitWindow();
  [window_ setStyleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel];
  [window_ setBackgroundColor:NSColor.clearColor];
  [window_ setOpaque:NO];
  [window_ setHasShadow:YES];
}
mozc::Rect CandidateWindow::GetCandidateRowRect(int index) const {
  if (!view_) {
    return mozc::Rect();
  }
  MoGRUCandidateView *candidate_view = (MoGRUCandidateView *)view_;
  const NSRect frame = [candidate_view frameForCandidateAt:index];
  const NSSize size = candidate_view.preferredSize;
  return mozc::Rect(frame.origin.x, size.height - NSMaxY(frame),
                    frame.size.width, frame.size.height);
}

void CandidateWindow::Show() {
  if (!window_) {
    InitWindow();
  }
  [window_ orderFrontRegardless];
}

void CandidateWindow::SetCandidateWindow(const commands::CandidateWindow& candidate_window) {
  DLOG(INFO) << "CandidateWindow::SetCandidateWindow";
  if (candidate_window.candidate_size() == 0) {
    return;
  }

  if (!window_) {
    InitWindow();
  }
  NSMutableArray<MoGRUCandidateItem *> *items = [NSMutableArray array];
  for (const auto &candidate : candidate_window.candidate()) {
    const auto &annotation = candidate.annotation();
    std::string value = candidate.value();
    if (annotation.has_prefix()) {
      value.insert(0, annotation.prefix());
    }
    if (annotation.has_suffix()) {
      value.append(annotation.suffix());
    }
    MoGRUCandidateItem *item = [[MoGRUCandidateItem alloc]
        initWithIndex:candidate.index()
        candidateID:candidate.id()
        text:[NSString stringWithUTF8String:value.c_str()]
        shortcut:[NSString stringWithUTF8String:annotation.shortcut().c_str()]
        descriptionText:[NSString stringWithUTF8String:annotation.description().c_str()]
        hasSubCandidates:candidate_window.has_sub_candidate_window() &&
                         candidate_window.has_focused_index() &&
                         candidate.index() == candidate_window.focused_index()];
    [items addObject:item];
  }

  NSMutableArray<NSString *> *footer_parts = [NSMutableArray array];
  if (candidate_window.has_footer()) {
    const auto &footer = candidate_window.footer();
    if (footer.has_label()) {
      [footer_parts addObject:[NSString stringWithUTF8String:footer.label().c_str()]];
    }
    if (footer.has_sub_label()) {
      [footer_parts addObject:[NSString stringWithUTF8String:footer.sub_label().c_str()]];
    }
    if (footer.index_visible() && candidate_window.has_focused_index()) {
      [footer_parts addObject:[NSString stringWithFormat:@"%u/%u",
          candidate_window.focused_index() + 1, candidate_window.size()]];
    }
  }

  MoGRUCandidateView *candidate_view = (MoGRUCandidateView *)view_;
  [candidate_view updateWith:items
               focusedIndex:candidate_window.has_focused_index()
                   ? candidate_window.focused_index() : -1
                     footer:[footer_parts componentsJoinedByString:@"  "]];
  const NSSize size = candidate_view.preferredSize;
  ResizeWindow(std::ceil(size.width), std::ceil(size.height));
}

void CandidateWindow::ResetView() {
  DLOG(INFO) << "CandidateWindow::ResetView()";
  MoGRUCandidateView *candidate_view = [[MoGRUCandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  MoGRUCandidateClickHandler *handler = [[MoGRUCandidateClickHandler alloc] init];
  handler.commandSender = command_sender_;
  candidate_view.selectionDelegate = handler;
  view_ = candidate_view;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
