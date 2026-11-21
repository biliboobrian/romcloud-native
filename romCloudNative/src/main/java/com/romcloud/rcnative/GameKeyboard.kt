package com.romcloud.rcnative

import android.content.Context
import android.text.InputType
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager

/** Touches virtuelles de Windows remises au jeu (rcn_keyboard.c). */
object Keys {
    const val BACK = 0x08
    const val TAB = 0x09
    const val RETURN = 0x0D
    const val CONTROL = 0x11
    const val ESCAPE = 0x1B
    const val V = 0x56
    const val LEFT = 0x25
    const val UP = 0x26
    const val RIGHT = 0x27
    const val DOWN = 0x28
    const val DELETE = 0x2E
    const val HOME = 0x24
    const val END = 0x23

    /** Touche Android sans caractère → touche de Windows, ou 0. */
    fun of(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_DEL -> BACK
        KeyEvent.KEYCODE_FORWARD_DEL -> DELETE
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> RETURN
        KeyEvent.KEYCODE_TAB -> TAB
        KeyEvent.KEYCODE_ESCAPE -> ESCAPE
        KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
        KeyEvent.KEYCODE_DPAD_UP -> UP
        KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
        KeyEvent.KEYCODE_MOVE_HOME -> HOME
        KeyEvent.KEYCODE_MOVE_END -> END
        else -> 0
    }

    fun press(vk: Int) {
        PortRunner.key(vk, true)
        PortRunner.key(vk, false)
    }

    /** Ctrl+V : colle le texte tapé dans l'application (presse-papiers du jeu). */
    fun paste() {
        PortRunner.key(CONTROL, true)
        PortRunner.key(V, true)
        PortRunner.key(V, false)
        PortRunner.key(CONTROL, false)
    }
}

/**
 * Vue invisible qui reçoit le clavier de l'appareil pendant le jeu : le texte et les touches (effacement,
 * entrée…) sont remis au jeu en frappes. Saisie sans suggestions ni composition (type « mot de passe
 * visible ») : chaque caractère arrive aussitôt.
 */
class KeyboardInputView(context: Context) : View(context) {
    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onCheckIsTextEditor() = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN or EditorInfo.IME_ACTION_DONE
        return object : BaseInputConnection(this, false) {
            override fun commitText(text: CharSequence, newCursorPosition: Int): Boolean {
                if (text.isNotEmpty()) PortRunner.keyText(text.toString())
                return true
            }

            override fun setComposingText(text: CharSequence, newCursorPosition: Int): Boolean {
                // Composition (claviers qui ne respectent pas « sans suggestions ») : texte remis tel quel.
                if (text.isNotEmpty()) PortRunner.keyText(text.toString())
                return true
            }

            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                repeat(beforeLength.coerceAtMost(64)) { Keys.press(Keys.BACK) }
                repeat(afterLength.coerceAtMost(64)) { Keys.press(Keys.DELETE) }
                return true
            }

            override fun sendKeyEvent(event: KeyEvent): Boolean {
                val vk = Keys.of(event.keyCode)
                when {
                    vk != 0 -> PortRunner.key(vk, event.action == KeyEvent.ACTION_DOWN)
                    event.action == KeyEvent.ACTION_DOWN && event.unicodeChar != 0 ->
                        PortRunner.keyText(String(Character.toChars(event.unicodeChar)))
                }
                return true
            }

            override fun performEditorAction(actionCode: Int): Boolean {
                Keys.press(Keys.RETURN)
                return true
            }
        }
    }

    fun show() {
        requestFocus()
        (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(this, 0)
    }

    fun hide() {
        (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(windowToken, 0)
        clearFocus()
    }
}
