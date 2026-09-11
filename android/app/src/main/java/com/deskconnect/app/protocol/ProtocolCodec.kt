package com.deskconnect.app.protocol

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.EOFException
import java.io.InputStream
import java.io.OutputStream
import java.nio.charset.StandardCharsets

/**
 * Deskflow/Barrier wire helpers.
 *
 * All regular messages are framed by PacketStreamFilter: 4-byte big-endian
 * length followed by the payload written by ProtocolUtil::writef.
 */
class PacketStream(private val input: InputStream, private val output: OutputStream) {
    private val outLock = Any()

    fun readPacket(): ByteArray {
        val len = readInt32()
        if (len < 0 || len > MAX_MESSAGE_LENGTH) {
            throw ProtocolException("invalid packet length: $len")
        }
        return readFully(len)
    }

    fun writePacket(payload: ByteArray) {
        synchronized(outLock) {
            writeInt32(payload.size)
            output.write(payload)
            output.flush()
        }
    }

    fun writePacket(build: DataOutputStream.() -> Unit) {
        val buffer = ByteArrayOutputStream()
        DataOutputStream(buffer).use { it.build() }
        writePacket(buffer.toByteArray())
    }

    private fun readFully(count: Int): ByteArray {
        val buffer = ByteArray(count)
        var offset = 0
        while (offset < count) {
            val n = input.read(buffer, offset, count - offset)
            if (n < 0) {
                throw EOFException("unexpected end of stream, need ${count - offset} more bytes")
            }
            offset += n
        }
        return buffer
    }

    private fun readInt32(): Int {
        val b = readFully(4)
        return ((b[0].toInt() and 0xff) shl 24) or
            ((b[1].toInt() and 0xff) shl 16) or
            ((b[2].toInt() and 0xff) shl 8) or
            (b[3].toInt() and 0xff)
    }

    private fun writeInt32(value: Int) {
        output.write((value ushr 24) and 0xff)
        output.write((value ushr 16) and 0xff)
        output.write((value ushr 8) and 0xff)
        output.write(value and 0xff)
    }

    companion object {
        const val MAX_MESSAGE_LENGTH = 4 * 1024 * 1024
    }
}

class ProtocolException(message: String) : Exception(message)

fun DataOutputStream.writeInt16Be(value: Int) {
    writeByte((value ushr 8) and 0xff)
    writeByte(value and 0xff)
}

fun DataOutputStream.writeInt32Be(value: Int) {
    writeInt(value)
}

fun DataOutputStream.writeUInt8(value: Int) {
    writeByte(value and 0xff)
}

fun DataOutputStream.writeLengthString(text: String) {
    writeLengthBytes(text.toByteArray(StandardCharsets.UTF_8))
}

fun DataOutputStream.writeLengthBytes(bytes: ByteArray) {
    writeInt32Be(bytes.size)
    write(bytes)
}

fun DataOutputStream.writeRaw(text: String) {
    write(text.toByteArray(StandardCharsets.US_ASCII))
}

fun DataInputStream.readInt16Be(): Int {
    val v = ((readUnsignedByte() shl 8) or readUnsignedByte())
    return if (v and 0x8000 != 0) v or -0x10000 else v
}

fun DataInputStream.readUInt8(): Int = readUnsignedByte()

fun DataInputStream.readLengthString(): String {
    val len = readInt()
    if (len < 0 || len > PacketStream.MAX_MESSAGE_LENGTH) {
        throw ProtocolException("invalid string length: $len")
    }
    val bytes = ByteArray(len)
    readFully(bytes)
    return String(bytes, StandardCharsets.UTF_8)
}

fun DataInputStream.readFixedAscii(len: Int): String {
    val bytes = ByteArray(len)
    readFully(bytes)
    return String(bytes, StandardCharsets.US_ASCII)
}

fun ByteArray.asDataInput(): DataInputStream = DataInputStream(ByteArrayInputStream(this))

object ClipboardCodec {
    const val FORMAT_TEXT = 0

    fun marshallText(text: String): ByteArray {
        val utf8 = text.replace("\r\n", "\n").replace('\r', '\n')
            .toByteArray(StandardCharsets.UTF_8)
        val out = ByteArrayOutputStream()
        DataOutputStream(out).use { dos ->
            dos.writeInt32Be(1)
            dos.writeInt32Be(FORMAT_TEXT)
            dos.writeInt32Be(utf8.size)
            dos.write(utf8)
        }
        return out.toByteArray()
    }

    fun unmarshallText(data: ByteArray): String? {
        if (data.size < 4) return null
        val input = data.asDataInput()
        val numFormats = input.readInt()
        repeat(numFormats) {
            if (input.available() < 8) return null
            val format = input.readInt()
            val size = input.readInt()
            if (size < 0 || size > input.available()) return null
            val payload = ByteArray(size)
            input.readFully(payload)
            if (format == FORMAT_TEXT) {
                return String(payload, StandardCharsets.UTF_8)
            }
        }
        return null
    }
}

object ChunkType {
    const val DATA_START = 1
    const val DATA_CHUNK = 2
    const val DATA_END = 3
    const val FULL_SPEED_REQ = 4
    const val FULL_SPEED_ACK = 5
}

object Msg {
    const val CALV = "CALV"
    const val CBYE = "CBYE"
    const val CCLP = "CCLP"
    const val CLAK = "CLAK"
    const val CIAK = "CIAK"
    const val CINN = "CINN"
    const val CNOP = "CNOP"
    const val COUT = "COUT"
    const val CROP = "CROP"
    const val CSEC = "CSEC"
    const val DCLP = "DCLP"
    const val DDRG = "DDRG"
    const val DFTR = "DFTR"
    const val DINF = "DINF"
    const val DKDN = "DKDN"
    const val DKDL = "DKDL"
    const val DKRP = "DKRP"
    const val DKUP = "DKUP"
    const val DMDN = "DMDN"
    const val DMMV = "DMMV"
    const val DMRM = "DMRM"
    const val DMUP = "DMUP"
    const val DMWM = "DMWM"
    const val DSOP = "DSOP"
    const val DFOF = "DFOF"
    const val LSYN = "LSYN"
    const val QINF = "QINF"
    const val SECN = "SECN"
    const val EICV = "EICV"
    const val EBSY = "EBSY"
    const val EUNK = "EUNK"
    const val EBAD = "EBAD"
}
