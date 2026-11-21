package com.romcloud.rcnative

import android.content.Context
import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.MotionEvent
import android.view.KeyEvent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/** Affichage de la manette virtuelle : selon la présence d'une manette, toujours, ou jamais. */
enum class VirtualPadMode { AUTO, ALWAYS, NEVER }

object VirtualPadSettings {
    private const val PREFS = "settings"
    private const val KEY = "virtual_pad"

    fun mode(context: Context): VirtualPadMode =
        runCatching { VirtualPadMode.valueOf(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null) ?: "") }
            .getOrDefault(VirtualPadMode.AUTO)

    fun setMode(context: Context, mode: VirtualPadMode) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, mode.name).apply()
    }

    /** Une manette physique (USB ou Bluetooth) est-elle branchée ? */
    fun gamepadConnected(context: Context): Boolean {
        val input = context.getSystemService(Context.INPUT_SERVICE) as InputManager
        return input.inputDeviceIds.any { id ->
            val device = input.getInputDevice(id) ?: return@any false
            if (device.isVirtual) return@any false
            val gamepad = device.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD
            val joystick = device.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
            if (!gamepad && !joystick) return@any false
            // Manette USB ou Bluetooth : de vrais boutons de manette ou des sticks. Exclut les boutons du
            // téléphone et les capteurs que certains modèles déclarent comme « joystick ».
            val buttons = device.hasKeys(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_START)
                .any { it }
            val sticks = device.motionRanges.any { it.axis == MotionEvent.AXIS_X || it.axis == MotionEvent.AXIS_Z }
            buttons || (joystick && sticks)
        }
    }
}

/** Zone tactile d'un bouton : cercle (centre, rayon) ou rectangle arrondi (gâchettes, Start, View). */
private class VButton(val bit: Int, val label: String, val color: Color, val center: Offset, val radius: Float, val rect: Rect? = null) {
    fun contains(p: Offset, slop: Float): Boolean =
        rect?.inflate(slop)?.contains(p) ?: (hypot(p.x - center.x, p.y - center.y) <= radius + slop)
}

/** Stick analogique : zone de prise (centre, rayon), axes x et y qu'il commande (indices de [Axes]). */
private class VStick(val label: String, val center: Offset, val radius: Float, val axisX: Int, val axisY: Int) {
    fun grabs(p: Offset): Boolean = hypot(p.x - center.x, p.y - center.y) <= radius * 1.35f
    /** Position du doigt ramenée à -1 … 1 sur chaque axe (le bord du stick est la course entière). */
    fun value(p: Offset): Offset {
        var d = (p - center) / radius
        val length = hypot(d.x, d.y)
        if (length > 1f) d /= length
        return d
    }
}

/** Disposition des commandes, calculée pour la taille de l'écran (pixels). */
private class Layout(width: Float, height: Float, unit: Float) {
    val leftStick = VStick("L", Offset(unit * 2.7f, height - unit * 2.5f), unit * 1.6f, 0, 1)
    val rightStick = VStick("R", Offset(width - unit * 6.4f, height - unit * 2.0f), unit * 1.45f, 2, 3)
    val sticks = listOf(leftStick, rightStick)
    val dpadCenter = Offset(unit * 2.7f, height - unit * 6.3f)
    val dpadRadius = unit * 1.25f
    val buttons: List<VButton>

    init {
        val face = Offset(width - unit * 2.4f, height - unit * 3.0f)
        val gap = unit * 1.15f
        val r = unit * 0.62f
        val shoulder = Size(unit * 2.6f, unit * 1.0f)
        val trigger = Size(unit * 2.6f, unit * 0.9f)
        val small = Size(unit * 1.4f, unit * 0.7f)
        val left = unit * 0.6f
        val right = width - unit * 0.6f - shoulder.width
        buttons = listOf(
            VButton(Pad.A, "A", Color(0xFF5DBB4E), face + Offset(0f, gap), r),
            VButton(Pad.B, "B", Color(0xFFE5483E), face + Offset(gap, 0f), r),
            VButton(Pad.X, "X", Color(0xFF3D8FE6), face + Offset(-gap, 0f), r),
            VButton(Pad.Y, "Y", Color(0xFFF2C230), face + Offset(0f, -gap), r),
            VButton(Pad.L, "LB", Color.White, Offset.Zero, 0f, Rect(Offset(left, unit * 0.5f), shoulder)),
            VButton(Pad.R, "RB", Color.White, Offset.Zero, 0f, Rect(Offset(right, unit * 0.5f), shoulder)),
            VButton(Pad.LT, "LT", Color.White, Offset.Zero, 0f, Rect(Offset(left, unit * 1.75f), trigger)),
            VButton(Pad.RT, "RT", Color.White, Offset.Zero, 0f, Rect(Offset(right, unit * 1.75f), trigger)),
            VButton(Pad.SELECT, "View", Color.White, Offset.Zero, 0f, Rect(Offset(width / 2 - unit * 0.3f - small.width, unit * 0.5f), small)),
            VButton(Pad.START, "Menu", Color.White, Offset.Zero, 0f, Rect(Offset(width / 2 + unit * 0.3f, unit * 0.5f), small)),
        )
    }

    /** Boutons enfoncés par les doigts posés aux points `touches` (hors doigts tenus par un stick). */
    fun pressed(touches: List<Offset>, unit: Float): Int {
        var bits = 0
        for (p in touches) {
            // Croix : direction du doigt par rapport à son centre, huit directions.
            val d = p - dpadCenter
            if (hypot(d.x, d.y) <= dpadRadius * 1.35f) {
                val dead = dpadRadius * 0.25f
                if (d.x < -dead && abs(d.x) > abs(d.y) * 0.4f) bits = bits or Pad.LEFT
                if (d.x > dead && abs(d.x) > abs(d.y) * 0.4f) bits = bits or Pad.RIGHT
                if (d.y < -dead && abs(d.y) > abs(d.x) * 0.4f) bits = bits or Pad.UP
                if (d.y > dead && abs(d.y) > abs(d.x) * 0.4f) bits = bits or Pad.DOWN
                continue
            }
            // Bouton le plus proche parmi ceux que le doigt touche (avec une marge).
            buttons.filter { it.contains(p, unit * 0.35f) }
                .minByOrNull { b -> b.rect?.let { hypot(p.x - it.center.x, p.y - it.center.y) } ?: hypot(p.x - b.center.x, p.y - b.center.y) }
                ?.let { bits = bits or it.bit }
        }
        return bits
    }
}

/**
 * Manette virtuelle aux couleurs d'une manette Xbox, par-dessus le jeu : stick gauche et croix à gauche,
 * A B X Y et stick droit à droite, LB / RB, LT / RT et View / Menu en haut. Plusieurs doigts à la fois ;
 * un stick garde le doigt qui l'a pris tant qu'il reste posé, même s'il sort de sa zone.
 * `onChange` reçoit les boutons enfoncés (bits de [Pad]) et les axes (sticks gauche x y, droit x y,
 * gâchettes) à chaque changement. Un doigt posé hors des commandes est un clic de souris à cet endroit de
 * l'image ([videoWidth] × [videoHeight], 0 : toute la vue).
 */
@Composable
fun VirtualPad(onChange: (Int, FloatArray) -> Unit, modifier: Modifier = Modifier, videoWidth: Int = 0, videoHeight: Int = 0) {
    var pressed by remember { mutableIntStateOf(0) }
    var axes by remember { mutableStateOf(FloatArray(6)) }
    val density = LocalDensity.current
    BoxWithConstraints(modifier.fillMaxSize()) {
        val width = constraints.maxWidth.toFloat()
        val height = constraints.maxHeight.toFloat()
        // Unité : 40 dp, réduite sur les petits écrans.
        val unit = min(with(density) { 40f * this.density }, min(width, height) / 9f)
        val layout = remember(width, height, unit) { Layout(width, height, unit) }
        val video = remember(width, height, videoWidth, videoHeight) { videoRect(width, height, videoWidth, videoHeight) }
        Canvas(
            Modifier.fillMaxSize().pointerInput(layout, video) {
                // Doigt (identifiant) → stick qu'il tient, et doigts servant de clic de souris.
                val held = HashMap<Long, VStick>()
                val clicking = HashSet<Long>()
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        val next = FloatArray(6)
                        val free = ArrayList<Offset>()
                        for (change in event.changes) {
                            val id = change.id.value
                            // Doigt hors des commandes : clic de souris à cet endroit de l'image.
                            if (id !in held && layout.sticks.none { it.grabs(change.position) } &&
                                layout.pressed(listOf(change.position), unit) == 0) {
                                if (!change.previousPressed) {
                                    clicking.add(id)
                                    Mouse.press(change.position, video)
                                } else if (!change.pressed) {
                                    if (clicking.remove(id)) Mouse.release(change.position, video)
                                } else {
                                    Mouse.move(change.position, video)
                                }
                            }
                            if (!change.pressed) {
                                held.remove(id)
                                clicking.remove(id)
                                continue
                            }
                            // Un doigt qui se pose sur un stick le prend.
                            if (id !in held && !change.previousPressed) {
                                layout.sticks.firstOrNull { it.grabs(change.position) && it !in held.values }?.let { held[id] = it }
                            }
                            val stick = held[id]
                            if (stick != null) {
                                val v = stick.value(change.position)
                                next[stick.axisX] = v.x
                                next[stick.axisY] = v.y
                            } else {
                                free.add(change.position)
                            }
                        }
                        val bits = layout.pressed(free, unit)
                        // Gâchettes tactiles : à fond quand elles sont touchées.
                        if (bits and Pad.LT != 0) next[4] = 1f
                        if (bits and Pad.RT != 0) next[5] = 1f
                        event.changes.forEach { it.consume() }
                        if (bits != pressed || !next.contentEquals(axes)) {
                            pressed = bits
                            axes = next
                            onChange(bits, next)
                        }
                    }
                }
            },
        ) {
            drawDpad(layout, pressed)
            for (stick in layout.sticks) drawStick(stick, axes[stick.axisX], axes[stick.axisY], unit)
            layout.buttons.forEach { drawButton(it, pressed and it.bit != 0, unit) }
        }
    }
}

/** Rectangle de l'image du jeu dans la vue : elle garde ses proportions, centrée, bandes noires autour. */
internal fun videoRect(width: Float, height: Float, videoWidth: Int, videoHeight: Int): Rect {
    if (width <= 0f || height <= 0f || videoWidth <= 0 || videoHeight <= 0) return Rect(0f, 0f, width, height)
    val aspect = videoWidth.toFloat() / videoHeight
    return if (width / height > aspect) {
        val w = height * aspect
        Rect((width - w) / 2f, 0f, (width + w) / 2f, height)
    } else {
        val h = width / aspect
        Rect(0f, (height - h) / 2f, width, (height + h) / 2f)
    }
}

/** Toucher de l'image hors des commandes virtuelles : pointeur de la souris du jeu (0…65535) et clic gauche. */
internal object Mouse {
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())
    private var pendingDown: Runnable? = null

    fun move(p: Offset, area: Rect) {
        if (area.width <= 0f || area.height <= 0f) return
        val nx = ((p.x - area.left) / area.width).coerceIn(0f, 1f)
        val ny = ((p.y - area.top) / area.height).coerceIn(0f, 1f)
        PortRunner.mouseMove((nx * 65535f).toInt(), (ny * 65535f).toInt())
    }

    fun button(down: Boolean) = PortRunner.mouseButton(0, down)

    /**
     * Doigt posé : le pointeur va à cet endroit, puis le bouton s'enfonce une image plus tard. Le jeu lit le
     * déplacement et le clic dans le même relevé : sans ce délai, il teste le clic à la position précédente
     * du curseur (le champ visé ne s'activait jamais).
     */
    fun press(p: Offset, area: Rect) {
        move(p, area)
        cancel()
        val down = Runnable {
            pendingDown = null
            PortRunner.mouseButton(0, true)
        }
        pendingDown = down
        handler.postDelayed(down, 70)
    }

    /** Doigt levé : relâche le bouton (un appui très bref est cliqué quand même). */
    fun release(p: Offset, area: Rect) {
        move(p, area)
        val down = pendingDown
        cancel()
        if (down != null) {
            down.run()
            handler.postDelayed({ PortRunner.mouseButton(0, false) }, 60)
        } else {
            PortRunner.mouseButton(0, false)
        }
    }

    private fun cancel() {
        pendingDown?.let { handler.removeCallbacks(it) }
        pendingDown = null
    }
}

private val idle = Color.White.copy(alpha = 0.18f)
private val outline = Color.White.copy(alpha = 0.45f)

private fun DrawScope.drawStick(stick: VStick, x: Float, y: Float, unit: Float) {
    drawCircle(Color.Black.copy(alpha = 0.18f), stick.radius, stick.center)
    drawCircle(outline, stick.radius, stick.center, style = Stroke(2f))
    val knob = stick.center + Offset(x, y) * stick.radius * 0.6f
    val active = x != 0f || y != 0f
    drawCircle(Color.White.copy(alpha = if (active) 0.55f else 0.28f), stick.radius * 0.45f, knob)
    drawCircle(outline, stick.radius * 0.45f, knob, style = Stroke(2f))
    drawContext.canvas.nativeCanvas.drawText(
        stick.label + "S",
        knob.x,
        knob.y + unit * 0.14f,
        android.graphics.Paint().apply {
            isAntiAlias = true
            textAlign = android.graphics.Paint.Align.CENTER
            textSize = unit * 0.4f
            isFakeBoldText = true
            color = android.graphics.Color.argb(200, 255, 255, 255)
        },
    )
}

private fun DrawScope.drawDpad(layout: Layout, pressed: Int) {
    val c = layout.dpadCenter
    val r = layout.dpadRadius
    val w = r * 0.62f
    drawCircle(Color.Black.copy(alpha = 0.18f), r * 1.12f, c)
    // Quatre branches de la croix, éclairées quand leur direction est enfoncée.
    val arms = listOf(
        Pad.UP to Rect(Offset(c.x - w / 2, c.y - r), Size(w, r - w / 2)),
        Pad.DOWN to Rect(Offset(c.x - w / 2, c.y + w / 2), Size(w, r - w / 2)),
        Pad.LEFT to Rect(Offset(c.x - r, c.y - w / 2), Size(r - w / 2, w)),
        Pad.RIGHT to Rect(Offset(c.x + w / 2, c.y - w / 2), Size(r - w / 2, w)),
    )
    for ((bit, rect) in arms) {
        drawRoundRect(if (pressed and bit != 0) Color.White.copy(alpha = 0.6f) else idle, rect.topLeft, rect.size, CornerRadius(w * 0.2f))
        drawRoundRect(outline, rect.topLeft, rect.size, CornerRadius(w * 0.2f), style = Stroke(2f))
    }
    drawRect(idle, Offset(c.x - w / 2, c.y - w / 2), Size(w, w))
    // Flèches sur les branches.
    val a = w * 0.22f
    for ((dx, dy) in listOf(0f to -1f, 0f to 1f, -1f to 0f, 1f to 0f)) {
        val tip = c + Offset(dx, dy) * (r * 0.78f)
        val base = c + Offset(dx, dy) * (r * 0.78f - a * 1.4f)
        val side = Offset(-dy, dx) * a
        val path = Path().apply {
            moveTo(tip.x, tip.y)
            lineTo(base.x + side.x, base.y + side.y)
            lineTo(base.x - side.x, base.y - side.y)
            close()
        }
        drawPath(path, outline)
    }
}

private fun DrawScope.drawButton(b: VButton, down: Boolean, unit: Float) {
    val fill = if (down) b.color.copy(alpha = 0.75f) else b.color.copy(alpha = if (b.rect == null) 0.32f else 0.16f)
    val rect = b.rect
    if (rect != null) {
        drawRoundRect(fill, rect.topLeft, rect.size, CornerRadius(rect.height / 2))
        drawRoundRect(outline, rect.topLeft, rect.size, CornerRadius(rect.height / 2), style = Stroke(2f))
    } else {
        drawCircle(fill, b.radius, b.center)
        drawCircle(b.color.copy(alpha = 0.8f), b.radius, b.center, style = Stroke(3f))
    }
    val center = rect?.center ?: b.center
    val size = if (rect == null) unit * 0.55f else unit * 0.38f
    drawContext.canvas.nativeCanvas.drawText(
        b.label,
        center.x,
        center.y + size * 0.36f,
        android.graphics.Paint().apply {
            isAntiAlias = true
            textAlign = android.graphics.Paint.Align.CENTER
            textSize = size
            isFakeBoldText = true
            color = android.graphics.Color.argb(if (down) 255 else 200, 255, 255, 255)
        },
    )
}
