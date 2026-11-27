import SwiftUI
import UIKit
import MetalKit

/// The Core Graphics plan view, wrapped for SwiftUI. It reads every editor
/// setting from LMDocument, so there is nothing to push in here -- a change
/// notification is what makes it redraw.
struct PlanPane: UIViewRepresentable {
    func makeUIView(context: Context) -> LMPlanView {
        let v = LMPlanView(frame: .zero)
        PaneRegistry.shared.plan = v
        // Nothing is drawn yet, so start framed on the whole grid.
        DispatchQueue.main.async { v.fitToLevel() }
        return v
    }

    func updateUIView(_ v: LMPlanView, context: Context) {}
}

/// The Metal 3D preview, wrapped for SwiftUI.
struct PreviewPane: UIViewRepresentable {
    func makeUIView(context: Context) -> LMMetalView {
        let v = LMMetalView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        PaneRegistry.shared.metal = v
        DispatchQueue.main.async { v.resetCamera(forcingWholeGrid: true) }
        return v
    }

    func updateUIView(_ v: LMMetalView, context: Context) {}
}
