import SwiftUI
import UIKit

/// Observes the Objective-C document and republishes its changes into SwiftUI.
/// The document itself stays the single source of truth, exactly as the Win32
/// build's global AppState did.
final class DocModel: ObservableObject {
    static let shared = DocModel()

    @Published private(set) var revision: Int = 0

    private var observer: NSObjectProtocol?

    init() {
        observer = NotificationCenter.default.addObserver(
            forName: NSNotification.Name(LMDocumentChangedNotification),
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.revision &+= 1
        }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }

    var doc: LMDocument { LMDocument.shared }
}

/// Weak handles to the two UIKit panes, so toolbar actions can reach them
/// without threading bindings through every view.
final class PaneRegistry {
    static let shared = PaneRegistry()
    weak var plan: LMPlanView?
    weak var metal: LMMetalView?
}

@main
struct LevelMakerApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(DocModel.shared)
                .preferredColorScheme(.dark)   // the editor palette is dark
        }
    }
}
