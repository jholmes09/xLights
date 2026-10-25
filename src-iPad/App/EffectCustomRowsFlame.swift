import SwiftUI

// Flame effect `controlType: custom` rows — iPad side of
// `src-ui-wx/effectpanels/FlamePanel.cpp`. The panel reads the row's
// DMX Flame model (through the bridge) to list its presets, lay out
// its angle positions as a fan, offer its shot-length quick buttons
// and keep the effect's length fitted to the preset / shot (with the
// optional "move early" lead taken out of the front).
//
// Custom ids (resources/effectmetadata/Flame.json):
//   Flame_PresetPicker  → FlamePresetPickerRowView
//   Flame_FanButtons    → FlameFanButtonsRowView
//   Flame_ShotButtons   → FlameShotButtonsRowView
//   Flame_Info          → FlameInfoRowView (also drives auto length)

// MARK: - Keys / shared helpers

enum FlameKeys {
    static let action     = "E_CHOICE_Flame_Action"
    static let repeatMode = "E_CHOICE_Flame_Repeat"
    static let preset     = "E_SPINCTRL_Flame_Preset"
    static let angle      = "E_SLIDER_Flame_Angle"
    static let endAngle   = "E_SLIDER_Flame_EndAngle"
    static let pattern    = "E_TEXTCTRL_Flame_Pattern"
    static let shotMs     = "E_SPINCTRL_Flame_ShotMs"
    static let gapMs      = "E_SPINCTRL_Flame_GapMs"
    static let moveEarly  = "E_CHECKBOX_Flame_MoveEarly"
    static let fitLength  = "E_CHECKBOX_Flame_FitLength"
    static let timing     = "E_CHOICE_Flame_TimingTrack"
    /// Hidden: how far "move early" has already shifted the effect's
    /// start, so the fire time can be kept when the lead changes.
    static let leadMs     = "E_TEXTCTRL_Flame_LeadMs"
}

/// The row model's flame fixture, as `flameFixtureInfo(forModel:)`
/// returns it. Nil when the selected effect's row isn't a DMX Flame.
struct FlameFixtureInfo {
    struct Preset {
        let n: Int
        let name: String
        let group: String
        let lengthMs: Int
        let isStatic: Bool
        let position: Int
        let longFlame: Bool
    }
    struct Position {
        let n: Int
        let deg: Double
        let allowed: Bool
    }

    let id: String
    let name: String
    let hasAngle: Bool
    let hasPresets: Bool
    let flip: Bool
    let minAllowedDeg: Double
    let maxAllowedDeg: Double
    let presets: [Preset]
    let positions: [Position]
    let shotPresetsMs: [Int]

    init?(_ d: [AnyHashable: Any]) {
        guard let id = d["id"] as? String else { return nil }
        self.id = id
        name = d["name"] as? String ?? ""
        hasAngle = (d["hasAngle"] as? NSNumber)?.boolValue ?? false
        hasPresets = (d["hasPresets"] as? NSNumber)?.boolValue ?? false
        flip = (d["flip"] as? NSNumber)?.boolValue ?? false
        minAllowedDeg = (d["minAllowedDeg"] as? NSNumber)?.doubleValue ?? -105
        maxAllowedDeg = (d["maxAllowedDeg"] as? NSNumber)?.doubleValue ?? 105
        presets = (d["presets"] as? [[AnyHashable: Any]] ?? []).map {
            Preset(n: ($0["n"] as? NSNumber)?.intValue ?? 0,
                   name: $0["name"] as? String ?? "",
                   group: $0["group"] as? String ?? "",
                   lengthMs: ($0["lengthMs"] as? NSNumber)?.intValue ?? 0,
                   isStatic: ($0["isStatic"] as? NSNumber)?.boolValue ?? false,
                   position: ($0["position"] as? NSNumber)?.intValue ?? 0,
                   longFlame: ($0["longFlame"] as? NSNumber)?.boolValue ?? false)
        }
        positions = (d["positions"] as? [[AnyHashable: Any]] ?? []).map {
            Position(n: ($0["n"] as? NSNumber)?.intValue ?? 0,
                     deg: ($0["deg"] as? NSNumber)?.doubleValue ?? 0,
                     allowed: ($0["allowed"] as? NSNumber)?.boolValue ?? true)
        }
        shotPresetsMs = (d["shotPresetsMs"] as? [NSNumber] ?? []).map { $0.intValue }
    }

    /// Fixture for the currently selected effect's row.
    @MainActor
    static func forSelection(_ viewModel: SequencerViewModel) -> FlameFixtureInfo? {
        guard let name = FlamePanelGate.selectedModelName(viewModel) else { return nil }
        return FlameFixtureInfo(viewModel.document.flameFixtureInfo(forModel: name))
    }

    func position(_ n: Int) -> Position? { positions.first { $0.n == n } }
    func preset(_ n: Int) -> Preset? { presets.first { $0.n == n } }

    func clampAngle(_ deg: Int) -> Double {
        min(max(Double(deg), minAllowedDeg), maxAllowedDeg)
    }
}

/// The few panel values the custom rows need, read from the effect's
/// settings with the metadata defaults (same as `FlameParams::FromSettings`).
struct FlameSettings {
    let action: String
    let repeatMode: String
    let preset: Int
    let angle: Int
    let endAngle: Int
    let shotMs: Int

    @MainActor
    init(_ vm: SequencerViewModel) {
        action = vm.settingValue(forKey: FlameKeys.action, defaultValue: "Preset")
        repeatMode = vm.settingValue(forKey: FlameKeys.repeatMode, defaultValue: "Once")
        preset = Int(vm.settingValue(forKey: FlameKeys.preset, defaultValue: "8")) ?? 8
        angle = Int(vm.settingValue(forKey: FlameKeys.angle, defaultValue: "0")) ?? 0
        endAngle = Int(vm.settingValue(forKey: FlameKeys.endAngle, defaultValue: "0")) ?? 0
        shotMs = max(10, Int(vm.settingValue(forKey: FlameKeys.shotMs, defaultValue: "500")) ?? 500)
    }

    func isPresetAction(_ fx: FlameFixtureInfo?) -> Bool {
        action == "Preset" && (fx?.hasPresets ?? true)
    }
}

/// Fixture-specific hiding (desktop `FlamePanel::ValidateWindow`),
/// consulted by `EffectMetadataPanel.isPropertyVisible` for the Flame
/// effect. With no DMX Flame model on the row everything stays visible,
/// like the desktop.
@MainActor
enum FlamePanelGate {
    static func selectedModelName(_ vm: SequencerViewModel) -> String? {
        guard let sel = vm.selectedEffect else { return nil }
        let name = vm.document.rowModelName(at: Int32(sel.rowIndex))
        return name.isEmpty ? nil : name
    }

    static func isVisible(_ propId: String, viewModel vm: SequencerViewModel) -> Bool {
        guard let name = selectedModelName(vm) else { return true }
        let caps = Int(vm.document.flameCapabilities(forModel: name))
        let isFlame = caps & 1 != 0
        let hasAngle = !isFlame || caps & 2 != 0
        let hasPresets = !isFlame || caps & 4 != 0
        switch propId {
        case "Flame_Angle", "Flame_EndAngle", "Flame_Pattern", "Flame_MoveEarly",
             "Flame_FanButtons":
            return hasAngle
        case "Flame_Action":
            return hasAngle || hasPresets
        case "Flame_Preset", "Flame_PresetPicker":
            return hasPresets
        case "Flame_ShotButtons":
            let action = vm.settingValue(forKey: FlameKeys.action, defaultValue: "Preset")
            return !(action == "Preset" && hasPresets)
        default:
            return true
        }
    }
}

private func flameSeconds(_ ms: Int) -> String {
    if ms % 100 == 0 {
        return String(format: "%.1f s", Double(ms) / 1000.0)
    }
    return String(format: "%.2f s", Double(ms) / 1000.0)
}

private func flameDegrees(_ d: Double) -> String {
    let v = Int(d.rounded())
    return v > 0 ? "+\(v)°" : "\(v)°"
}

// MARK: - Flame_PresetPicker

/// The unit's presets by name with each one's real length
/// ("31  Step 1→15 short  (2.57 s)"). Picking one sets Preset #.
struct FlamePresetPickerRowView: View {
    @Environment(SequencerViewModel.self) var viewModel

    var body: some View {
        if let fx = FlameFixtureInfo.forSelection(viewModel), fx.hasPresets {
            let current = FlameSettings(viewModel).preset
            let binding = Binding<Int>(
                get: { current },
                set: { viewModel.setSettingValue(String($0), forKey: FlameKeys.preset) }
            )
            VStack(alignment: .leading, spacing: 2) {
                Text("Preset")
                    .font(.caption)
                Picker("Preset", selection: binding) {
                    if fx.preset(current) == nil {
                        Text("\(current)  (not on this unit)").tag(current)
                    }
                    ForEach(fx.presets, id: \.n) { p in
                        Text("\(p.n)  \(p.name)  (\(flameSeconds(p.lengthMs)))").tag(p.n)
                    }
                }
                .pickerStyle(.menu)
                .labelsHidden()
            }
            .padding(.vertical, 2)
        }
    }
}

// MARK: - Flame_FanButtons

/// One button per named angle position, laid out left→right as the fan
/// (reversed when the model is flipped). Tap sets the angle — with
/// Action = Preset it picks the single SHORT/LONG static preset at that
/// position; with Sweep and "Buttons set end angle" on, it sets the end
/// angle. Long-press → "Add to Angle Pattern" (desktop: shift-click).
struct FlameFanButtonsRowView: View {
    @Environment(SequencerViewModel.self) var viewModel
    // Panel-only toggles, like the desktop's IDD_ checkboxes (not saved
    // with the effect).
    @State private var longSingles = false
    @State private var setEnd = false

    var body: some View {
        if let fx = FlameFixtureInfo.forSelection(viewModel), fx.hasAngle, !fx.positions.isEmpty {
            let s = FlameSettings(viewModel)
            let presetAction = s.isPresetAction(fx)
            let ordered = fx.flip ? Array(fx.positions.reversed()) : fx.positions
            let active = activePosition(fx, s)
            VStack(alignment: .leading, spacing: 4) {
                Text("Angle")
                    .font(.caption)
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 34), spacing: 3)], spacing: 3) {
                    ForEach(ordered, id: \.n) { pos in
                        Button {
                            onFan(pos.n, fx: fx, settings: s, addToPattern: false)
                        } label: {
                            Text("\(pos.n)")
                                .font(.caption2.monospacedDigit())
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .tint(pos.n == active ? .orange : nil)
                        .disabled(!pos.allowed)
                        .help("Position \(pos.n) (\(flameDegrees(pos.deg)))")
                        .contextMenu {
                            Button("Add \(pos.n) to Angle Pattern") {
                                onFan(pos.n, fx: fx, settings: s, addToPattern: true)
                            }
                        }
                    }
                }
                if presetAction {
                    Toggle("Long flame", isOn: $longSingles)
                        .font(.caption)
                }
                if s.action == "Sweep" {
                    Toggle("Buttons set end angle", isOn: $setEnd)
                        .font(.caption)
                }
            }
            .padding(.vertical, 2)
        }
    }

    /// Position the current settings point at, for highlighting.
    private func activePosition(_ fx: FlameFixtureInfo, _ s: FlameSettings) -> Int? {
        if s.isPresetAction(fx) {
            guard let p = fx.preset(s.preset), p.isStatic else { return nil }
            return p.position
        }
        let deg = Double((s.action == "Sweep" && setEnd) ? s.endAngle : s.angle)
        return fx.positions.first { Int($0.deg.rounded()) == Int(deg.rounded()) }?.n
    }

    private func onFan(_ n: Int, fx: FlameFixtureInfo, settings s: FlameSettings, addToPattern: Bool) {
        if addToPattern {
            let cur = viewModel.settingValue(forKey: FlameKeys.pattern, defaultValue: "")
                .trimmingCharacters(in: .whitespaces)
            viewModel.setSettingValue(cur.isEmpty ? "\(n)" : "\(cur),\(n)", forKey: FlameKeys.pattern)
        } else if s.isPresetAction(fx) {
            if let p = fx.presets.first(where: { $0.isStatic && $0.position == n && $0.longFlame == longSingles }) {
                viewModel.setSettingValue(String(p.n), forKey: FlameKeys.preset)
            }
        } else if let pos = fx.position(n) {
            let deg = min(max(Int(pos.deg.rounded()), -105), 105)
            let key = (s.action == "Sweep" && setEnd) ? FlameKeys.endAngle : FlameKeys.angle
            viewModel.setSettingValue(String(deg), forKey: key)
        }
    }
}

// MARK: - Flame_ShotButtons

/// Shot-length quick buttons: the fixture's own list (G-Flame 0.3 / 0.5 /
/// 1.0 / 1.5 s) or 0.25 / 0.5 / 1 / 2 s.
struct FlameShotButtonsRowView: View {
    @Environment(SequencerViewModel.self) var viewModel

    var body: some View {
        let fx = FlameFixtureInfo.forSelection(viewModel)
        let list = (fx?.shotPresetsMs.isEmpty ?? true) ? [250, 500, 1000, 2000] : fx!.shotPresetsMs
        let current = FlameSettings(viewModel).shotMs
        HStack(spacing: 4) {
            ForEach(list, id: \.self) { ms in
                Button(flameSeconds(ms)) {
                    viewModel.setSettingValue(String(ms), forKey: FlameKeys.shotMs)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .tint(ms == current ? .orange : nil)
            }
        }
        .padding(.vertical, 2)
    }
}

// MARK: - Flame_Info

/// Tracks which Flame effect the panel last fitted, so a later length
/// change can be told apart: our own resize, a hand resize (turns Fit
/// length off, desktop parity) or an undo back to the pre-fit length.
/// Desktop keeps the same state on the FlamePanel instance.
@MainActor
private final class FlameLengthTracker {
    static let shared = FlameLengthTracker()
    var row = -1
    var index = -1
    var start = 0
    var end = 0
    var signature = ""
    /// Length the effect had before our last auto resize — an undo
    /// back to it isn't a hand resize.
    var preFitLength: Int? = nil

    func matches(_ sel: SequencerViewModel.EffectSelection) -> Bool {
        row == sel.rowIndex && index == sel.effectIndex
    }
    func track(_ sel: SequencerViewModel.EffectSelection) {
        if !matches(sel) { preFitLength = nil }
        row = sel.rowIndex
        index = sel.effectIndex
        start = sel.startTimeMS
        end = sel.endTimeMS
    }
}

/// What the effect will do and how long it lasts, plus the auto-length /
/// move-early behaviour of desktop `FlamePanel::ApplyLength` /
/// `SetEffectTimeRange`. Length changes go through
/// `SequencerViewModel.moveEffect`, so they're undoable.
struct FlameInfoRowView: View {
    @Environment(SequencerViewModel.self) var viewModel
    @State private var note = ""

    /// Settings that change the fitted length or the lead.
    private var signature: String {
        [FlameKeys.action, FlameKeys.repeatMode, FlameKeys.preset, FlameKeys.angle,
         FlameKeys.pattern, FlameKeys.shotMs, FlameKeys.moveEarly, FlameKeys.fitLength]
            .map { viewModel.settingValue(forKey: $0, defaultValue: "") }
            .joined(separator: "|")
    }

    var body: some View {
        let sel = viewModel.selectedEffect
        let modelName = FlamePanelGate.selectedModelName(viewModel) ?? ""
        let taskKey = sel.map { "\($0.rowIndex):\($0.effectIndex):\($0.startTimeMS):\($0.endTimeMS)|\(signature)" } ?? ""
        Text(infoText)
            .font(.caption2)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.vertical, 2)
            .task(id: modelName) { forceShotWithoutPresets() }
            .task(id: taskKey) { await reconcile() }
    }

    private var infoText: String {
        guard let fx = FlameFixtureInfo.forSelection(viewModel) else {
            return "Put this effect on a DMX Flame model to pick its presets and angles."
        }
        let s = FlameSettings(viewModel)
        var text: String
        if s.isPresetAction(fx) {
            if let p = fx.preset(s.preset) {
                text = "Preset \(p.n): \(p.name), \(flameSeconds(p.lengthMs))."
            } else {
                text = "Preset \(s.preset) isn't on the \(fx.name)."
            }
        } else if s.action == "Sweep" && fx.hasAngle {
            text = "Sweep \(flameDegrees(fx.clampAngle(s.angle))) to \(flameDegrees(fx.clampAngle(s.endAngle))) while firing."
        } else if fx.hasAngle {
            text = "\(flameSeconds(s.shotMs)) shot at \(flameDegrees(fx.clampAngle(s.angle)))."
        } else {
            text = "\(flameSeconds(s.shotMs)) shot."
        }

        if let sel = viewModel.selectedEffect {
            let plan = viewModel.document.flameLengthPlan(forRow: Int32(sel.rowIndex),
                                                          at: Int32(sel.effectIndex))
            let lead = (plan["leadMS"] as? NSNumber)?.intValue ?? 0
            let fit = (plan["fitMS"] as? NSNumber)?.intValue ?? 0
            if fit > 0 {
                text += " Bar fits \(flameSeconds(fit))"
                text += lead > 0 ? " (includes \(lead) ms nozzle move)." : "."
            } else if lead > 0 {
                text += " Starts \(lead) ms early to move the nozzle."
            }
        }
        if fx.id == "ShowvenCircle" {
            text += "\nThe pump arm (CH6) is left to your flame panel."
        } else if fx.id == "GFlame" {
            text += "\nThe safety channel is left to your flame panel."
        }
        if !note.isEmpty {
            text += "\n" + note
        }
        return text
    }

    /// Desktop RefreshForModel: a fixture without presets can't fire one.
    private func forceShotWithoutPresets() {
        guard let fx = FlameFixtureInfo.forSelection(viewModel), !fx.hasPresets else { return }
        if viewModel.settingValue(forKey: FlameKeys.action, defaultValue: "Preset") == "Preset" {
            viewModel.setSettingValue("Shot", forKey: FlameKeys.action)
        }
    }

    private func reconcile() async {
        guard let sel = viewModel.selectedEffect, sel.name == "Flame",
              FlameFixtureInfo.forSelection(viewModel) != nil else { return }
        let tracker = FlameLengthTracker.shared
        let sig = signature

        if tracker.matches(sel) && sig != tracker.signature {
            // A panel change on the same effect (desktop OnUserChange).
            // Short wait so a slider drag fits once, at the end — the
            // task is cancelled and restarted while values keep coming.
            try? await Task.sleep(for: .milliseconds(300))
            if Task.isCancelled { return }
            tracker.signature = sig
            note = ""
            applyLength()
            return
        }
        tracker.signature = sig

        let plan = viewModel.document.flameLengthPlan(forRow: Int32(sel.rowIndex),
                                                      at: Int32(sel.effectIndex))
        let fit = (plan["fitMS"] as? NSNumber)?.intValue ?? 0
        let len = sel.endTimeMS - sel.startTimeMS
        if tracker.matches(sel) {
            if fit > 0 && len != tracker.end - tracker.start && len != fit
                && len != tracker.preFitLength {
                // resized by hand: stop fitting this effect
                viewModel.setSettingValue("0", forKey: FlameKeys.fitLength)
                tracker.signature = signature
                note = "Fit length turned off because the effect was resized by hand."
            }
        } else if fit > 0 && len != fit {
            // e.g. just dropped from the palette: fit it to the preset
            tracker.track(sel)
            note = ""
            applyLength()
            return
        }
        tracker.track(sel)
    }

    /// Desktop `FlamePanel::ApplyLength`. The bridge works out the target
    /// times (lead kept off the fire time, fitted end, lock / neighbour /
    /// sequence-end clamps); this applies them on the undo stack and
    /// stores the new lead.
    private func applyLength() {
        guard let sel = viewModel.selectedEffect, sel.name == "Flame" else { return }
        let plan = viewModel.document.flameLengthPlan(forRow: Int32(sel.rowIndex),
                                                      at: Int32(sel.effectIndex))
        guard !plan.isEmpty else { return }
        let needsMove = (plan["needsMove"] as? NSNumber)?.boolValue ?? false
        let canMove = (plan["canMove"] as? NSNumber)?.boolValue ?? false
        let s = (plan["startMS"] as? NSNumber)?.intValue ?? sel.startTimeMS
        let e = (plan["endMS"] as? NSNumber)?.intValue ?? sel.endTimeMS
        let lead = (plan["leadMS"] as? NSNumber)?.intValue ?? 0
        let oldLead = (plan["oldLeadMS"] as? NSNumber)?.intValue ?? 0
        let planNote = plan["note"] as? String ?? ""

        let tracker = FlameLengthTracker.shared
        var moved = true
        if needsMove {
            moved = false
            if canMove {
                let before = sel.endTimeMS - sel.startTimeMS
                viewModel.moveEffect(rowIndex: sel.rowIndex, effectIndex: sel.effectIndex,
                                     newStartMS: s, newEndMS: e)
                if let now = viewModel.selectedEffect,
                   now.startTimeMS == s, now.endTimeMS == e {
                    moved = true
                    tracker.track(now)
                    tracker.preFitLength = before
                }
            }
            note = planNote
        }
        if moved && lead != oldLead {
            viewModel.setSettingValue(String(lead), forKey: FlameKeys.leadMs,
                                      suppressIfDefault: "0")
        }
    }
}
