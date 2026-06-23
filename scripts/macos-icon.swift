// Generates the Nova Terminal app icon (a dark terminal squircle with a green
// prompt chevron + cursor). Source of truth for assets/nova.icns.
//
// Regenerate:
//   swift scripts/macos-icon.swift /tmp/nova_1024.png
//   mkdir -p /tmp/nova.iconset
//   for s in 16 32 128 256 512; do \
//     sips -z $s $s /tmp/nova_1024.png --out /tmp/nova.iconset/icon_${s}x${s}.png; \
//     d=$((s*2)); sips -z $d $d /tmp/nova_1024.png --out /tmp/nova.iconset/icon_${s}x${s}@2x.png; done
//   cp /tmp/nova_1024.png /tmp/nova.iconset/icon_512x512@2x.png
//   iconutil -c icns /tmp/nova.iconset -o assets/nova.icns
import AppKit

let S = 1024.0
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1024, pixelsHigh: 1024,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

// Rounded-rect "squircle" background with a subtle vertical gradient.
let margin = S * 0.085
let rect = NSRect(x: margin, y: margin, width: S - 2*margin, height: S - 2*margin)
let radius = (S - 2*margin) * 0.225
let bg = NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius)
let grad = NSGradient(colors: [
    NSColor(srgbRed: 0.150, green: 0.168, blue: 0.200, alpha: 1.0),
    NSColor(srgbRed: 0.094, green: 0.106, blue: 0.130, alpha: 1.0),
])!
grad.draw(in: bg, angle: -90)
NSColor(white: 1.0, alpha: 0.06).setStroke()
bg.lineWidth = 2.5
bg.stroke()

let green = NSColor(srgbRed: 0.36, green: 0.84, blue: 0.50, alpha: 1.0)

// Prompt chevron  ❯
green.setStroke()
let chev = NSBezierPath()
chev.lineWidth = S * 0.058
chev.lineCapStyle = .round
chev.lineJoinStyle = .round
let cx = S * 0.40, cy = S * 0.50, w = S * 0.135, h = S * 0.165
chev.move(to: NSPoint(x: cx - w, y: cy + h))
chev.line(to: NSPoint(x: cx + w, y: cy))
chev.line(to: NSPoint(x: cx - w, y: cy - h))
chev.stroke()

// Cursor block to the right of the prompt.
green.withAlphaComponent(0.95).setFill()
let curW = S * 0.215, curH = S * 0.060
let cur = NSBezierPath(roundedRect:
    NSRect(x: cx + w + S * 0.055, y: cy - curH/2, width: curW, height: curH),
    xRadius: curH * 0.45, yRadius: curH * 0.45)
cur.fill()

NSGraphicsContext.restoreGraphicsState()

let png = rep.representation(using: .png, properties: [:])!
try! png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
