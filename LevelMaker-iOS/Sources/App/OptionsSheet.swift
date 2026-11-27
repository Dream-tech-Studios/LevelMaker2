import SwiftUI

/// Everything the Win32 build kept on its menu bar. An iPad has no menu bar,
/// so the infrequent settings live here and the toolbar keeps only what gets
/// touched during a stroke.
struct OptionsSheet: View {
    @EnvironmentObject var model: DocModel
    @Environment(\.dismiss) private var dismiss

    private var doc: LMDocument { model.doc }

    var body: some View {
        NavigationStack {
            Form {
                Section("Input") {
                    Toggle("One finger draws", isOn: binding(\.fingerDraws))
                    Text("Off: a finger pans and the Pencil draws. On: a finger draws too, "
                         + "and two fingers pan. Turn it on for an iPad with no Pencil.")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    Toggle("Pencil pressure sets width", isOn: binding(\.pressureSensitive))
                    Text("Scales the stamp between 0.4x and 1.6x of the brush size. "
                         + "Pencil only -- fingers report no usable force.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("Drawing") {
                    Toggle("Straight strokes", isOn: binding(\.straightMode))
                    if doc.straightMode {
                        Picker("Direction", selection: binding(\.drawAngle)) {
                            ForEach([0, 45, 90, 135, 180, 225, 270, 315], id: \.self) { a in
                                Text("\(a)\u{00b0}").tag(a)
                            }
                        }
                    }
                    Toggle("Smooth diagonals", isOn: binding(\.smoothDiagonals))
                    Text("Chamfers 45\u{00b0} staircases into straight edges instead of steps.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("Preview and export") {
                    Picker("Surfaces face", selection: facingBinding) {
                        Text("Inward").tag(LMFacingMode.inward)
                        Text("Outward").tag(LMFacingMode.outward)
                        Text("Both").tag(LMFacingMode.both)
                    }
                    Toggle("Ceilings", isOn: binding(\.showCeiling))
                    Toggle("Quad outlines", isOn: binding(\.showWire))
                    Toggle("Cutaway", isOn: binding(\.cutaway))
                    Text("Cutaway culls back faces so you can see into the halls. "
                         + "It does nothing when surfaces face both ways.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("Scale") {
                    LabeledContent("Cell size") {
                        Stepper("\(doc.cellSize, specifier: "%.1f")",
                                value: cellSizeBinding, in: 0.5...20, step: 0.5)
                    }
                    LabeledContent("Wall height") {
                        Stepper("\(doc.wallHeight, specifier: "%.1f")",
                                value: wallHeightBinding, in: 0.5...40, step: 0.5)
                    }
                }
            }
            .navigationTitle("Options")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }

    // MARK: - bindings
    // LMDocument is a reference type that posts a notification on every set,
    // which DocModel turns into a published revision, so these read straight
    // through rather than caching anything.

    private func binding(_ key: ReferenceWritableKeyPath<LMDocument, Bool>) -> Binding<Bool> {
        Binding(get: { doc[keyPath: key] }, set: { doc[keyPath: key] = $0 })
    }

    private func binding(_ key: ReferenceWritableKeyPath<LMDocument, Int>) -> Binding<Int> {
        Binding(get: { doc[keyPath: key] }, set: { doc[keyPath: key] = $0 })
    }

    private var facingBinding: Binding<LMFacingMode> {
        Binding(get: { doc.facing }, set: { doc.facing = $0 })
    }

    private var cellSizeBinding: Binding<Double> {
        Binding(get: { Double(doc.cellSize) }, set: { doc.cellSize = Float($0) })
    }

    private var wallHeightBinding: Binding<Double> {
        Binding(get: { Double(doc.wallHeight) }, set: { doc.wallHeight = Float($0) })
    }
}
