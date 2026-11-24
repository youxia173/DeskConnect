package com.deskconnect.app.protocol

import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class RemoteInputCodecTest {
    @Test
    fun functionKeysAndPhysicalButtonsRemainUnsigned() {
        val bytes = payload { writeShort(0xEF51); writeShort(0xA001); writeShort(0xF123) }
        assertEquals(RemoteInputEvent.Key(0xEF51, 0xA001, 0xF123, true), decode(Msg.DKDN, bytes))
        assertEquals(RemoteInputEvent.Key(0xEF51, 0xA001, 0xF123, false), decode(Msg.DKUP, bytes))
    }

    @Test
    fun enterPreservesSequenceAndSignedCoordinates() {
        val bytes = payload { writeShort(-10); writeShort(2300); writeInt(123456); writeShort(0x8001) }
        assertEquals(RemoteInputEvent.Enter(-10, 2300, 123456, 0x8001), decode(Msg.CINN, bytes))
        assertEquals(RemoteInputEvent.Leave, decode(Msg.COUT, byteArrayOf()))
    }

    @Test
    fun absoluteAndRelativeMovementKeepSignedAxes() {
        val bytes = payload { writeShort(-300); writeShort(700) }
        assertEquals(RemoteInputEvent.Move(-300, 700, false), decode(Msg.DMMV, bytes))
        assertEquals(RemoteInputEvent.Move(-300, 700, true), decode(Msg.DMRM, bytes))
    }

    @Test
    fun mouseButtonsAndWheelUseBarrierValues() {
        assertEquals(RemoteInputEvent.Button(2, true), decode(Msg.DMDN, byteArrayOf(2)))
        assertEquals(RemoteInputEvent.Button(3, false), decode(Msg.DMUP, byteArrayOf(3)))
        val bytes = payload { writeShort(-120); writeShort(240) }
        assertEquals(RemoteInputEvent.Wheel(-120, 240), decode(Msg.DMWM, bytes))
    }

    @Test
    fun repeatSupportsBothStockBarrierAndDeskConnectLanguageExtension() {
        val fixed = payload { writeShort(65); writeShort(1); writeShort(3); writeShort(38) }
        assertEquals(RemoteInputEvent.Key(65, 1, 38, true, repeat = 3), decode(Msg.DKRP, fixed))
        val extended = fixed + payload { writeLengthString("en-US") }
        assertEquals(RemoteInputEvent.Key(65, 1, 38, true, repeat = 3, language = "en-US"), decode(Msg.DKRP, extended))
        val down = payload { writeShort(65); writeShort(1); writeShort(38); writeLengthString("zh-CN") }
        assertEquals(RemoteInputEvent.Key(65, 1, 38, true, language = "zh-CN"), decode(Msg.DKDL, down))
    }

    @Test
    fun oldProtocolOmitsPhysicalButtonAndHorizontalWheel() {
        assertEquals(RemoteInputEvent.Key(65, 0, 65, true),
            RemoteInputCodec.decode(Msg.DKDN, payload { writeShort(65); writeShort(0) }, 0))
        assertEquals(RemoteInputEvent.Key(65, 0, 65, true, repeat = 2),
            RemoteInputCodec.decode(Msg.DKRP, payload { writeShort(65); writeShort(0); writeShort(2) }, 0))
        assertEquals(RemoteInputEvent.Wheel(0, -120),
            RemoteInputCodec.decode(Msg.DMWM, payload { writeShort(-120) }, 2))
    }

    @Test
    fun truncatedOrTrailingInputBytesCannotBecomeEvents() {
        val messages = listOf(
            Msg.CINN to payload { writeShort(1); writeShort(2); writeInt(3); writeShort(4) },
            Msg.DMMV to payload { writeShort(1); writeShort(2) },
            Msg.DMRM to payload { writeShort(1); writeShort(2) },
            Msg.DMDN to byteArrayOf(1),
            Msg.DMUP to byteArrayOf(1),
            Msg.DMWM to payload { writeShort(1); writeShort(2) },
            Msg.DKDN to payload { writeShort(65); writeShort(0); writeShort(38) },
            Msg.DKUP to payload { writeShort(65); writeShort(0); writeShort(38) },
            Msg.DKDL to payload { writeShort(65); writeShort(0); writeShort(38); writeLengthString("en") },
        )
        for ((code, valid) in messages) {
            for (length in valid.indices) {
                assertThrows("$code truncated at $length", ProtocolException::class.java) {
                    decode(code, valid.copyOf(length))
                }
            }
            assertThrows("$code trailing data", ProtocolException::class.java) {
                decode(code, valid + byteArrayOf(0))
            }
        }
        assertThrows(ProtocolException::class.java) { decode(Msg.COUT, byteArrayOf(0)) }
        assertThrows(ProtocolException::class.java) { decode(Msg.DMDN, byteArrayOf(0)) }
    }

    @Test
    fun malformedRepeatAndLanguageLengthsAreRejectedBeforeDispatch() {
        val fixed = payload { writeShort(65); writeShort(0); writeShort(2); writeShort(38) }
        for (length in fixed.indices) {
            assertThrows(ProtocolException::class.java) { decode(Msg.DKRP, fixed.copyOf(length)) }
        }
        for (suffix in listOf(byteArrayOf(0), payload { writeInt(-1) }, payload { writeInt(Int.MAX_VALUE) },
            payload { writeInt(2); writeByte(65) }, payload { writeInt(0); writeByte(65) })) {
            assertThrows(ProtocolException::class.java) { decode(Msg.DKRP, fixed + suffix) }
        }
    }

    @Test
    fun nonInputPacketsRemainForClipboardAndKeepaliveHandlers() {
        assertNull(decode(Msg.CALV, byteArrayOf()))
        assertNull(decode(Msg.DCLP, byteArrayOf(1, 2, 3)))
    }

    private fun decode(code: String, body: ByteArray) = RemoteInputCodec.decode(code, body, 8)

    private fun payload(build: DataOutputStream.() -> Unit): ByteArray {
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use { it.build() }
        return bytes.toByteArray()
    }
}
