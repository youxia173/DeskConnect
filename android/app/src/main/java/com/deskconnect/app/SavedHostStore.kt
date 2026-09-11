package com.deskconnect.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import org.json.JSONArray
import org.json.JSONObject

data class SavedHost(
    val host: String,
    val port: Int = 24800,
    val screen: String = "Phone",
    val useTls: Boolean = true,
    /** DeskConnect computer name of the PC (server), when known. */
    val peerName: String = "",
    val lastUsed: Long = System.currentTimeMillis(),
) {
    fun key(): String = "${host.trim()}|$port"

    fun label(): String {
        val endpoint = if (port == 24800) host else "$host:$port"
        return if (peerName.isNotBlank()) "$endpoint · $peerName" else endpoint
    }
}

/**
 * Remembers PC endpoints per Wi‑Fi network (SSID). Falls back to a shared bucket
 * when SSID is unavailable (no location / nearby-wifi permission).
 */
class SavedHostStore(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun listForWifi(wifiKey: String): List<SavedHost> {
        val raw = prefs.getString(keyFor(wifiKey), null) ?: return emptyList()
        return parseList(raw).sortedByDescending { it.lastUsed }
    }

    fun remember(wifiKey: String, host: SavedHost) {
        val list = listForWifi(wifiKey).toMutableList()
        val existing = list.firstOrNull { it.key() == host.key() }
        list.removeAll { it.key() == host.key() }
        val merged = host.copy(
            peerName = host.peerName.ifBlank { existing?.peerName.orEmpty() },
            screen = host.screen.ifBlank { existing?.screen.orEmpty() },
            lastUsed = System.currentTimeMillis(),
        )
        list.add(0, merged)
        while (list.size > MAX_PER_WIFI) {
            list.removeAt(list.lastIndex)
        }
        prefs.edit().putString(keyFor(wifiKey), toJson(list)).apply()
    }

    fun remove(wifiKey: String, host: SavedHost) {
        val list = listForWifi(wifiKey).filterNot { it.key() == host.key() }
        if (list.isEmpty()) {
            prefs.edit().remove(keyFor(wifiKey)).apply()
        } else {
            prefs.edit().putString(keyFor(wifiKey), toJson(list)).apply()
        }
    }

    fun removeByKey(wifiKey: String, hostKey: String) {
        val list = listForWifi(wifiKey).filterNot { it.key() == hostKey }
        if (list.isEmpty()) {
            prefs.edit().remove(keyFor(wifiKey)).apply()
        } else {
            prefs.edit().putString(keyFor(wifiKey), toJson(list)).apply()
        }
    }

    private fun keyFor(wifiKey: String) = "hosts_$wifiKey"

    private fun parseList(raw: String): List<SavedHost> {
        return try {
            val arr = JSONArray(raw)
            buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    val host = o.optString("host").trim()
                    if (host.isEmpty()) continue
                    add(
                        SavedHost(
                            host = host,
                            port = o.optInt("port", 24800),
                            screen = o.optString("screen", "Phone").ifBlank { "Phone" },
                            useTls = o.optBoolean("tls", true),
                            peerName = o.optString("peerName", ""),
                            lastUsed = o.optLong("lastUsed", 0L),
                        )
                    )
                }
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun toJson(list: List<SavedHost>): String {
        val arr = JSONArray()
        list.forEach { h ->
            arr.put(
                JSONObject()
                    .put("host", h.host)
                    .put("port", h.port)
                    .put("screen", h.screen)
                    .put("tls", h.useTls)
                    .put("peerName", h.peerName)
                    .put("lastUsed", h.lastUsed)
            )
        }
        return arr.toString()
    }

    companion object {
        private const val PREFS = "deskconnect_hosts"
        private const val MAX_PER_WIFI = 12
        const val WIFI_UNKNOWN = "_unknown_"
    }
}

object WifiNetworkHelper {
    fun currentWifiKey(context: Context): String {
        if (!isWifiConnected(context)) {
            return SavedHostStore.WIFI_UNKNOWN
        }
        val ssid = readSsid(context)?.trim().orEmpty()
        if (ssid.isEmpty() || ssid == "<unknown ssid>" || ssid.equals("unknown ssid", true)) {
            return SavedHostStore.WIFI_UNKNOWN
        }
        // Strip quotes WifiManager often adds.
        val clean = ssid.removePrefix("\"").removeSuffix("\"")
        return if (clean.isBlank()) SavedHostStore.WIFI_UNKNOWN else clean
    }

    fun displayName(context: Context): String {
        val key = currentWifiKey(context)
        return if (key == SavedHostStore.WIFI_UNKNOWN) {
            context.getString(R.string.wifi_unknown)
        } else {
            key
        }
    }

    private fun isWifiConnected(context: Context): Boolean {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val network = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(network) ?: return false
        return caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
    }

    @Suppress("DEPRECATION")
    private fun readSsid(context: Context): String? {
        return try {
            val wifi = context.applicationContext.getSystemService(WifiManager::class.java) ?: return null
            val info = wifi.connectionInfo ?: return null
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                // May be unknown without location / nearby-wifi permission.
                info.ssid
            } else {
                info.ssid
            }
        } catch (_: Exception) {
            null
        }
    }
}
