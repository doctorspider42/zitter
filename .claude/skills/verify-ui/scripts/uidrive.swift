// Lists the app's windows and clicks at screen points (macOS points, origin top-left).
//   uidrive windows <owner>   one JSON object per window: id, onscreen, x, y, width, height
//   uidrive click <x> <y>     hover 0.6 s (hover states settle), then a left click
//   uidrive rightclick <x> <y> the same with the right button (context menus)
//   uidrive activate <pid>    bring the app to the front (an inactive app drops clicks)
import AppKit
import CoreGraphics
import Foundation

let args = CommandLine.arguments
func fail(_ message: String) -> Never { FileHandle.standardError.write((message + "\n").data(using: .utf8)!); exit(2) }
guard args.count >= 2 else { fail("usage: uidrive windows <owner> | click <x> <y>") }

switch args[1] {
case "windows":
  let owner = args.count > 2 ? args[2] : "Zitter"
  let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
  for w in list where (w[kCGWindowOwnerName as String] as? String) == owner {
    guard let b = w[kCGWindowBounds as String] as? [String: Double], (w[kCGWindowLayer as String] as? Int) == 0 else { continue }
    let item: [String: Any] = ["id": w[kCGWindowNumber as String] as? Int ?? 0, "onscreen": w[kCGWindowIsOnscreen as String] as? Bool ?? false,
                               "x": b["X"] ?? 0, "y": b["Y"] ?? 0, "width": b["Width"] ?? 0, "height": b["Height"] ?? 0]
    print(String(data: try! JSONSerialization.data(withJSONObject: item, options: [.sortedKeys]), encoding: .utf8)!)
  }
case "click":
  guard args.count == 4, let x = Double(args[2]), let y = Double(args[3]) else { fail("usage: uidrive click <x> <y>") }
  let p = CGPoint(x: x, y: y)
  CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
  usleep(600_000)
  for t in [CGEventType.leftMouseDown, .leftMouseUp] {
    CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(120_000)
  }
case "rightclick":
  guard args.count == 4, let x = Double(args[2]), let y = Double(args[3]) else { fail("usage: uidrive rightclick <x> <y>") }
  let p = CGPoint(x: x, y: y)
  CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: .right)?.post(tap: .cghidEventTap)
  usleep(400_000)
  for t in [CGEventType.rightMouseDown, .rightMouseUp] {
    CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: p, mouseButton: .right)?.post(tap: .cghidEventTap)
    usleep(120_000)
  }
case "activate":
  guard args.count == 3, let pid = Int32(args[2]), let app = NSRunningApplication(processIdentifier: pid) else { fail("usage: uidrive activate <pid>") }
  app.activate(options: [.activateAllWindows])
  usleep(500_000)
default: fail("unknown command \(args[1])")
}
