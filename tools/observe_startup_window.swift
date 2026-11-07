// Observe WindowServer metadata for the benchmark process without capturing pixels.
import CoreGraphics
import Foundation

guard CommandLine.arguments.count == 2,
      let pid = Int32(CommandLine.arguments[1]), pid > 0 else {
    exit(2)
}

let deadline = DispatchTime.now().uptimeNanoseconds + 30_000_000_000
while DispatchTime.now().uptimeNanoseconds < deadline && kill(pid, 0) == 0 {
    let windows = CGWindowListCopyWindowInfo(
        [.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID
    ) as? [[String: Any]] ?? []
    if windows.contains(where: { window in
        guard window[kCGWindowOwnerPID as String] as? Int32 == pid,
              window[kCGWindowLayer as String] as? Int == 0,
              let bounds = window[kCGWindowBounds as String] as? [String: CGFloat],
              let width = bounds["Width"], let height = bounds["Height"] else {
            return false
        }
        return width >= 500 && height >= 300
    }) {
        print(DispatchTime.now().uptimeNanoseconds)
        exit(0)
    }
    Thread.sleep(forTimeInterval: 0.005)
}

fputs("No on-screen Zitter workspace window observed\n", stderr)
exit(1)
