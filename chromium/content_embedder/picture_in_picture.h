// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Picture-in-picture for the Content embedder, modelled on Chrome's
// PictureInPictureWindowManager and AutoPictureInPictureTabHelper. One
// floating window is open at a time: a video overlay (HTMLVideoElement's
// requestPictureInPicture) or a document window
// (documentPictureInPicture.requestWindow). Tab pages that play audible media
// or capture the camera or microphone, and that registered for it, enter
// picture-in-picture automatically when the user switches away; Settings >
// Site permissions > "auto-picture-in-picture" decides (ask by default, like
// Chrome: the window then offers to allow or block the site).

#ifndef ZITTER_CONTENT_PICTURE_IN_PICTURE_H_
#define ZITTER_CONTENT_PICTURE_IN_PICTURE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents_delegate.h"

namespace content {
class PictureInPictureWindowController;
class VideoOverlayWindow;
class VideoPictureInPictureWindowController;
class WebContents;
}  // namespace content

namespace zitter::content_embedder {

class DocumentPictureInPictureWindow;

class PictureInPicture final {
 public:
  // How the window's own controls leave picture-in-picture.
  enum class CloseReason { kClose, kCloseAndPause, kBackToTab };

  static PictureInPicture& Get();

  PictureInPicture(const PictureInPicture&) = delete;
  PictureInPicture& operator=(const PictureInPicture&) = delete;

  // WebContentsDelegate::EnterPictureInPicture for a video in `page`.
  content::PictureInPictureResult EnterVideo(content::WebContents* page);
  // WebContentsDelegate::ExitPictureInPicture: closes the open window.
  void Exit();
  // WebContentsDelegate::AddNewContents with NEW_PICTURE_IN_PICTURE: shows
  // `child` in a floating window. Returns `child`, or null when refused.
  content::WebContents* OpenDocument(content::WebContents* opener,
                                     std::unique_ptr<content::WebContents> child);
  // The close, close-and-pause and back-to-tab controls of the window that
  // `controller` shows.
  void CloseFromWindow(content::PictureInPictureWindowController* controller,
                       CloseReason reason);

  // A tab page was shown, hidden or occluded. `capturing` says whether it
  // uses the camera or microphone.
  void OnTabVisibilityChanged(content::WebContents& page,
                              content::Visibility visibility, bool capturing);
  // Whether `opener` may open a document window without a user gesture
  // because it was just asked to enter picture-in-picture automatically.
  bool AllowsAutomaticWindow(content::WebContents* opener) const;
  // Whether the window shown for `page` opened automatically while the site
  // setting is "ask"; the window then offers to allow or block the site.
  bool ShouldOfferAutomaticSetting(content::WebContents* page) const;
  // The answer to that offer. Blocking also closes the window.
  void AnswerAutomaticSetting(bool allow);

 private:
  friend class base::NoDestructor<PictureInPicture>;
  friend class DocumentPictureInPictureWindow;
  class VideoObserver;
  class OpenerObserver;

  PictureInPicture();
  ~PictureInPicture();

  void CloseWindowInternal();
  void DestroyDocumentWindowSoon();
  void OnDocumentWindowClosed(DocumentPictureInPictureWindow* window);
  void OnOpenerDestroyed();
  // Marks the window opened for `page` as automatic when the page was asked
  // to enter picture-in-picture moments ago.
  void NoteWindowOpened(content::WebContents* page);
  void MaybeEnterAutomatically(base::WeakPtr<content::WebContents> page,
                               bool capturing);
  void MaybeExitAutomatically(base::WeakPtr<content::WebContents> page);

  raw_ptr<content::PictureInPictureWindowController> controller_ = nullptr;
  std::unique_ptr<VideoObserver> video_observer_;
  std::unique_ptr<OpenerObserver> opener_observer_;
  std::unique_ptr<DocumentPictureInPictureWindow> document_;

  // The page just asked to enter picture-in-picture automatically.
  base::WeakPtr<content::WebContents> pending_page_;
  base::TimeTicks pending_deadline_;
  bool pending_ask_ = false;
  // The page whose window opened automatically.
  base::WeakPtr<content::WebContents> automatic_page_;
  bool automatic_ask_ = false;

  base::WeakPtrFactory<PictureInPicture> weak_factory_{this};
};

// ContentBrowserClient::CreateWindowForVideoPictureInPicture.
std::unique_ptr<content::VideoOverlayWindow> CreateVideoOverlayWindow(
    content::VideoPictureInPictureWindowController* controller);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PICTURE_IN_PICTURE_H_
