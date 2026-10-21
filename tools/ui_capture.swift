// Copyright 2026 the Zitter contributors
// SPDX-License-Identifier: Apache-2.0
// Observe ONLY the benchmark window. Emit frame IDs and WindowServer display
// timestamps, never screenshots, image files, or other windows' contents.
import Foundation
import ScreenCaptureKit
import CoreGraphics
import CoreMedia
import CoreVideo
import Darwin

func emit(_ value: [String: Any]) {
    if let data = try? JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]) {
        FileHandle.standardOutput.write(data + Data([10]))
    }
}
func nanoseconds(_ ticks: UInt64) -> UInt64 {
    var info = mach_timebase_info_data_t()
    mach_timebase_info(&info)
    return ticks / UInt64(info.denom) * UInt64(info.numer)
         + ticks % UInt64(info.denom) * UInt64(info.numer) / UInt64(info.denom)
}
final class Observer: NSObject, SCStreamOutput, SCStreamDelegate, @unchecked Sendable {
    var scenario = "selection"
    var last = -1
    var location: (Int, Int)?
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        emit(["error": error.localizedDescription])
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sample: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, sample.isValid,
              let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let frame = attachments.first,
              let status = frame[.status] as? Int, status == SCFrameStatus.complete.rawValue,
              let display = frame[.displayTime] as? UInt64,
              let image = CMSampleBufferGetImageBuffer(sample) else { return }
        CVPixelBufferLockBaseAddress(image, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(image, .readOnly) }
        guard let address = CVPixelBufferGetBaseAddress(image) else { return }
        let pixels = address.assumingMemoryBound(to: UInt8.self)
        let width = CVPixelBufferGetWidth(image), height = CVPixelBufferGetHeight(image)
        let stride = CVPixelBufferGetBytesPerRow(image)
        func code(_ x: Int, _ y: Int, bits: Int = 10, prefix: Int = 0xa55a) -> Int? {
            guard x >= 0, x + (bits + 16) * 4 <= width, y >= 0, y < height else { return nil }
            var value = 0
            for bit in 0..<(bits + 16) {
                let offset = y * stride + (x + bit * 4 + 2) * 4
                let bright = (Int(pixels[offset]) + Int(pixels[offset + 1]) + Int(pixels[offset + 2])) / 3 > 128
                value = (value << 1) | (bright ? 1 : 0)
            }
            return value >> bits == prefix ? value & ((1 << bits) - 1) : nil
        }
        var marker: Int?
        if let (x, y) = location { marker = code(x, y) }
        if marker == nil {
            search: for y in 0..<min(120, height) {
                for x in 0..<min(40, max(0, width - 104)) {
                    if let value = code(x, y) { location = (x, y); marker = value; break search }
                }
            }
        }
        guard let id = marker, id != last else { return }
        guard let (x, y) = location else { return }
        // The identity stripe and selected row must belong to the same scene.
        // This rejects a marker that arrived before the relevant raster tile.
        let selectedY = y - 4 + 64 + (id % 16) * 36 + 5
        let selectedX = x - 4 + 16
        guard id == 0 || (selectedX >= 0 && selectedX < width && selectedY >= 0 && selectedY < height) else { return }
        if id != 0 {
            let offset = selectedY * stride + selectedX * 4
            guard pixels[offset] > 110 && pixels[offset + 1] < 140 && pixels[offset + 2] < 120 else { return }
            let previousY = y - 4 + 64 + ((id - 1) % 16) * 36 + 5
            let previous = previousY * stride + selectedX * 4
            guard previous >= 0 && previous + 2 < stride * height && pixels[previous] < 110 else { return }
        }
        if id != 0 {
            let sidebarWidth = scenario == "resize" ? 240 + ((id - 1) % 16) * 8 : 280
            let pageX = x - 4 + sidebarWidth + 4
            // The page owns this second stripe. It changes only after the
            // renderer has received and painted the new viewport dimensions.
            guard code(pageX, y, bits: 11, prefix: 0x5aa5) == width - sidebarWidth else { return }
            let left = (y - 4 + 52) * stride + (x - 4 + sidebarWidth - 4) * 4
            let right = (y - 4 + 52) * stride + (x - 4 + sidebarWidth + 4) * 4
            guard left >= 0 && right + 2 < stride * height,
                  pixels[left] > 20 && pixels[left] < 100 && pixels[right] > 180 else { return }
        }
        last = id
        emit(["marker_id": id, "display_ns": nanoseconds(display),
              "callback_ns": nanoseconds(mach_absolute_time()), "scene_verified": true, "width": width, "height": height])
    }
}

@main struct Capture {
    static func main() async {
        guard (2...3).contains(CommandLine.arguments.count), let pid = Int32(CommandLine.arguments[1]),
              CGPreflightScreenCaptureAccess() else {
            emit(["error": "Screen Recording permission required; no permission request was issued"])
            exit(2)
        }
        do {
            var found: (SCWindow, SCDisplay)?
            for _ in 0..<100 {
                let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
                if let window = content.windows.first(where: { $0.owningApplication?.processID == pid && $0.frame.width > 500 }),
                   let display = content.displays.first(where: { $0.frame.contains(window.frame) }) {
                    found = (window, display); break
                }
                try await Task.sleep(nanoseconds: 100_000_000)
            }
            guard let (window, display) = found else { throw NSError(domain: "ZitterCapture", code: 1, userInfo: [NSLocalizedDescriptionKey: "Benchmark window missing or not fully on one display"]) }
            let filter = SCContentFilter(display: display, including: [window])
            let config = SCStreamConfiguration()
            config.sourceRect = CGRect(x: window.frame.minX - display.frame.minX,
                                       y: window.frame.minY - display.frame.minY,
                                       width: window.frame.width, height: window.frame.height)
            config.width = Int(window.frame.width)
            config.height = Int(window.frame.height)
            config.pixelFormat = kCVPixelFormatType_32BGRA
            config.minimumFrameInterval = CMTime(value: 1, timescale: 120)
            config.queueDepth = 5
            config.showsCursor = false
            config.capturesAudio = false
            let observer = Observer()
            if CommandLine.arguments.count == 3 { observer.scenario = CommandLine.arguments[2] }
            let stream = SCStream(filter: filter, configuration: config, delegate: observer)
            try stream.addStreamOutput(observer, type: .screen, sampleHandlerQueue: DispatchQueue(label: "zitter.capture"))
            try await stream.startCapture()
            emit(["ready": true, "pid": pid, "clock": "mach_absolute_time", "measurement": "windowserver-displayTime"])
            while kill(pid, 0) == 0 { try await Task.sleep(nanoseconds: 100_000_000) }
            try await stream.stopCapture()
        } catch {
            emit(["error": error.localizedDescription]); exit(1)
        }
    }
}
