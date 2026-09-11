package com.deskconnect.app

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream

/**
 * Where PC→phone file transfers are saved.
 *
 * Default: shared Downloads/DeskConnect (visible in the system Downloads app).
 * Optional: user-picked folder via SAF tree URI (persisted).
 */
class ReceiveFolderStore(context: Context) {
    private val app = context.applicationContext
    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun displayPath(): String {
        val tree = treeUri()
        if (tree != null) {
            val label = prefs.getString(KEY_TREE_LABEL, null)?.takeIf { it.isNotBlank() }
            return label ?: tree.toString()
        }
        return defaultDisplayPath()
    }

    fun isCustom(): Boolean = treeUri() != null

    fun setTreeUri(uri: Uri, label: String?) {
        try {
            app.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
        } catch (_: SecurityException) {
            // Some providers only allow one flag; still store URI and try on write.
        }
        prefs.edit()
            .putString(KEY_TREE_URI, uri.toString())
            .putString(KEY_TREE_LABEL, label)
            .apply()
    }

    fun clearCustom() {
        treeUri()?.let { uri ->
            try {
                app.contentResolver.releasePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
                )
            } catch (_: SecurityException) {
            }
        }
        prefs.edit()
            .remove(KEY_TREE_URI)
            .remove(KEY_TREE_LABEL)
            .apply()
    }

    fun open(fileName: String): Pair<String, OutputStream> {
        val safe = sanitizeName(fileName)
        val tree = treeUri()
        return if (tree != null) {
            openInTree(tree, safe)
        } else {
            openInPublicDownloads(safe)
        }
    }

    private fun treeUri(): Uri? =
        prefs.getString(KEY_TREE_URI, null)?.let { Uri.parse(it) }

    private fun openInTree(tree: Uri, safeName: String): Pair<String, OutputStream> {
        val root = DocumentFile.fromTreeUri(app, tree)
            ?: throw IllegalStateException("无法打开所选文件夹，请重新选择保存位置")
        val unique = uniqueDocumentName(root, safeName)
        val mime = guessMime(unique)
        val created = root.createFile(mime, stripExtension(unique))
            ?: root.createFile(mime, unique)
            ?: throw IllegalStateException("无法在所选文件夹创建文件")
        val out = app.contentResolver.openOutputStream(created.uri)
            ?: throw IllegalStateException("无法写入所选文件夹")
        val label = prefs.getString(KEY_TREE_LABEL, null)?.takeIf { it.isNotBlank() }
        val shown = if (label != null) "$label/$unique" else unique
        return shown to out
    }

    private fun openInPublicDownloads(safeName: String): Pair<String, OutputStream> {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val unique = uniqueMediaStoreName(safeName)
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, unique)
                put(MediaStore.Downloads.MIME_TYPE, guessMime(unique))
                put(
                    MediaStore.Downloads.RELATIVE_PATH,
                    Environment.DIRECTORY_DOWNLOADS + "/" + SUBDIR,
                )
                put(MediaStore.Downloads.IS_PENDING, 1)
            }
            val uri = app.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: throw IllegalStateException("无法写入系统下载目录")
            val out = app.contentResolver.openOutputStream(uri)
                ?: run {
                    app.contentResolver.delete(uri, null, null)
                    throw IllegalStateException("无法打开下载目录写入流")
                }
            // Clear pending when the stream is closed so Downloads app lists the file.
            val wrapped = object : OutputStream() {
                override fun write(b: Int) = out.write(b)
                override fun write(b: ByteArray) = out.write(b)
                override fun write(b: ByteArray, off: Int, len: Int) = out.write(b, off, len)
                override fun flush() = out.flush()
                override fun close() {
                    try {
                        out.close()
                    } finally {
                        val done = ContentValues().apply {
                            put(MediaStore.Downloads.IS_PENDING, 0)
                        }
                        try {
                            app.contentResolver.update(uri, done, null, null)
                        } catch (_: Exception) {
                        }
                    }
                }
            }
            return "${defaultDisplayPath()}/$unique" to wrapped
        }

        val dir = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            SUBDIR,
        )
        if (!dir.exists() && !dir.mkdirs()) {
            throw IllegalStateException("无法创建下载目录: ${dir.absolutePath}")
        }
        val target = uniqueFile(dir, safeName)
        return target.absolutePath to FileOutputStream(target)
    }

    private fun uniqueDocumentName(dir: DocumentFile, name: String): String {
        if (dir.findFile(name) == null) return name
        val dot = name.lastIndexOf('.')
        val base = if (dot > 0) name.substring(0, dot) else name
        val ext = if (dot > 0) name.substring(dot) else ""
        var i = 1
        while (true) {
            val candidate = "$base ($i)$ext"
            if (dir.findFile(candidate) == null) return candidate
            i++
        }
    }

    private fun uniqueMediaStoreName(name: String): String {
        // MediaStore may auto-dedupe; still prefer a readable name when possible.
        return name
    }

    private fun uniqueFile(dir: File, name: String): File {
        var target = File(dir, name)
        if (!target.exists()) return target
        val dot = name.lastIndexOf('.')
        val base = if (dot > 0) name.substring(0, dot) else name
        val ext = if (dot > 0) name.substring(dot) else ""
        var i = 1
        while (true) {
            target = File(dir, "$base ($i)$ext")
            if (!target.exists()) return target
            i++
        }
    }

    companion object {
        private const val PREFS = "deskconnect"
        private const val KEY_TREE_URI = "receive_tree_uri"
        private const val KEY_TREE_LABEL = "receive_tree_label"
        const val SUBDIR = "DeskConnect"

        fun defaultDisplayPath(): String = "下载/$SUBDIR"

        fun sanitizeName(fileName: String): String {
            val trimmed = fileName.trim().ifEmpty { "file" }
            return trimmed.replace(Regex("[\\\\/:*?\"<>|]"), "_")
        }

        fun guessMime(name: String): String {
            val ext = name.substringAfterLast('.', "").lowercase()
            if (ext.isEmpty()) return "application/octet-stream"
            return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext)
                ?: "application/octet-stream"
        }

        fun stripExtension(name: String): String {
            val dot = name.lastIndexOf('.')
            return if (dot > 0) name.substring(0, dot) else name
        }
    }
}
