import Foundation
import CoreGraphics
import ImageIO

let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
let space = CGColorSpaceCreateDeviceRGB()
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
                                bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.scaleBy(x: CGFloat(pixels) / 1024, y: CGFloat(pixels) / 1024)
        context.addPath(CGPath(roundedRect: CGRect(x: 48, y: 48, width: 928, height: 928), cornerWidth: 210, cornerHeight: 210, transform: nil))
        context.clip()
        let gradient = CGGradient(colorsSpace: space, colors: [CGColor(red: 1, green: 0.50, blue: 0.20, alpha: 1), CGColor(red: 0.72, green: 0.20, blue: 0.10, alpha: 1)] as CFArray, locations: [0, 1])!
        context.drawLinearGradient(gradient, start: CGPoint(x: 180, y: 900), end: CGPoint(x: 850, y: 100), options: [])
        context.setStrokeColor(CGColor(gray: 1, alpha: 0.94)); context.setLineWidth(45)
        context.strokeEllipse(in: CGRect(x: 240, y: 240, width: 544, height: 544))
        context.setStrokeColor(CGColor(gray: 1, alpha: 0.35)); context.setLineWidth(14)
        context.strokeEllipse(in: CGRect(x: 300, y: 300, width: 424, height: 424))
        context.setFillColor(CGColor(gray: 1, alpha: 0.96)); context.fillEllipse(in: CGRect(x: 445, y: 445, width: 134, height: 134))
        let filename = "icon_\(size)x\(size)\(scale == 2 ? "@2x" : "").png"
        let destination = CGImageDestinationCreateWithURL(output.appendingPathComponent(filename) as CFURL, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, context.makeImage()!, nil)
        guard CGImageDestinationFinalize(destination) else { fatalError("Cannot save icon") }
    }
}
