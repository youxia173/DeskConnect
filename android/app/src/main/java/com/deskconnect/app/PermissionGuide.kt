package com.deskconnect.app

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat

enum class PermissionKind {
    Notifications,
    WifiSsid,
    LocationServices,
    Overlay,
}

data class PermissionItem(
    val kind: PermissionKind,
    val titleRes: Int,
    val reasonRes: Int,
    val granted: Boolean,
)

object PermissionGuide {
    fun items(context: Context): List<PermissionItem> {
        val list = mutableListOf(
            PermissionItem(
                kind = PermissionKind.Notifications,
                titleRes = R.string.perm_notifications_title,
                reasonRes = R.string.perm_notifications_reason,
                granted = hasNotifications(context),
            ),
            PermissionItem(
                kind = PermissionKind.WifiSsid,
                titleRes = R.string.perm_wifi_title,
                reasonRes = R.string.perm_wifi_reason,
                granted = hasWifiSsidPermission(context),
            ),
        )
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            list += PermissionItem(
                kind = PermissionKind.LocationServices,
                titleRes = R.string.perm_location_services_title,
                reasonRes = R.string.perm_location_services_reason,
                granted = isLocationEnabled(context),
            )
        }
        list += PermissionItem(
            kind = PermissionKind.Overlay,
            titleRes = R.string.perm_overlay_title,
            reasonRes = R.string.perm_overlay_reason,
            granted = ConnectionService.canDrawOverlays(context),
        )
        return list
    }

    fun missing(context: Context): List<PermissionItem> = items(context).filterNot { it.granted }

    fun hasNotifications(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return NotificationManagerCompat.from(context).areNotificationsEnabled()
        }
        return ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED &&
            NotificationManagerCompat.from(context).areNotificationsEnabled()
    }

    fun hasWifiSsidPermission(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(context, Manifest.permission.NEARBY_WIFI_DEVICES) ==
                PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED
        }
    }

    fun wifiRuntimePermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    fun notificationRuntimePermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.POST_NOTIFICATIONS)
        } else {
            emptyArray()
        }
    }

    fun isLocationEnabled(context: Context): Boolean {
        val lm = context.getSystemService(LocationManager::class.java) ?: return true
        return try {
            lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER) ||
                lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
        } catch (_: Exception) {
            true
        }
    }

    fun openAppDetails(context: Context) {
        val intent = Intent(
            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.parse("package:${context.packageName}")
        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }

    fun openOverlaySettings(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        try {
            context.startActivity(
                Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:${context.packageName}")
                ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        } catch (_: Exception) {
            openAppDetails(context)
        }
    }

    fun openNotificationSettings(context: Context) {
        try {
            val intent = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(
                    Settings.EXTRA_APP_PACKAGE,
                    context.packageName
                )
            } else {
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                    .setData(Uri.parse("package:${context.packageName}"))
            }
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(intent)
        } catch (_: Exception) {
            openAppDetails(context)
        }
    }

    fun openLocationServicesSettings(context: Context) {
        try {
            context.startActivity(
                Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        } catch (_: Exception) {
            openAppDetails(context)
        }
    }

    /** True when system dialog cannot be shown again (user chose Don't ask again). */
    fun shouldOpenSettingsForRuntime(activity: Activity, permission: String): Boolean {
        val granted = ContextCompat.checkSelfPermission(activity, permission) ==
            PackageManager.PERMISSION_GRANTED
        if (granted) return false
        return !activity.shouldShowRequestPermissionRationale(permission) &&
            activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getBoolean(askedKey(permission), false)
    }

    fun markRuntimeAsked(context: Context, permissions: Array<String>) {
        val ed = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
        permissions.forEach { ed.putBoolean(askedKey(it), true) }
        ed.apply()
    }

    private fun askedKey(permission: String) = "asked_$permission"

    private const val PREFS = "deskconnect_perms"
}
