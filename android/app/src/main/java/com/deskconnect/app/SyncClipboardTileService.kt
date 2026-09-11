package com.deskconnect.app

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.service.quicksettings.TileService
import com.deskconnect.app.ui.SyncClipboardActivity

/**
 * Quick settings tile that pushes the current clipboard to the PC.
 *
 * Android 10+ forbids background clipboard reads, so the tile opens the
 * invisible [SyncClipboardActivity] to take focus for the read — a tile tap is
 * a user action, so this activity start is always allowed.
 */
class SyncClipboardTileService : TileService() {
    override fun onClick() {
        super.onClick()
        val intent = Intent(this, SyncClipboardActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
            .addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            val pending = PendingIntent.getActivity(
                this,
                0,
                intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            startActivityAndCollapse(pending)
        } else {
            @Suppress("DEPRECATION")
            startActivityAndCollapse(intent)
        }
    }
}
