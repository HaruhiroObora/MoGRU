// Copyright 2026 Haruhiro Obora.
// Candidate layout adapted from nagi (https://github.com/nvalleo/nagi),
// licensed under Apache License 2.0; see Nagi-LICENSE.txt.

import AppKit
import Combine
import SwiftUI

/// A candidate supplied by MoGRU's existing renderer protocol.
@objc(MoGRUCandidateItem)
public final class MoGRUCandidateItem: NSObject, Identifiable {
    @objc public let index: Int
    @objc public let candidateID: Int32
    @objc public let text: String
    @objc public let shortcut: String
    @objc public let descriptionText: String
    @objc public let hasSubCandidates: Bool

    public var id: Int { index }

    /// Creates the display data for one candidate on the current page.
    @objc public init(
        index: Int,
        candidateID: Int32,
        text: String,
        shortcut: String,
        descriptionText: String,
        hasSubCandidates: Bool
    ) {
        self.index = index
        self.candidateID = candidateID
        self.text = text
        self.shortcut = shortcut
        self.descriptionText = descriptionText
        self.hasSubCandidates = hasSubCandidates
    }

    /// Whether this candidate should use nagi's compact pictograph grid.
    public var isPictograph: Bool {
        descriptionText.hasPrefix("絵文字") || descriptionText.hasPrefix("顔文字")
    }
}

/// Receives mouse selections and forwards them to the existing Mozc session.
@objc(MoGRUCandidateSelectionDelegate)
public protocol MoGRUCandidateSelectionDelegate: AnyObject {
    /// Selects the candidate identified by Mozc's stable candidate ID.
    func candidateView(_ view: MoGRUCandidateView, didSelectCandidateWithID candidateID: Int32)
}

/// Observable state shared by the AppKit host and the SwiftUI candidate list.
private final class CandidateListState: ObservableObject {
    @Published var candidates: [MoGRUCandidateItem] = []
    @Published var focusedIndex: Int = -1
    @Published var footer = ""
    var selectCandidate: ((Int32) -> Void)?
}

/// A nonactivating candidate view embedded in MoGRU's renderer window.
@objc(MoGRUCandidateView)
public final class MoGRUCandidateView: NSView {
    private let state: CandidateListState
    private let hostingView: NSHostingView<CandidateListView>

    @objc public var selectionDelegate: MoGRUCandidateSelectionDelegate?

    /// Creates a SwiftUI backed view for the existing AppKit renderer window.
    public override init(frame frameRect: NSRect) {
        let state = CandidateListState()
        self.state = state
        hostingView = NSHostingView(rootView: CandidateListView(state: state))
        super.init(frame: frameRect)
        hostingView.autoresizingMask = [.width, .height]
        hostingView.frame = bounds
        addSubview(hostingView)
        state.selectCandidate = { [weak self] candidateID in
            guard let self else { return }
            self.selectionDelegate?.candidateView(self, didSelectCandidateWithID: candidateID)
        }
    }

    /// Interface Builder is not used by the renderer.
    public required init?(coder: NSCoder) {
        return nil
    }

    /// Refreshes the displayed page, focus, and footer from one renderer update.
    @objc public func update(
        with candidates: [MoGRUCandidateItem],
        focusedIndex: Int,
        footer: String
    ) {
        state.candidates = candidates
        state.focusedIndex = focusedIndex
        state.footer = footer
        hostingView.invalidateIntrinsicContentSize()
        hostingView.needsLayout = true
    }

    /// Returns the SwiftUI content size for the surrounding NSPanel.
    @objc public var preferredSize: NSSize {
        // Resolve pending SwiftUI layout before positioning the panel.
        hostingView.layoutSubtreeIfNeeded()
        let fitting = hostingView.fittingSize
        return NSSize(width: max(180, fitting.width), height: min(300, fitting.height))
    }

    /// Returns a candidate's current row in AppKit coordinates for a submenu.
    @objc public func frameForCandidate(at index: Int) -> NSRect {
        CandidateListView(state: state).frame(forCandidateAt: index, in: preferredSize) ?? .zero
    }
}

/// Nagi's candidate layout adapted to MoGRU's renderer data.
private struct CandidateListView: View {
    @ObservedObject var state: CandidateListState

    private static let fontSize: CGFloat = 15
    private static let rowHeight: CGFloat = 27
    private static let gridColumns = 9
    private static let gridCellSize: CGFloat = 26
    private static let maxRows = 10

    private enum Row: Identifiable {
        case text(MoGRUCandidateItem)
        case grid([MoGRUCandidateItem])

        var id: Int {
            switch self {
            case .text(let candidate): return candidate.index
            case .grid(let candidates): return candidates.first?.index ?? -1
            }
        }

        var height: CGFloat {
            switch self {
            case .text: return CandidateListView.rowHeight
            case .grid: return CandidateListView.gridCellSize + 4
            }
        }

        func contains(_ index: Int) -> Bool {
            switch self {
            case .text(let candidate): return candidate.index == index
            case .grid(let candidates): return candidates.contains { $0.index == index }
            }
        }
    }

    private var rows: [Row] {
        let groups = state.candidates.reduce(into: [[MoGRUCandidateItem]]()) { result, candidate in
            if result.last?.first?.isPictograph == candidate.isPictograph {
                result[result.count - 1].append(candidate)
            } else {
                result.append([candidate])
            }
        }
        return groups.flatMap { group -> [Row] in
            guard group.first?.isPictograph == true else { return group.map(Row.text) }
            return stride(from: 0, to: group.count, by: Self.gridColumns).map { start in
                .grid(Array(group[start..<min(start + Self.gridColumns, group.count)]))
            }
        }
    }

    private var visibleRows: [Row] {
        let allRows = rows
        guard allRows.count > Self.maxRows else { return allRows }
        let focusedRow = allRows.firstIndex { $0.contains(state.focusedIndex) } ?? 0
        let start = min(max(0, focusedRow - Self.maxRows / 2), allRows.count - Self.maxRows)
        return Array(allRows[start..<(start + Self.maxRows)])
    }

    /// Finds the visible row for a candidate in AppKit's bottom-up coordinates.
    func frame(forCandidateAt index: Int, in size: NSSize) -> NSRect? {
        var topOffset: CGFloat = 4
        for row in visibleRows {
            if row.contains(index) {
                return NSRect(
                    x: 0,
                    y: size.height - topOffset - row.height,
                    width: size.width,
                    height: row.height
                )
            }
            topOffset += row.height
        }
        return nil
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(visibleRows) { row in
                switch row {
                case .text(let candidate): textRow(candidate)
                case .grid(let candidates): gridRow(candidates)
                }
            }
            if !state.footer.isEmpty {
                Text(state.footer)
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
            }
        }
        .padding(4)
        .background(.regularMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .fixedSize(horizontal: true, vertical: false)
    }

    @ViewBuilder
    private func textRow(_ candidate: MoGRUCandidateItem) -> some View {
        HStack(spacing: 6) {
            Text(candidate.shortcut.isEmpty ? String(candidate.index + 1) : candidate.shortcut)
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
                .frame(width: 20, alignment: .trailing)
            Text(candidate.text)
                .font(.system(size: Self.fontSize))
            if candidate.hasSubCandidates {
                Image(systemName: "chevron.right")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundStyle(.secondary)
            }
            if !candidate.descriptionText.isEmpty {
                Text(candidate.descriptionText)
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(.horizontal, 8)
        .frame(minHeight: Self.rowHeight)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(candidate.index == state.focusedIndex ? Color.accentColor.opacity(0.25) : .clear)
        .contentShape(Rectangle())
        .onTapGesture { state.selectCandidate?(candidate.candidateID) }
        .accessibilityLabel("\(candidate.index + 1). \(candidate.text)")
    }

    @ViewBuilder
    private func gridRow(_ candidates: [MoGRUCandidateItem]) -> some View {
        HStack(spacing: 4) {
            ForEach(candidates) { candidate in
                Text(candidate.text)
                    .font(.system(size: 20))
                    .frame(width: Self.gridCellSize, height: Self.gridCellSize)
                    .background(candidate.index == state.focusedIndex ? Color.accentColor.opacity(0.25) : .clear)
                    .clipShape(RoundedRectangle(cornerRadius: 6))
                    .contentShape(Rectangle())
                    .onTapGesture { state.selectCandidate?(candidate.candidateID) }
                    .accessibilityLabel(candidate.text)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 2)
    }
}
