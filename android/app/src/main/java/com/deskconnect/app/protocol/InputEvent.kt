package com.deskconnect.app.protocol

import java.io.DataInputStream
import java.nio.charset.StandardCharsets

/** Display coordinates sent to the server in DINF. */
data class RemoteScreenInfo(val width: Int, val height: Int, val x: Int = 0, val y: Int = 0)

/** Barrier values are preserved here; the platform bridge maps them to Android input. */
sealed interface RemoteInputEvent {
    data class Enter(val x: Int, val y: Int, val sequence: Int, val modifiers: Int) : RemoteInputEvent
    data object Leave : RemoteInputEvent
    data class Move(val x: Int, val y: Int, val relative: Boolean) : RemoteInputEvent
    /** Barrier button IDs: 1 = left, 2 = middle, 3 = right. */
    data class Button(val button: Int, val down: Boolean) : RemoteInputEvent
    /** Barrier wheel deltas use 120 units per conventional wheel notch. */
    data class Wheel(val dx: Int, val dy: Int) : RemoteInputEvent
    data class Key(
        val keyId: Int,
        val modifiers: Int,
        val button: Int,
        val down: Boolean,
        /** Number of repeats in this packet, not an Android cumulative repeat count. */
        val repeat: Int = 0,
        val language: String = "",
    ) : RemoteInputEvent
}

/** Decode an entire input packet before any event is allowed to reach the device. */
internal object RemoteInputCodec {
    fun decode(code: String, body: ByteArray, protocolMinor: Int): RemoteInputEvent? {
        val input = body.asDataInput()
        fun requireSize(size: Int) {
            if (body.size != size) throw ProtocolException("invalid $code length: ${body.size}, expected $size")
        }
        fun requireMinimum(size: Int) {
            if (body.size < size) throw ProtocolException("short $code input packet: ${body.size}")
        }
        return when (code) {
            Msg.CINN -> {
                requireSize(10)
                RemoteInputEvent.Enter(input.readInt16Be(), input.readInt16Be(), input.readInt(), input.readUnsignedShort())
            }
            Msg.COUT -> {
                requireSize(0)
                RemoteInputEvent.Leave
            }
            Msg.DMMV, Msg.DMRM -> {
                requireSize(4)
                RemoteInputEvent.Move(input.readInt16Be(), input.readInt16Be(), code == Msg.DMRM)
            }
            Msg.DMDN, Msg.DMUP -> {
                requireSize(1)
                val button = input.readUnsignedByte()
                if (button == 0) throw ProtocolException("invalid $code button: 0")
                RemoteInputEvent.Button(button, code == Msg.DMDN)
            }
            Msg.DMWM -> {
                requireSize(if (protocolMinor < 3) 2 else 4)
                val dx = if (protocolMinor < 3) 0 else input.readInt16Be()
                RemoteInputEvent.Wheel(dx, input.readInt16Be())
            }
            Msg.DKDN, Msg.DKUP -> {
                requireSize(if (protocolMinor == 0) 4 else 6)
                val key = input.readUnsignedShort()
                val modifiers = input.readUnsignedShort()
                val button = if (protocolMinor == 0) key else input.readUnsignedShort()
                RemoteInputEvent.Key(key, modifiers, button, code == Msg.DKDN)
            }
            Msg.DKDL -> {
                requireMinimum(10)
                val key = input.readUnsignedShort()
                val modifiers = input.readUnsignedShort()
                val button = input.readUnsignedShort()
                RemoteInputEvent.Key(key, modifiers, button, true, language = input.readInputLanguage(code))
            }
            Msg.DKRP -> {
                val fixedSize = if (protocolMinor == 0) 6 else 8
                requireMinimum(fixedSize)
                val key = input.readUnsignedShort()
                val modifiers = input.readUnsignedShort()
                val repeats = input.readUnsignedShort()
                val button = if (protocolMinor == 0) key else input.readUnsignedShort()
                // Stock Barrier omits the language; DeskConnect appends a length-prefixed string.
                val language = if (input.available() == 0) "" else input.readInputLanguage(code)
                RemoteInputEvent.Key(key, modifiers, button, true, repeat = repeats, language = language)
            }
            else -> null
        }
    }

    private fun DataInputStream.readInputLanguage(code: String): String {
        if (available() < 4) throw ProtocolException("short $code language length")
        val length = readInt()
        if (length < 0 || length > 256 || length != available()) {
            throw ProtocolException("invalid $code language length: $length")
        }
        val bytes = ByteArray(length)
        readFully(bytes)
        return String(bytes, StandardCharsets.UTF_8)
    }
}
