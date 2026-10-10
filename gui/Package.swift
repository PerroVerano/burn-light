// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "BurnLight",
    platforms: [.macOS("26.0")],
    products: [.executable(name: "BurnLight", targets: ["BurnLightApp"])],
    targets: [
        .target(name: "BurnLightCore"),
        .executableTarget(name: "BurnLightApp", dependencies: ["BurnLightCore"])
    ]
)
