package com.deskconnect.app

import android.content.Context
import android.os.Build
import android.provider.Settings

/**
 * Resolves a Barrier/DeskConnect-safe screen name from the phone's device name.
 */
object DeviceNameHelper {
    fun screenName(context: Context): String {
        val raw = listOfNotNull(
            deviceName(context),
            Build.MODEL,
            "Phone",
        ).firstOrNull { it.isNotBlank() } ?: "Phone"
        return clean(raw)
    }

    private fun deviceName(context: Context): String? {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N_MR1) {
                Settings.Global.getString(context.contentResolver, Settings.Global.DEVICE_NAME)
                    ?.trim()
                    ?.takeIf { it.isNotEmpty() }
            } else {
                @Suppress("DEPRECATION")
                Settings.Secure.getString(context.contentResolver, "bluetooth_name")
                    ?.trim()
                    ?.takeIf { it.isNotEmpty() }
            }
        } catch (_: Exception) {
            null
        }
    }

    /** Match Settings::cleanComputerName rules used by DeskConnect PC. */
    fun clean(name: String): String {
        var cleanName = name.trim().replace(Regex("\\s+"), "_")
        cleanName = cleanName.replace(Regex("[^\\w\\-.]"), "")
        while (cleanName.startsWith("-") || cleanName.startsWith("_") || cleanName.startsWith(".")) {
            cleanName = cleanName.drop(1)
        }
        while (cleanName.endsWith("-") || cleanName.endsWith("_") || cleanName.endsWith(".")) {
            cleanName = cleanName.dropLast(1)
        }
        if (cleanName.length > 255) {
            cleanName = clean(cleanName.take(255))
        }
        return cleanName.ifBlank { "Phone" }
    }
}
