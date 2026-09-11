package com.deskconnect.app.protocol

import org.bouncycastle.asn1.x500.X500Name
import org.bouncycastle.asn1.x509.BasicConstraints
import org.bouncycastle.asn1.x509.Extension
import org.bouncycastle.asn1.x509.KeyUsage
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder
import java.io.ByteArrayInputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.math.BigInteger
import java.net.InetSocketAddress
import java.net.Socket
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.util.Date
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

class UntrustedFingerprintException(val fingerprintHex: String) :
    CertificateException("untrusted server fingerprint: $fingerprintHex")

object FingerprintUtil {
    fun sha256Hex(certDer: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(certDer)
        return digest.joinToString("") { "%02X".format(it) }
    }

    fun formatColon(hex: String): String {
        val clean = normalize(hex)
        return clean.chunked(2).joinToString(":")
    }

    fun normalize(hex: String): String =
        hex.replace(":", "").replace(" ", "").uppercase()
}

/**
 * TLS helpers matching DeskConnect/Deskflow:
 * TCP connect, then TLS 1.2+, then Barrier hello.
 * Trust is SHA-256 certificate fingerprint pinning (not system CAs).
 * Client presents a self-signed identity so PeerAuth servers accept the handshake.
 */
object TlsSupport {
    private const val KEY_PASSWORD = "deskconnect"

    fun ensureClientIdentity(dir: File): ClientIdentity {
        if (!dir.exists()) dir.mkdirs()
        val ksFile = File(dir, "client-identity.p12")
        val metaFile = File(dir, "client-fingerprint.txt")
        if (ksFile.exists() && metaFile.exists() && ksFile.length() > 0L) {
            val fp = FingerprintUtil.normalize(metaFile.readText().trim())
            if (fp.length == 64) {
                return ClientIdentity(ksFile, fp)
            }
        }

        // Android ships a truncated "BC" provider that cannot sign SHA256WithRSA.
        // Use platform crypto for the key/signature; BouncyCastle only for X.509 structure.
        val keyPair = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
        val now = System.currentTimeMillis()
        val notBefore = Date(now - 24L * 60 * 60 * 1000)
        val notAfter = Date(now + 10L * 365 * 24 * 60 * 60 * 1000)
        val subject = X500Name("CN=DeskConnect Android")
        val serial = BigInteger(64, SecureRandom())
        val builder = JcaX509v3CertificateBuilder(
            subject, serial, notBefore, notAfter, subject, keyPair.public
        )
        builder.addExtension(Extension.basicConstraints, true, BasicConstraints(false))
        builder.addExtension(
            Extension.keyUsage, true,
            KeyUsage(KeyUsage.digitalSignature or KeyUsage.keyEncipherment)
        )
        // Do NOT setProvider("BC") - that hits Android's incomplete BC provider.
        val signer = JcaContentSignerBuilder("SHA256WithRSA").build(keyPair.private)
        val holder = builder.build(signer)
        val cert = CertificateFactory.getInstance("X.509")
            .generateCertificate(ByteArrayInputStream(holder.encoded)) as X509Certificate

        val keyStore = KeyStore.getInstance("PKCS12")
        keyStore.load(null, null)
        keyStore.setKeyEntry(
            "client",
            keyPair.private,
            KEY_PASSWORD.toCharArray(),
            arrayOf(cert)
        )
        FileOutputStream(ksFile).use { keyStore.store(it, KEY_PASSWORD.toCharArray()) }

        val fp = FingerprintUtil.sha256Hex(cert.encoded)
        metaFile.writeText(fp)
        return ClientIdentity(ksFile, fp)
    }

    fun connect(
        host: String,
        port: Int,
        connectTimeoutMs: Int,
        trustedServerFingerprints: Set<String>,
        identity: ClientIdentity,
        onServerFingerprint: (String) -> Unit,
    ): SSLSocket {
        val trustManager = FingerprintTrustManager(trustedServerFingerprints, onServerFingerprint)
        val keyManagers = loadKeyManagers(identity.pkcs12File)

        val context = SSLContext.getInstance("TLS")
        context.init(keyManagers, arrayOf<TrustManager>(trustManager), SecureRandom())

        val plain = Socket()
        plain.tcpNoDelay = true
        plain.connect(InetSocketAddress(host, port), connectTimeoutMs)

        val socket = context.socketFactory.createSocket(plain, host, port, true) as SSLSocket
        socket.tcpNoDelay = true
        // DeskConnect disables TLS < 1.2
        val enabled = socket.supportedProtocols.filter {
            it == "TLSv1.2" || it == "TLSv1.3"
        }.toTypedArray()
        if (enabled.isNotEmpty()) {
            socket.enabledProtocols = enabled
        }
        try {
            socket.startHandshake()
        } catch (e: Exception) {
            try {
                socket.close()
            } catch (_: Exception) {
            }
            val cause = findUntrusted(e)
            if (cause != null) throw cause
            throw e
        }
        return socket
    }

    private fun loadKeyManagers(pkcs12: File): Array<javax.net.ssl.KeyManager> {
        val keyStore = KeyStore.getInstance("PKCS12")
        FileInputStream(pkcs12).use { keyStore.load(it, KEY_PASSWORD.toCharArray()) }
        val kmf = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm())
        kmf.init(keyStore, KEY_PASSWORD.toCharArray())
        return kmf.keyManagers
    }

    private fun findUntrusted(error: Throwable?): UntrustedFingerprintException? {
        var cur = error
        while (cur != null) {
            if (cur is UntrustedFingerprintException) return cur
            cur = cur.cause
        }
        return null
    }
}

data class ClientIdentity(
    val pkcs12File: File,
    val fingerprintHex: String,
)

private class FingerprintTrustManager(
    trusted: Set<String>,
    private val onServerFingerprint: (String) -> Unit,
) : X509TrustManager {
    private val trustedNorm = trusted.map { FingerprintUtil.normalize(it) }.toSet()

    override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {
        // Client role only.
    }

    override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {
        if (chain.isNullOrEmpty()) {
            throw CertificateException("empty server certificate chain")
        }
        val fp = FingerprintUtil.sha256Hex(chain[0].encoded)
        onServerFingerprint(fp)
        if (!trustedNorm.contains(fp)) {
            throw UntrustedFingerprintException(fp)
        }
    }

    override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
}
