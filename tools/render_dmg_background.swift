// Render the drag-to-Applications DMG background ("Papier" design) at 1x and 2x.
//
// The light theme is deliberate: Finder draws icon labels in black over a
// background picture, even in Dark Mode, so a dark background hides them.
//
// Usage: swift render_dmg_background.swift FONTS_DIR VERSION FOOTER OUTPUT_DIR
// Writes OUTPUT_DIR/background.png (720x432) and OUTPUT_DIR/background@2x.png.
// Finder draws the icons and their labels; package_dmg.py places them at
// (200, 236) and (520, 236), the centres this artwork leaves room for.
import AppKit
import CoreText
import Foundation

let width: CGFloat = 720
let height: CGFloat = 432

guard CommandLine.arguments.count == 5 else {
    fputs("usage: render_dmg_background.swift FONTS_DIR VERSION FOOTER OUTPUT_DIR\n", stderr)
    exit(2)
}
let fonts = URL(fileURLWithPath: CommandLine.arguments[1])
let version = CommandLine.arguments[2]
let footer = CommandLine.arguments[3]
let output = URL(fileURLWithPath: CommandLine.arguments[4])

func color(_ hex: UInt32) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
            blue: CGFloat(hex & 0xff) / 255, alpha: 1)
}

// Zitter design tokens, "Papier" theme.
let void = color(0xebe8e1)      // bg-void
let ink = color(0x16141c)       // ink
let inkMuted = color(0x5a5566)  // ink-muted
let inkFaint = color(0x5a5566)  // ink-muted: the middle dot stays legible on ivory
let particle = color(0x4b40d9)  // ket: zing would vanish on ivory

func font(_ file: String, size: CGFloat, weight: CGFloat? = nil) -> CTFont {
    let url = fonts.appendingPathComponent(file) as CFURL
    guard let descriptors = CTFontManagerCreateFontDescriptorsFromURL(url) as? [CTFontDescriptor],
          let descriptor = descriptors.first else {
        fputs("Cannot load font \(file)\n", stderr)
        exit(1)
    }
    let base = CTFontCreateWithFontDescriptor(descriptor, size, nil)
    guard let weight else { return base }
    let wght = 0x7767_6874  // 'wght'
    let variation = [kCTFontVariationAttribute: [wght: weight]] as CFDictionary
    return CTFontCreateCopyWithAttributes(base, size, nil, CTFontDescriptorCreateWithAttributes(variation))
}

let eyebrowFont = font("SchibstedGrotesk-Variable.ttf", size: 11, weight: 600)
let displayFont = font("InstrumentSerif-Italic.ttf", size: 40)
let monoFont = font("JetBrainsMono-Variable.ttf", size: 11, weight: 400)

func line(_ text: String, _ font: CTFont, _ fill: CGColor, kern: CGFloat = 0) -> CTLine {
    let attributes: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): fill,
        NSAttributedString.Key(kCTKernAttributeName as String): kern,
    ]
    return CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attributes))
}

func advance(_ line: CTLine) -> CGFloat {
    CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
}

// Draws a line inside a CSS-style line box whose top edge is `top` (y down).
func draw(_ line: CTLine, x: CGFloat, top: CGFloat, lineHeight: CGFloat, in context: CGContext) {
    var ascent: CGFloat = 0, descent: CGFloat = 0
    CTLineGetTypographicBounds(line, &ascent, &descent, nil)
    let baseline = top + (lineHeight - (ascent + descent)) / 2 + ascent
    context.saveGState()
    context.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    context.textPosition = CGPoint(x: x, y: baseline)
    CTLineDraw(line, context)
    context.restoreGState()
}

func centered(_ line: CTLine, top: CGFloat, lineHeight: CGFloat, in context: CGContext) {
    draw(line, x: ((width - advance(line)) / 2).rounded(), top: top, lineHeight: lineHeight, in: context)
}

func render(scale: CGFloat, to url: URL) throws {
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    guard let context = CGContext(data: nil, width: Int(width * scale), height: Int(height * scale),
                                  bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
        throw NSError(domain: "dmg", code: 1)
    }
    // Work in CSS points with the origin at the top-left corner.
    context.translateBy(x: 0, y: height * scale)
    context.scaleBy(x: scale, y: -scale)

    context.setFillColor(void)
    context.fill(CGRect(x: 0, y: 0, width: width, height: height))

    // Eyebrow and italic display title.
    centered(line("INSTALACJA", eyebrowFont, inkMuted, kern: 11 * 0.12), top: 40, lineHeight: 14, in: context)
    centered(line("Przeciągnij do Aplikacji", displayFont, ink, kern: -0.4), top: 62, lineHeight: 40, in: context)

    // The trembling path between the two icons, ending in a chevron.
    let wave = CGMutablePath()
    wave.move(to: CGPoint(x: 298, y: 236))
    var x: CGFloat = 298
    for index in 0..<5 {
        let lift: CGFloat = index % 2 == 0 ? -9 : 9
        wave.addQuadCurve(to: CGPoint(x: x + 22, y: 236), control: CGPoint(x: x + 11, y: 236 + lift))
        x += 22
    }
    wave.addLine(to: CGPoint(x: 422, y: 236))
    wave.move(to: CGPoint(x: 415, y: 230))
    wave.addLine(to: CGPoint(x: 422, y: 236))
    wave.addLine(to: CGPoint(x: 415, y: 242))
    context.addPath(wave)
    context.setStrokeColor(inkMuted)
    context.setLineWidth(1.6)
    context.setLineCap(.round)
    context.setLineJoin(.round)
    context.strokePath()

    // The single accent: the particle setting off.
    context.setFillColor(particle)
    context.fillEllipse(in: CGRect(x: 292, y: 232, width: 8, height: 8))

    // Footer: version and platform, separated by faint middle dots.
    var parts: [CTLine] = []
    for (index, text) in ["v\(version)", footer].enumerated() {
        if index > 0 { parts.append(line("·", monoFont, inkFaint)) }
        parts.append(line(text, monoFont, inkMuted))
    }
    let gap: CGFloat = 16
    let total = parts.map(advance).reduce(0, +) + gap * CGFloat(parts.count - 1)
    var cursor = ((width - total) / 2).rounded()
    for part in parts {
        draw(part, x: cursor, top: height - 20 - 16, lineHeight: 16, in: context)
        cursor += advance(part) + gap
    }

    guard let image = context.makeImage() else { throw NSError(domain: "dmg", code: 2) }
    let bitmap = NSBitmapImageRep(cgImage: image)
    bitmap.size = NSSize(width: width, height: height)  // 72 dpi at 1x, 144 dpi at 2x
    guard let png = bitmap.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "dmg", code: 3)
    }
    try png.write(to: url)
}

do {
    try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
    try render(scale: 1, to: output.appendingPathComponent("background.png"))
    try render(scale: 2, to: output.appendingPathComponent("background@2x.png"))
} catch {
    fputs("Rendering the DMG background failed: \(error)\n", stderr)
    exit(1)
}
