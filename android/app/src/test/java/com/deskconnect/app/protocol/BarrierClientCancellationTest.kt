package com.deskconnect.app.protocol

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BarrierClientCancellationTest {
    @Test
    fun stopBeforeConnectCannotBeUndoneByStartingTheAttempt() {
        val callbacks = mutableListOf<String>()
        val listener = object : BarrierClientListener {
            override fun onConnected(protocol: String, major: Int, minor: Int) { callbacks += "connected" }
            override fun onDisconnected(reason: String?) { callbacks += "disconnected" }
            override fun onLog(message: String) { callbacks += "log" }
            override fun onClipboardText(text: String) = Unit
            override fun onFileReceived(fileName: String, absolutePath: String) = Unit
            override fun onFileProgress(message: String) = Unit
        }
        val fileStore = object : ReceivedFileStore {
            override fun open(fileName: String): Pair<String, java.io.OutputStream> = error("unexpected file")
        }
        val client = BarrierClient(listener, fileStore)
        // Mirrors publishing clientRef, then receiving stop before the worker calls connect.
        client.disconnect()
        client.connect("", -1, "cancelled-attempt", useTls = false)
        assertFalse(client.connected)
        assertTrue("A cancelled attempt must not start networking or notify connected", callbacks.isEmpty())
        // A disposed attempt cannot be revived; reconnect uses a new client instance.
        client.connect("", -1, "cancelled-attempt", useTls = false)
        assertFalse(client.connected)
        assertTrue(callbacks.isEmpty())
    }
}
