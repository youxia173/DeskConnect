package com.deskconnect.app.input

import android.view.KeyEvent

/** Deskflow KeyID/KeyModifierMask values are defined in src/lib/deskflow/KeyTypes.h. */
object DeskflowKeyMapper {
    fun keyCode(keyId: Int): Int? = when (keyId) {
        in 'a'.code..'z'.code -> KeyEvent.KEYCODE_A + keyId - 'a'.code
        in 'A'.code..'Z'.code -> KeyEvent.KEYCODE_A + keyId - 'A'.code
        in '0'.code..'9'.code -> KeyEvent.KEYCODE_0 + keyId - '0'.code
        in 0xEFB0..0xEFB9 -> KeyEvent.KEYCODE_NUMPAD_0 + keyId - 0xEFB0
        in 0xEFBE..0xEFC9 -> KeyEvent.KEYCODE_F1 + keyId - 0xEFBE
        in 0xEF91..0xEF94 -> KeyEvent.KEYCODE_F1 + keyId - 0xEF91
        ' '.code, 0xEF80 -> KeyEvent.KEYCODE_SPACE
        ','.code, '<'.code -> KeyEvent.KEYCODE_COMMA
        '.'.code, '>'.code -> KeyEvent.KEYCODE_PERIOD
        '/'.code, '?'.code -> KeyEvent.KEYCODE_SLASH
        ';'.code, ':'.code -> KeyEvent.KEYCODE_SEMICOLON
        '\''.code, '"'.code -> KeyEvent.KEYCODE_APOSTROPHE
        '['.code, '{'.code -> KeyEvent.KEYCODE_LEFT_BRACKET
        ']'.code, '}'.code -> KeyEvent.KEYCODE_RIGHT_BRACKET
        '\\'.code, '|'.code -> KeyEvent.KEYCODE_BACKSLASH
        '-'.code, '_'.code -> KeyEvent.KEYCODE_MINUS
        '='.code, '+'.code -> KeyEvent.KEYCODE_EQUALS
        '`'.code, '~'.code -> KeyEvent.KEYCODE_GRAVE
        '!'.code -> KeyEvent.KEYCODE_1
        '@'.code -> KeyEvent.KEYCODE_2
        '#'.code -> KeyEvent.KEYCODE_3
        '$'.code -> KeyEvent.KEYCODE_4
        '%'.code -> KeyEvent.KEYCODE_5
        '^'.code -> KeyEvent.KEYCODE_6
        '&'.code -> KeyEvent.KEYCODE_7
        '*'.code -> KeyEvent.KEYCODE_8
        '('.code -> KeyEvent.KEYCODE_9
        ')'.code -> KeyEvent.KEYCODE_0
        0xEF08 -> KeyEvent.KEYCODE_DEL
        0xEF09, 0xEF89, 0xEE20 -> KeyEvent.KEYCODE_TAB
        0xEF0A, 0xEF0D, 0xEF62 -> KeyEvent.KEYCODE_ENTER
        0xEF0B -> KeyEvent.KEYCODE_CLEAR
        0xEF13, 0xEF6B -> KeyEvent.KEYCODE_BREAK
        0xEF14 -> KeyEvent.KEYCODE_SCROLL_LOCK
        0xEF15, 0xEF61 -> KeyEvent.KEYCODE_SYSRQ
        0xEF1B, 0xEF69 -> KeyEvent.KEYCODE_ESCAPE
        0xEF23 -> KeyEvent.KEYCODE_HENKAN
        0xEF26 -> KeyEvent.KEYCODE_KANA
        0xEF27 -> KeyEvent.KEYCODE_KATAKANA_HIRAGANA
        0xEF2A -> KeyEvent.KEYCODE_ZENKAKU_HANKAKU
        0xEF31, 0xEF34, 0xEE08, 0xEE0A -> KeyEvent.KEYCODE_LANGUAGE_SWITCH
        0xEF50, 0xEF58, 0xEF95, 0xEF9D -> KeyEvent.KEYCODE_MOVE_HOME
        0xEF51, 0xEF96 -> KeyEvent.KEYCODE_DPAD_LEFT
        0xEF52, 0xEF97 -> KeyEvent.KEYCODE_DPAD_UP
        0xEF53, 0xEF98 -> KeyEvent.KEYCODE_DPAD_RIGHT
        0xEF54, 0xEF99 -> KeyEvent.KEYCODE_DPAD_DOWN
        0xEF55, 0xEF9A -> KeyEvent.KEYCODE_PAGE_UP
        0xEF56, 0xEF9B -> KeyEvent.KEYCODE_PAGE_DOWN
        0xEF57, 0xEF9C -> KeyEvent.KEYCODE_MOVE_END
        0xEF60 -> KeyEvent.KEYCODE_DPAD_CENTER
        0xEF63, 0xEF9E -> KeyEvent.KEYCODE_INSERT
        0xEF67 -> KeyEvent.KEYCODE_MENU
        0xEF68, 0xE0AA -> KeyEvent.KEYCODE_SEARCH
        0xEF6A -> KeyEvent.KEYCODE_HELP
        0xEF7F -> KeyEvent.KEYCODE_NUM_LOCK
        0xEF8D -> KeyEvent.KEYCODE_NUMPAD_ENTER
        0xEF9F, 0xEFFF -> KeyEvent.KEYCODE_FORWARD_DEL
        0xEFAA -> KeyEvent.KEYCODE_NUMPAD_MULTIPLY
        0xEFAB -> KeyEvent.KEYCODE_NUMPAD_ADD
        0xEFAC -> KeyEvent.KEYCODE_NUMPAD_COMMA
        0xEFAD -> KeyEvent.KEYCODE_NUMPAD_SUBTRACT
        0xEFAE -> KeyEvent.KEYCODE_NUMPAD_DOT
        0xEFAF -> KeyEvent.KEYCODE_NUMPAD_DIVIDE
        0xEFBD -> KeyEvent.KEYCODE_NUMPAD_EQUALS
        0xEFE1 -> KeyEvent.KEYCODE_SHIFT_LEFT
        0xEFE2 -> KeyEvent.KEYCODE_SHIFT_RIGHT
        0xEFE3 -> KeyEvent.KEYCODE_CTRL_LEFT
        0xEFE4 -> KeyEvent.KEYCODE_CTRL_RIGHT
        0xEFE5 -> KeyEvent.KEYCODE_CAPS_LOCK
        0xEFE7, 0xEFEB, 0xEFED -> KeyEvent.KEYCODE_META_LEFT
        0xEFE8, 0xEFEC, 0xEFEE -> KeyEvent.KEYCODE_META_RIGHT
        0xEFE9 -> KeyEvent.KEYCODE_ALT_LEFT
        0xEFEA, 0xEF7E -> KeyEvent.KEYCODE_ALT_RIGHT
        0xE001 -> KeyEvent.KEYCODE_MEDIA_EJECT
        0xE05F -> KeyEvent.KEYCODE_SLEEP
        0xE0A6 -> KeyEvent.KEYCODE_BACK
        0xE0A7 -> KeyEvent.KEYCODE_FORWARD
        0xE0A8 -> KeyEvent.KEYCODE_REFRESH
        0xE0AB -> KeyEvent.KEYCODE_BOOKMARK
        0xE0AC -> KeyEvent.KEYCODE_EXPLORER
        0xE0AD -> KeyEvent.KEYCODE_VOLUME_MUTE
        0xE0AE -> KeyEvent.KEYCODE_VOLUME_DOWN
        0xE0AF -> KeyEvent.KEYCODE_VOLUME_UP
        0xE0B0 -> KeyEvent.KEYCODE_MEDIA_NEXT
        0xE0B1 -> KeyEvent.KEYCODE_MEDIA_PREVIOUS
        0xE0B2 -> KeyEvent.KEYCODE_MEDIA_STOP
        0xE0B3 -> KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
        0xE0B4 -> KeyEvent.KEYCODE_ENVELOPE
        0xE0B5 -> KeyEvent.KEYCODE_MUSIC
        0xE0B8 -> KeyEvent.KEYCODE_BRIGHTNESS_DOWN
        0xE0B9 -> KeyEvent.KEYCODE_BRIGHTNESS_UP
        0xE0C0 -> KeyEvent.KEYCODE_APP_SWITCH
        0xE0C1 -> KeyEvent.KEYCODE_HOME
        else -> null
    }

    fun metaState(mask: Int): Int {
        var result = 0
        if (mask and 0x0001 != 0) result = result or KeyEvent.META_SHIFT_ON
        if (mask and 0x0002 != 0) result = result or KeyEvent.META_CTRL_ON
        if (mask and 0x0004 != 0) result = result or KeyEvent.META_ALT_ON
        if (mask and 0x0018 != 0) result = result or KeyEvent.META_META_ON
        if (mask and 0x0020 != 0) {
            result = result or KeyEvent.META_ALT_ON or KeyEvent.META_ALT_RIGHT_ON
        }
        if (mask and 0x1000 != 0) result = result or KeyEvent.META_CAPS_LOCK_ON
        if (mask and 0x2000 != 0) result = result or KeyEvent.META_NUM_LOCK_ON
        if (mask and 0x4000 != 0) result = result or KeyEvent.META_SCROLL_LOCK_ON
        return result
    }

    /** Text fallback must not interpret Deskflow's private control-key range as Unicode. */
    fun unicodeText(keyId: Int): String? {
        if (!Character.isValidCodePoint(keyId) || keyId < 0x20 ||
            keyId in 0x7F..0x9F || keyId in 0xD800..0xDFFF || keyId in 0xE000..0xEFFF
        ) return null
        return String(Character.toChars(keyId))
    }
}
