#ifndef ZITTER_CHROMIUM_APP_UI_LAB_H_
#define ZITTER_CHROMIUM_APP_UI_LAB_H_
#include <memory>
#include "base/functional/callback.h"
#include "url/gurl.h"
namespace zitter {
// Opt-in, isolated experiment. Does not replace the production workspace UI.
class UiLab {
 public:
  virtual ~UiLab() = default;
  virtual void Raise() = 0;
  virtual void Navigate(const GURL& url) = 0;
  static std::unique_ptr<UiLab> Create(base::OnceClosure done);
};
}
#endif
