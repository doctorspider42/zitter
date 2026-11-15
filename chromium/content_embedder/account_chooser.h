// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The sign-in dialog shared by the Credential Management API (saved
// passwords) and FedCM (identity provider accounts): a dialog over the page's
// window that lists accounts with a picture, a name and a detail line, or
// shows a message while loading, verifying or after an error. The owner can
// change what it shows; deleting the chooser closes it without an answer.

#ifndef ZITTER_CONTENT_ACCOUNT_CHOOSER_H_
#define ZITTER_CONTENT_ACCOUNT_CHOOSER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/image/image_skia.h"

namespace content {
class WebContents;
}
namespace views {
class DialogDelegate;
class Widget;
}

namespace zitter::content_embedder {

class AccountChooserView;

class AccountChooser final {
 public:
  struct Account {
    std::u16string name;
    std::u16string detail;
    gfx::ImageSkia picture;  // Empty: no picture.
  };

  struct Page {
    Page();
    Page(Page&&);
    Page& operator=(Page&&);
    ~Page();

    std::u16string title;
    std::u16string message;
    // Listed with radio buttons; the first one starts selected.
    std::vector<Account> accounts;
    std::u16string note;
    // Empty: the page has no accept button.
    std::u16string accept;
    std::u16string cancel;
  };

  enum class Answer {
    kAccepted,
    // The Cancel button, the close button or Escape.
    kCancelled,
    // The window went away for another reason, e.g. its parent closed.
    kClosed,
  };
  // `account` is the selected row when the page lists accounts. Accepting
  // keeps the dialog open: the owner updates or deletes the chooser.
  using Done = base::OnceCallback<void(Answer answer, std::optional<size_t> account)>;

  // Returns nullptr when the page has no window to show the dialog over.
  // `modal` blocks the window until the dialog is answered.
  static std::unique_ptr<AccountChooser> Show(content::WebContents& contents,
                                              Page page,
                                              bool modal,
                                              Done done);
  ~AccountChooser();

  void Update(Page page, Done done);

 private:
  AccountChooser();
  void Apply(Page page, Done done);
  void OnAnswer(Answer answer, std::optional<size_t> account);

  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtr<AccountChooserView> view_;
  Done done_;
  base::WeakPtrFactory<AccountChooser> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_ACCOUNT_CHOOSER_H_
