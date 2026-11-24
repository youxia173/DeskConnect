package com.deskconnect.app.protocol

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.util.concurrent.LinkedBlockingQueue
import org.junit.Assert.*
import org.junit.Test

class RecoveryRegistrationTest {
    private class Fixture {
        var enabled = true
        var registrations = 0
        val id = "0123456789abcdef0123456789abcdef"
        val client = BarrierClient(object : BarrierClientListener {
            override fun onConnected(protocol: String, major: Int, minor: Int) = Unit
            override fun onDisconnected(reason: String?) = Unit
            override fun onLog(message: String) = Unit
            override fun onClipboardText(text: String) = Unit
            override fun onFileReceived(fileName: String, absolutePath: String) = Unit
            override fun onFileProgress(message: String) = Unit
            override fun beginRecoveryRegistration(): String { registrations++; return id }
            override fun isRecoveryRequested() = enabled
        }, object : ReceivedFileStore {
            override fun open(fileName: String): Pair<String, java.io.OutputStream> = error("unexpected file")
        })

        fun options(version: Int? = null) {
            val bytes = ByteArrayOutputStream()
            DataOutputStream(bytes).use {
                it.writeRaw("DSOP")
                it.writeInt(if (version == null) 0 else 2)
                if (version != null) { it.writeInt(0x44435256); it.writeInt(version) }
            }
            val method = client.javaClass.getDeclaredMethod("handlePacket", PacketStream::class.java, ByteArray::class.java)
            method.isAccessible = true
            method.invoke(client, PacketStream(ByteArrayInputStream(byteArrayOf()), ByteArrayOutputStream()), bytes.toByteArray())
        }

        fun registrationsSent(): List<ByteArray> {
            val field = client.javaClass.getDeclaredField("outbound").apply { isAccessible = true }
            @Suppress("UNCHECKED_CAST") val queue = field.get(client) as LinkedBlockingQueue<ByteArray>
            val packets = mutableListOf<ByteArray>()
            queue.drainTo(packets)
            return packets.filter { String(it.take(4).toByteArray()) == "DCAR" }
        }
    }

    @Test fun oldOrUnsupportedServersReceiveNoRecoveryPackets() {
        val f = Fixture()
        f.options()
        f.options(2)
        f.client.refreshRecoveryRegistration()
        assertEquals(0, f.registrations)
        assertTrue(f.registrationsSent().isEmpty())
    }

    @Test fun negotiatedRegistrationIsFixedSizeAndSettingsCanRevokeWithoutHeartbeat() {
        val f = Fixture()
        f.options(1)
        val packet = f.registrationsSent().single()
        assertEquals(37, packet.size)
        assertEquals(1, packet[4].toInt())
        assertEquals(f.id, String(packet.copyOfRange(5, packet.size)))
        f.options(1)
        assertEquals(1, f.registrations)
        assertTrue(f.registrationsSent().isEmpty())
        f.enabled = false
        f.client.refreshRecoveryRegistration()
        assertEquals(0, f.registrationsSent().single()[4].toInt())
        f.client.refreshRecoveryRegistration()
        assertTrue(f.registrationsSent().isEmpty())
        f.enabled = true
        f.client.refreshRecoveryRegistration()
        assertEquals(1, f.registrationsSent().single()[4].toInt())
    }

    @Test fun disabledPhoneDoesNotRegisterForActivation() {
        val f = Fixture()
        f.enabled = false
        f.options(1)
        assertEquals(0, f.registrationsSent().single()[4].toInt())
    }
}
