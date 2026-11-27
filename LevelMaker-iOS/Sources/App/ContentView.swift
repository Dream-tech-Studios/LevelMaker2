import SwiftUI
import UniformTypeIdentifiers

/// The level file type, declared in Info.plist. Falls back to raw data if the
/// declaration is ever missing, so Open still works.
let lmLevelType: UTType = UTType("com.levelmaker.level") ?? .data

enum Pane: String, CaseIterable {
    case plan = "Plan"
    case preview = "3D"
}

struct ContentView: View {
    @EnvironmentObject var model: DocModel
    @Environment(\.horizontalSizeClass) private var sizeClass

    @State private var pane: Pane = .plan
    @State private var showingOptions = false
    @State private var importing = false
    @State private var alert: String?

    /// Side by side only where there is room for it: the 12.9" iPad Pro in
    /// landscape. On the mini and on iPhone the panes swap instead, which is
    /// the layout the compact devices were always going to need.
    private var sideBySide: Bool {
        sizeClass == .regular
    }

    /// Driving the alert from an optional needs a real two-way binding --
    /// a .constant() would never let it dismiss.
    private var alertShown: Binding<Bool> {
        Binding(get: { alert != nil },
                set: { shown in if !shown { alert = nil } })
    }

    private var brushBinding: Binding<Int> {
        Binding(get: { model.doc.brush }, set: { model.doc.brush = $0 })
    }

    var body: some View {
        VStack(spacing: 0) {
            content
            Divider()
            statusBar
        }
        .safeAreaInset(edge: .bottom) { toolbar }
        .sheet(isPresented: $showingOptions) { OptionsSheet() }
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: [lmLevelType],
                      allowsMultipleSelection: false) { result in
            handleImport(result)
        }
        .alert("LevelMaker", isPresented: alertShown) {
            Button("OK") { alert = nil }
        } message: {
            Text(alert ?? "")
        }
    }

    // MARK: - layout

    @ViewBuilder private var content: some View {
        if sideBySide {
            HStack(spacing: 0) {
                PlanPane()
                Divider()
                PreviewPane()
            }
        } else {
            ZStack {
                // Both stay alive so the 3D camera and the plan camera keep
                // their positions across a swap.
                PlanPane().opacity(pane == .plan ? 1 : 0)
                PreviewPane().opacity(pane == .preview ? 1 : 0)
            }
        }
    }

    private var statusBar: some View {
        HStack {
            Text(model.doc.displayName)
                .fontWeight(.medium)
            Spacer()
            Text(model.doc.statusText)
                .foregroundStyle(.secondary)
        }
        .font(.footnote.monospacedDigit())
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(.bar)
    }

    // MARK: - toolbar

    private var toolbar: some View {
        HStack(spacing: 14) {
            if !sideBySide {
                Picker("", selection: $pane) {
                    ForEach(Pane.allCases, id: \.self) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .frame(width: 128)
            }

            Button { _ = model.doc.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!model.doc.canUndo)
            Button { _ = model.doc.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!model.doc.canRedo)

            Divider().frame(height: 22)

            Button {
                model.doc.eraser.toggle()
            } label: {
                Image(systemName: model.doc.eraser ? "eraser.fill" : "pencil.tip")
            }
            .tint(model.doc.eraser ? .orange : .accentColor)

            Menu {
                Picker("Brush", selection: brushBinding) {
                    ForEach([1, 2, 3, 4, 6, 8, 12, 16], id: \.self) { n in
                        Text("\(n) cell\(n == 1 ? "" : "s")").tag(n)
                    }
                }
            } label: {
                Label("\(model.doc.brush)", systemImage: "circle.dotted")
                    .labelStyle(.titleAndIcon)
            }

            Divider().frame(height: 22)

            Button { PaneRegistry.shared.plan?.fitToLevel()
                     PaneRegistry.shared.metal?.resetCamera(forcingWholeGrid: true) }
                label: { Image(systemName: "viewfinder") }

            Spacer()

            fileMenu

            Button { showingOptions = true } label: { Image(systemName: "slider.horizontal.3") }
        }
        .font(.body)
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(.bar)
    }

    private var fileMenu: some View {
        Menu {
            Button("New Level") { model.doc.newLevel(); PaneRegistry.shared.plan?.fitToLevel() }
            Button("Open...") { importing = true }
            Divider()
            Button("Save As...") { export(kind: .level) }
            Button("Export OBJ...") { export(kind: .obj) }
            Button("Export FBX...") { export(kind: .fbx) }
            Divider()
            Button("Clear Level", role: .destructive) { model.doc.clearLevel() }
        } label: {
            Image(systemName: "folder")
        }
    }

    // MARK: - file i/o

    private func handleImport(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }
        // A picked URL arrives security-scoped and must be bracketed.
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }

        if model.doc.load(from: url) {
            PaneRegistry.shared.plan?.fitToLevel()
            PaneRegistry.shared.metal?.resetCamera(forcingWholeGrid: true)
        } else {
            alert = "That file is not a LevelMaker level, or it could not be read."
        }
    }

    private enum ExportKind { case level, obj, fbx }

    private func export(kind: ExportKind) {
        if kind != .level && model.doc.floorCount == 0 {
            alert = "Nothing to export yet -- trace a hall in the plan view first."
            return
        }

        let name = model.doc.displayName
            .replacingOccurrences(of: " \u{2022}", with: "")
        let ext: String
        switch kind {
        case .level: ext = "lvl"
        case .obj:   ext = "obj"
        case .fbx:   ext = "fbx"
        }

        // Write into a temporary directory, then hand that file to the
        // document picker. The engine only ever writes plain POSIX paths, so
        // nothing has to understand the sandbox but this one function.
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("export-\(UUID().uuidString)", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        } catch {
            alert = "Could not prepare the export."
            return
        }
        let url = dir.appendingPathComponent(name).appendingPathExtension(ext)

        let ok: Bool
        switch kind {
        case .level: ok = model.doc.save(to: url)
        case .obj:   ok = model.doc.exportOBJ(to: url)
        case .fbx:   ok = model.doc.exportFBX(to: url)
        }
        guard ok else {
            alert = "Could not write that file."
            return
        }

        // An OBJ export also writes a sibling .mtl, so offer both.
        var files = [url]
        if kind == .obj {
            let mtl = url.deletingPathExtension().appendingPathExtension("mtl")
            if FileManager.default.fileExists(atPath: mtl.path) { files.append(mtl) }
        }
        DocumentPicker.present(exporting: files)
    }
}
