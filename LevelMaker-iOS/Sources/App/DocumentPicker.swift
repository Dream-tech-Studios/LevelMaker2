import UIKit
import UniformTypeIdentifiers

/// Hands finished files to the system document picker so the user chooses
/// where they land -- Files, iCloud Drive, or anywhere else.
///
/// SwiftUI's .fileExporter wants a FileDocument, which does not suit an engine
/// that writes bytes to a path. Presenting UIDocumentPickerViewController over
/// the key window is both shorter and closer to what the engine already does.
enum DocumentPicker {

    static func present(exporting urls: [URL]) {
        guard !urls.isEmpty, let presenter = topViewController() else { return }

        let picker = UIDocumentPickerViewController(forExporting: urls, asCopy: true)
        picker.shouldShowFileExtensions = true
        // On a compact screen a form sheet leaves the picker usable; on a big
        // iPad the system gives it a sheet anyway.
        picker.modalPresentationStyle = .formSheet
        presenter.present(picker, animated: true)
    }

    private static func topViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes
        guard let window = scenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.activationState == .foregroundActive })?
            .windows
            .first(where: { $0.isKeyWindow })
        else { return nil }

        var top = window.rootViewController
        while let next = top?.presentedViewController {
            top = next
        }
        return top
    }
}
