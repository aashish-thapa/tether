//
//  TetherMessage.swift
//  TetherFramework
//
//  Codable models matching the tetherd newline-delimited JSON protocol.
//

import Foundation

#if canImport(UIKit)
import UIKit
#endif

// MARK: - Command Enum

// All known command types in the Tether protocol.
public enum TetherCommand: String, Codable, Sendable {
    // Client → Daemon
    case clipboardSet = "clipboard_set"
    case clipboardGet = "clipboard_get"
    case openUrl = "open_url"
    case fileStart = "file_start"
    case fileChunk = "file_chunk"
    case fileEnd = "file_end"
    case pairRequest = "pair_request"
    case newOtp = "new_otp"
    // Both directions: announces optional protocol features.
    case hello = "hello"

    // Daemon → Client
    case clipboardUpdated = "clipboard_updated"
    case clipboardContent = "clipboard_content"
    case fileStatus = "file_status"
    case pairPending = "pair_pending"
    case pairAccepted = "pair_accepted"
    case error = "error"

    // Commands a peer may send before it is pinned as a known host. Inbound means
    // the peer dialled us and the local user is the approver, so only its request
    // is allowed. Outbound means we asked and the peer's user decides, so only its
    // verdict is. Mirrors the boundary tetherd enforces.
    public static func allowedWhileUnpaired(_ command: TetherCommand?, inbound: Bool) -> Bool {
        guard let command else { return false }
        return inbound
            ? command == .pairRequest
            : command == .pairAccepted || command == .pairPending || command == .error
    }
}

// MARK: - Message

// A single Tether protocol message.
//
// Uses a flat structure with optional fields — the daemon's JSON payloads
// share a unified shape where only the relevant fields are present
// for each command type.
public struct TetherMessage: Codable, Sendable {
    public let command: String

    // Clipboard
    public var content: String?

    // File transfer
    public var filename: String?
    public var size: Int64?
    public var transferId: String?
    public var chunkIndex: Int?
    public var data: String? // Base64 encoded chunk
    // Set on file_start when the file is a clipboard image: "set", "updated" or "content".
    public var clipboard: String?

    // hello
    public var features: [String]?
    // "phone", "tablet" or "computer", so the desktop can show the right icon.
    public var device: String?

    // Pairing
    public var deviceName: String?

    // OTP (new_otp command uses "otp" and optional "source" keys)
    public var otp: String?
    public var source: String?

    // Status / Error
    public var status: String?
    public var message: String?

    public enum CodingKeys: String, CodingKey {
        case command, content, filename, size
        case transferId = "transfer_id"
        case chunkIndex = "chunk_index"
        case data, clipboard, features, device
        case deviceName = "device_name"
        case otp, source
        case status, message
    }

    public init(
        command: String,
        content: String? = nil,
        filename: String? = nil,
        size: Int64? = nil,
        transferId: String? = nil,
        chunkIndex: Int? = nil,
        data: String? = nil,
        deviceName: String? = nil,
        otp: String? = nil,
        source: String? = nil,
        status: String? = nil,
        message: String? = nil,
        clipboard: String? = nil,
        features: [String]? = nil,
        device: String? = nil
    ) {
        self.command = command
        self.content = content
        self.filename = filename
        self.size = size
        self.transferId = transferId
        self.chunkIndex = chunkIndex
        self.data = data
        self.deviceName = deviceName
        self.otp = otp
        self.source = source
        self.status = status
        self.message = message
        self.clipboard = clipboard
        self.features = features
        self.device = device
    }
}

// MARK: - Convenience Initializers

extension TetherMessage {
    // Create a `clipboard_set` message.
    public static func clipboardSet(_ text: String) -> TetherMessage {
        TetherMessage(command: TetherCommand.clipboardSet.rawValue, content: text)
    }

    // Create an `open_url` message; tetherd opens only http(s) links.
    public static func openUrl(_ url: String) -> TetherMessage {
        TetherMessage(command: TetherCommand.openUrl.rawValue, content: url)
    }

    static var localDeviceKind: String {
        #if canImport(UIKit)
        UIDevice.current.userInterfaceIdiom == .pad ? "tablet" : "phone"
        #else
        "computer"
        #endif
    }

    // Create a `hello` announcing the optional features this client supports.
    public static func hello(features: [String]) -> TetherMessage {
        TetherMessage(command: TetherCommand.hello.rawValue, features: features, device: localDeviceKind)
    }

    // Create a `clipboard_get` request.
    public static func clipboardGet() -> TetherMessage {
        TetherMessage(command: TetherCommand.clipboardGet.rawValue)
    }

    // Create a `new_otp` message.
    // Uses the `otp` key to match the daemon's Unix socket handler and TCP handler.
    public static func newOtp(_ code: String, source: String = "iPhone Share") -> TetherMessage {
        TetherMessage(command: TetherCommand.newOtp.rawValue, otp: code, source: source)
    }

    // Create a `pair_request` message.
    public static func pairRequest(deviceName: String) -> TetherMessage {
        TetherMessage(command: TetherCommand.pairRequest.rawValue, deviceName: deviceName)
    }

    // Create a `pair_accepted` message (replying to a request).
    public static var pairAccepted: TetherMessage {
        TetherMessage(command: TetherCommand.pairAccepted.rawValue)
    }

    // Create a `file_start` message.
    public static func fileStart(filename: String, size: Int64, transferId: String, clipboard: String? = nil) -> TetherMessage {
        TetherMessage(
            command: TetherCommand.fileStart.rawValue,
            filename: filename,
            size: size,
            transferId: transferId,
            clipboard: clipboard
        )
    }

    // Create a `file_chunk` message.
    public static func fileChunk(transferId: String, chunkIndex: Int, data: String) -> TetherMessage {
        TetherMessage(
            command: TetherCommand.fileChunk.rawValue,
            transferId: transferId,
            chunkIndex: chunkIndex,
            data: data
        )
    }

    // Create a `file_end` message.
    public static func fileEnd(transferId: String) -> TetherMessage {
        TetherMessage(command: TetherCommand.fileEnd.rawValue, transferId: transferId)
    }

    // The parsed command enum, if recognized.
    public var parsedCommand: TetherCommand? {
        TetherCommand(rawValue: command)
    }
}
