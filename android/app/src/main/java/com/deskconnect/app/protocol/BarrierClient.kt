package com.deskconnect.app.protocol

import android.util.Log
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.charset.StandardCharsets
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.min

interface BarrierClientListener {
    fun onConnected(protocol: String, major: Int, minor: Int)
    fun onDisconnected(reason: String?)
    fun onLog(message: String)
    fun onClipboardText(text: String)
    fun onClipboardAck(clipboardId: Int) {}
    fun onFileReceived(fileName: String, absolutePath: String)
    fun onFileProgress(message: String)
    fun onUntrustedServerFingerprint(fingerprintHex: String) {}
}

interface ReceivedFileStore {
    fun open(fileName: String): Pair<String, java.io.OutputStream>
}

/**
 * Lightweight Barrier/Deskflow client: handshake, keepalive, text clipboard,
 * and bidirectional file transfer. Input events are ignored.
 */
class BarrierClient(
    private val listener: BarrierClientListener,
    private val fileStore: ReceivedFileStore,
) {
    private val running = AtomicBoolean(false)
    private var socket: Socket? = null
    private var packets: PacketStream? = null
    private var writerThread: Thread? = null
    /** Outbound packets written only from the dedicated writer thread. */
    private val outbound = LinkedBlockingQueue<ByteArray>()
    /** Last CINN enter sequence from the server (0 if never entered). */
    private var enterSequence = 0
    /** Monotonic outbound clipboard sequence; must not lag behind server clipboardSeqNum. */
    private var clipboardSequence = 0
    private val clipboardSeqLock = Any()
    @Volatile
    var suppressClipboardEcho = false

    private var clipboardExpected = -1
    private val clipboardBytes = ByteArrayOutputGrowable()

    private var receiveNames: List<String> = emptyList()
    private var receiveIndex = 0
    private var receiveExpected = 0L
    private var receiveWritten = 0L
    private var receivePath: String? = null
    private var receiveStream: java.io.OutputStream? = null

    @Volatile
    var connected: Boolean = false
        private set

    private var lastProtocol = "Barrier"
    private var lastMajor = PROTOCOL_MAJOR
    private var lastMinor = PROTOCOL_MINOR

    fun connect(
        host: String,
        port: Int,
        screenName: String,
        useTls: Boolean = true,
        trustedServerFingerprints: Set<String> = emptySet(),
        tlsIdentityDir: java.io.File? = null,
    ) {
        if (!running.compareAndSet(false, true)) {
            return
        }
        try {
            val mode = if (useTls) "TLS" else "plaintext"
            log("connecting to $host:$port as \"$screenName\" ($mode)")
            val sock = if (useTls) {
                val dir = tlsIdentityDir
                    ?: throw ProtocolException("TLS identity directory required")
                val identity = TlsSupport.ensureClientIdentity(dir)
                log("client fingerprint: ${FingerprintUtil.formatColon(identity.fingerprintHex)}")
                TlsSupport.connect(
                    host = host,
                    port = port,
                    connectTimeoutMs = CONNECT_TIMEOUT_MS,
                    trustedServerFingerprints = trustedServerFingerprints,
                    identity = identity,
                    onServerFingerprint = { fp ->
                        log("server fingerprint: ${FingerprintUtil.formatColon(fp)}")
                    },
                )
            } else {
                Socket().also {
                    it.tcpNoDelay = true
                    it.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
                }
            }
            socket = sock
            val stream = PacketStream(sock.getInputStream(), sock.getOutputStream())
            packets = stream
            outbound.clear()

            handshake(stream, screenName)
            startWriter(stream)
            connected = true
            listener.onConnected(lastProtocol, lastMajor, lastMinor)
            log("handshake ok ($lastProtocol $lastMajor.$lastMinor)")

            var endReason: String? = "disconnected"
            while (running.get()) {
                val packet = stream.readPacket()
                if (!handlePacket(stream, packet)) {
                    endReason = "server closed"
                    break
                }
            }
            if (!running.get()) {
                endReason = "disconnected"
            }
            listener.onDisconnected(endReason)
        } catch (e: UntrustedFingerprintException) {
            log("untrusted server fingerprint: ${FingerprintUtil.formatColon(e.fingerprintHex)}")
            listener.onUntrustedServerFingerprint(e.fingerprintHex)
            listener.onDisconnected("untrusted fingerprint")
        } catch (e: Exception) {
            if (running.get()) {
                log("error: ${e.message}")
            }
            listener.onDisconnected(e.message)
        } finally {
            cleanup()
        }
    }

    fun disconnect() {
        running.set(false)
        try {
            socket?.close()
        } catch (_: Exception) {
        }
    }

    fun sendClipboardText(text: String): Boolean {
        if (suppressClipboardEcho || !connected || packets == null) return false
        val data = ClipboardCodec.marshallText(text)
        val seq = nextClipboardSeq()
        return try {
            enqueuePacket {
                writeRaw(Msg.CCLP)
                writeUInt8(CLIPBOARD_ID)
                writeInt32Be(seq)
            }
            enqueueClipboardChunks(data, seq)
            log("queued clipboard text (${text.length} chars, seq=$seq)")
            true
        } catch (e: Exception) {
            log("clipboard send failed: ${e.javaClass.simpleName}: ${e.message}")
            false
        }
    }

    private fun enqueuePacket(build: DataOutputStream.() -> Unit) {
        val buffer = ByteArrayOutputStream()
        DataOutputStream(buffer).use { it.build() }
        if (!outbound.offer(buffer.toByteArray())) {
            throw ProtocolException("outbound queue full")
        }
    }

    private fun startWriter(stream: PacketStream) {
        writerThread = Thread({
            try {
                while (running.get() || outbound.isNotEmpty()) {
                    val packet = outbound.poll(200, TimeUnit.MILLISECONDS) ?: continue
                    if (!running.get() && packet.isEmpty()) break
                    stream.writePacket(packet)
                }
            } catch (e: Exception) {
                if (running.get()) {
                    log("writer error: ${e.javaClass.simpleName}: ${e.message}")
                    running.set(false)
                    try {
                        socket?.close()
                    } catch (_: Exception) {
                    }
                }
            }
        }, "barrier-writer").also {
            it.isDaemon = true
            it.start()
        }
    }

    private fun nextClipboardSeq(): Int {
        synchronized(clipboardSeqLock) {
            clipboardSequence = maxOf(clipboardSequence, enterSequence) + 1
            return clipboardSequence
        }
    }

    fun sendFiles(paths: List<Pair<String, java.io.InputStream>>, sizes: List<Long>) {
        if (!connected || packets == null || paths.isEmpty()) return
        try {
            val prepared = paths.mapIndexed { index, (name, input) ->
                val declared = sizes.getOrElse(index) { -1L }
                if (declared >= 0) {
                    Triple(name, input, declared)
                } else {
                    val bytes = input.readBytes()
                    input.close()
                    Triple(name, java.io.ByteArrayInputStream(bytes), bytes.size.toLong())
                }
            }

            val namesPayload = buildString {
                prepared.forEach { (name, _, _) ->
                    append(name)
                    append('\u0000')
                }
            }
            enqueuePacket {
                writeRaw(Msg.DDRG)
                writeInt16Be(prepared.size)
                writeLengthString(namesPayload)
            }

            prepared.forEach { (name, input, size) ->
                log("sending file \"$name\" ($size bytes)")
                enqueuePacket {
                    writeRaw(Msg.DFTR)
                    writeUInt8(ChunkType.DATA_START)
                    writeLengthString(size.toString())
                }

                val buffer = ByteArray(CHUNK_SIZE)
                var sent = 0L
                input.use { src ->
                    while (true) {
                        val n = src.read(buffer)
                        if (n <= 0) break
                        val chunk = buffer.copyOf(n)
                        enqueuePacket {
                            writeRaw(Msg.DFTR)
                            writeUInt8(ChunkType.DATA_CHUNK)
                            writeLengthBytes(chunk)
                        }
                        sent += n
                    }
                }
                enqueuePacket {
                    writeRaw(Msg.DFTR)
                    writeUInt8(ChunkType.DATA_END)
                    writeLengthString("")
                }
                log("queued file \"$name\" ($sent bytes)")
            }
            listener.onFileProgress("send complete (${prepared.size} file(s))")
        } catch (e: Exception) {
            log("file send failed: ${e.javaClass.simpleName}: ${e.message}")
            listener.onFileProgress("send failed: ${e.message}")
        }
    }

    private fun handshake(stream: PacketStream, screenName: String) {
        val hello = stream.readPacket().asDataInput()
        val protocol = hello.readFixedAscii(7)
        if (protocol != "Barrier" && protocol != "Synergy") {
            throw ProtocolException("unexpected protocol name: $protocol")
        }
        val major = hello.readInt16Be()
        val minor = hello.readInt16Be()
        if (major != PROTOCOL_MAJOR) {
            throw ProtocolException("incompatible major version: $major.$minor")
        }
        val replyMinor = min(PROTOCOL_MINOR, minor)
        lastProtocol = protocol
        lastMajor = major
        lastMinor = replyMinor

        stream.writePacket {
            writeRaw(protocol)
            writeInt16Be(PROTOCOL_MAJOR)
            writeInt16Be(replyMinor)
            writeLengthString(screenName)
        }
    }

    private fun handlePacket(stream: PacketStream, packet: ByteArray): Boolean {
        if (packet.size < 4) {
            throw ProtocolException("short message")
        }
        val code = String(packet, 0, 4, StandardCharsets.US_ASCII)
        val body = packet.copyOfRange(4, packet.size)
        val input = body.asDataInput()

        when (code) {
            Msg.CALV -> enqueuePacket { writeRaw(Msg.CALV) }
            Msg.QINF -> sendInfo()
            Msg.CIAK, Msg.CNOP, Msg.CROP, Msg.COUT -> Unit
            Msg.CSEC -> if (body.isNotEmpty()) input.readUInt8()
            Msg.CINN -> {
                if (body.size >= 10) {
                    input.readInt16Be()
                    input.readInt16Be()
                    enterSequence = input.readInt()
                    input.readInt16Be()
                    synchronized(clipboardSeqLock) {
                        if (enterSequence > clipboardSequence) {
                            clipboardSequence = enterSequence
                        }
                    }
                }
            }
            Msg.CCLP -> {
                if (body.size >= 5) {
                    val id = input.readUInt8()
                    val seq = input.readInt()
                    log("recv grab clipboard id=$id seq=$seq")
                }
            }
            Msg.CLAK -> {
                val id = if (body.isNotEmpty()) body.asDataInput().readUInt8() else 0
                log("recv clipboard ack id=$id")
                listener.onClipboardAck(id)
            }
            Msg.DCLP -> handleClipboardChunk(input)
            Msg.DDRG -> handleDragInfo(input)
            Msg.DFTR -> handleFileChunk(input)
            Msg.DSOP, Msg.DFOF, Msg.LSYN, Msg.SECN,
            Msg.DKDN, Msg.DKDL, Msg.DKRP, Msg.DKUP,
            Msg.DMDN, Msg.DMUP, Msg.DMMV, Msg.DMRM, Msg.DMWM -> Unit
            Msg.CBYE -> {
                log("server closed connection")
                return false
            }
            Msg.EUNK -> throw ProtocolException("unknown screen name (add it in PC layout)")
            Msg.EBSY -> throw ProtocolException("screen name already connected")
            Msg.EICV -> throw ProtocolException("incompatible protocol version")
            Msg.EBAD -> throw ProtocolException("protocol error")
            else -> log("ignored message $code (${body.size} bytes)")
        }

        // Only reply CNOP for messages that historically needed a delayed-ACK
        // kick. Flooding CNOP after every DCLP/CCLP/CLAK can stall the Linux
        // server event loop and drop Windows peers during phone clipboard sync.
        val needsNoop =
            code != Msg.CALV &&
                code != Msg.CNOP &&
                code != Msg.DCLP &&
                code != Msg.CCLP &&
                code != Msg.CLAK &&
                code != Msg.DFTR &&
                code != Msg.DDRG
        if (needsNoop) {
            try {
                enqueuePacket { writeRaw(Msg.CNOP) }
            } catch (_: Exception) {
            }
        }
        return true
    }

    private fun sendInfo() {
        enqueuePacket {
            writeRaw(Msg.DINF)
            writeInt16Be(0)
            writeInt16Be(0)
            writeInt16Be(1080)
            writeInt16Be(1920)
            writeInt16Be(0)
            writeInt16Be(540)
            writeInt16Be(960)
        }
    }

    private fun enqueueClipboardChunks(data: ByteArray, seq: Int) {
        enqueuePacket {
            writeRaw(Msg.DCLP)
            writeUInt8(CLIPBOARD_ID)
            writeInt32Be(seq)
            writeUInt8(ChunkType.DATA_START)
            writeLengthString(data.size.toString())
        }
        var offset = 0
        while (offset < data.size) {
            val n = min(CHUNK_SIZE, data.size - offset)
            val chunk = data.copyOfRange(offset, offset + n)
            enqueuePacket {
                writeRaw(Msg.DCLP)
                writeUInt8(CLIPBOARD_ID)
                writeInt32Be(seq)
                writeUInt8(ChunkType.DATA_CHUNK)
                writeLengthBytes(chunk)
            }
            offset += n
        }
        enqueuePacket {
            writeRaw(Msg.DCLP)
            writeUInt8(CLIPBOARD_ID)
            writeInt32Be(seq)
            writeUInt8(ChunkType.DATA_END)
            writeLengthString("")
        }
    }

    private fun handleClipboardChunk(input: DataInputStream) {
        input.readUInt8()
        input.readInt()
        val mark = input.readUInt8()
        val len = input.readInt()
        val payload = ByteArray(len)
        input.readFully(payload)
        when (mark) {
            ChunkType.DATA_START -> {
                clipboardExpected = String(payload, StandardCharsets.UTF_8).toIntOrNull() ?: -1
                clipboardBytes.reset()
                log("clipboard data start, expected=$clipboardExpected")
            }
            ChunkType.DATA_CHUNK -> {
                clipboardBytes.write(payload)
                log("clipboard data chunk +${payload.size}")
            }
            ChunkType.DATA_END -> {
                val data = clipboardBytes.toByteArray()
                if (clipboardExpected >= 0 && data.size != clipboardExpected) {
                    log("clipboard size mismatch: expected=$clipboardExpected actual=${data.size}")
                } else {
                    val text = ClipboardCodec.unmarshallText(data)
                    if (text != null) {
                        suppressClipboardEcho = true
                        try {
                            listener.onClipboardText(text)
                            log("received clipboard text (${text.length} chars)")
                        } finally {
                            // Cleared by UI after applying; keep brief suppress here too.
                            suppressClipboardEcho = false
                        }
                    } else {
                        log("clipboard end but no text format (raw=${data.size} bytes)")
                    }
                }
                clipboardBytes.reset()
                clipboardExpected = -1
            }
            else -> log("clipboard unknown mark=$mark")
        }
    }

    private fun handleDragInfo(input: DataInputStream) {
        val count = input.readInt16Be()
        val info = input.readLengthString()
        receiveNames = info.split('\u0000').map { it.trim() }.filter { it.isNotEmpty() }
        receiveIndex = 0
        closeReceiveFile()
        log("incoming files: ${receiveNames.joinToString()} (count=$count)")
    }

    private fun handleFileChunk(input: DataInputStream) {
        val mark = input.readUInt8()
        val len = input.readInt()
        val payload = ByteArray(len)
        input.readFully(payload)
        when (mark) {
            ChunkType.FULL_SPEED_REQ -> {
                enqueuePacket {
                    writeRaw(Msg.DFTR)
                    writeUInt8(ChunkType.FULL_SPEED_ACK)
                    writeLengthString("")
                }
                log("acked full-speed file transfer")
            }
            ChunkType.DATA_START -> {
                receiveExpected = String(payload, StandardCharsets.UTF_8).toLongOrNull() ?: 0L
                receiveWritten = 0L
                if (receiveIndex >= receiveNames.size) {
                    log("unexpected file start")
                    return
                }
                val name = receiveNames[receiveIndex]
                val opened = fileStore.open(name)
                receivePath = opened.first
                receiveStream = opened.second
                log("receiving \"$name\" ($receiveExpected bytes)")
            }
            ChunkType.DATA_CHUNK -> {
                receiveStream?.write(payload)
                receiveWritten += payload.size
            }
            ChunkType.DATA_END -> {
                val name = receiveNames.getOrNull(receiveIndex) ?: "file"
                val path = receivePath
                closeReceiveFile()
                receiveIndex++
                if (path != null) {
                    listener.onFileReceived(name, path)
                    log("saved \"$name\" -> $path")
                }
                if (receiveIndex >= receiveNames.size) {
                    listener.onFileProgress("receive complete (${receiveNames.size} file(s))")
                    receiveNames = emptyList()
                    receiveIndex = 0
                }
            }
        }
    }

    private fun closeReceiveFile() {
        try {
            receiveStream?.close()
        } catch (_: Exception) {
        }
        receiveStream = null
    }

    private fun cleanup() {
        connected = false
        running.set(false)
        closeReceiveFile()
        try {
            socket?.close()
        } catch (_: Exception) {
        }
        try {
            writerThread?.join(1_000)
        } catch (_: Exception) {
        }
        writerThread = null
        outbound.clear()
        socket = null
        packets = null
    }

    private fun log(message: String) {
        Log.i(TAG, message)
        listener.onLog(message)
    }

    companion object {
        private const val TAG = "BarrierClient"
        private const val PROTOCOL_MAJOR = 1
        private const val PROTOCOL_MINOR = 8
        private const val CONNECT_TIMEOUT_MS = 10_000
        private const val CLIPBOARD_ID = 0
        // Keep chunks modest so one TLS write cannot starve keepalive on the PC.
        private const val CHUNK_SIZE = 16 * 1024
    }
}

private class ByteArrayOutputGrowable {
    private var buffer = ByteArray(0)
    private var size = 0

    fun write(bytes: ByteArray) {
        ensure(size + bytes.size)
        System.arraycopy(bytes, 0, buffer, size, bytes.size)
        size += bytes.size
    }

    fun toByteArray(): ByteArray = buffer.copyOf(size)

    fun reset() {
        size = 0
    }

    private fun ensure(needed: Int) {
        if (needed <= buffer.size) return
        var cap = if (buffer.isEmpty()) 4096 else buffer.size * 2
        while (cap < needed) cap *= 2
        buffer = buffer.copyOf(cap)
    }
}
