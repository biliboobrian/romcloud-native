package com.romcloud.rcnative

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.Process
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.material.icons.filled.Keyboard
import androidx.compose.material.icons.Icons
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.ui.draw.alpha
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * Exécute un jeu transposé. Tant qu'il n'affiche rien, sa sortie texte est montrée (programmes en
 * mode console, messages de démarrage) ; à sa première image, l'écran passe au jeu en plein écran,
 * en paysage, et la manette lui est transmise — manette physique, ou manette virtuelle à l'écran sans\n * manette branchée (réglage dans la liste des jeux).
 * L'activité a son propre processus (manifeste) : la quitter arrête le jeu.
 */
class PortActivity : ComponentActivity(), PortSink, SurfaceHolder.Callback {
    private var title by mutableStateOf("")
    private var output by mutableStateOf("")
    private var status by mutableStateOf<String?>(null)
    private var failed by mutableStateOf(false)

    /** Taille de l'image du jeu, dès qu'il en affiche une. */
    private var video by mutableStateOf<Pair<Int, Int>?>(null)

    private val pending = StringBuilder()
    private var refreshPosted = false

    /** Boutons enfoncés (touches) et direction des sticks ou de la croix (axes) : bits [Pad]. */
    private var keyButtons = 0
    private var axisButtons = 0
    /** Boutons de la manette virtuelle (écran tactile). */
    private var virtualButtons = 0

    /** Axes (sticks gauche x y, droit x y, gâchettes) : manette physique et manette virtuelle. */
    private val physicalAxes = FloatArray(6)
    private var virtualAxes = FloatArray(6)

    /** Clavier de l'appareil pour la saisie de texte : vue qui le reçoit, et état montré ou caché. */
    private var keyboardView: KeyboardInputView? = null
    private var keyboardShown = false

    /** Indicateurs de performance (réglage global, lu au lancement). */
    private var showPerf = false

    /** Manette virtuelle affichée : réglage, et en mode automatique, absence de manette physique. */
    private var showPad by mutableStateOf(false)
    private var padMode = VirtualPadMode.AUTO
    /** Manette USB branchée ou débranchée : la manette virtuelle suit (en plus de l'écoute des entrées). */
    private val usb = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: android.content.Context, intent: android.content.Intent) {
            // Le périphérique d'entrée apparaît un peu après le branchement.
            window.decorView.postDelayed({ updatePad() }, 500)
        }
    }
    private val devices = object : android.hardware.input.InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = updatePad()
        override fun onInputDeviceRemoved(deviceId: Int) = updatePad()
        override fun onInputDeviceChanged(deviceId: Int) = updatePad()
    }

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val port = intent.getStringExtra(EXTRA_PORT)?.let { PortStore.find(this, it) }
        if (port == null) {
            finish()
            return
        }
        title = port.name
        showPerf = PerfOverlay.enabled(this)
        padMode = VirtualPadSettings.mode(this)
        updatePad()
        (getSystemService(INPUT_SERVICE) as android.hardware.input.InputManager).registerInputDeviceListener(devices, null)
        val usbEvents = android.content.IntentFilter().apply {
            addAction(android.hardware.usb.UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(android.hardware.usb.UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        androidx.core.content.ContextCompat.registerReceiver(this, usb, usbEvents, androidx.core.content.ContextCompat.RECEIVER_EXPORTED)
        setContent {
            RcnTheme {
                val size = video
                if (size != null) {
                    Box(Modifier.fillMaxSize().background(Color.Black)) {
                        // L'image garde ses proportions ; le système l'agrandit à la taille de la vue.
                        AndroidView(
                            factory = { context -> SurfaceView(context).also { it.holder.addCallback(this@PortActivity) } },
                            modifier = Modifier.align(Alignment.Center).aspectRatio(size.first.toFloat() / size.second),
                        )
                        // Reçoit le clavier de l'appareil (invisible).
                        AndroidView(
                            factory = { context -> KeyboardInputView(context).also { keyboardView = it } },
                            modifier = Modifier.size(1.dp).alpha(0f),
                        )
                        if (showPad) {
                            VirtualPad(videoWidth = size.first, videoHeight = size.second, onChange = { buttons, axes ->
                                virtualButtons = buttons
                                virtualAxes = axes
                                sendPad()
                                sendAxes()
                            })
                        } else {
                            // Sans manette virtuelle, tout toucher de l'image est un clic de souris du jeu.
                            MouseTouches(size.first, size.second)
                        }
                        // Clavier à la demande (le jeu ne dit pas toujours qu'il attend du texte) : à droite du
                        // bouton Menu de la manette virtuelle, ou en haut à droite de l'écran sans elle.
                        KeyboardButton(besideMenu = showPad, onClick = { setKeyboard(!keyboardShown) })
                        // Indicateurs : en haut au centre, sous les boutons View / Menu (l'affichage de DXVK est en haut à gauche de l'image).
                        if (showPerf) PerfPanel(Modifier.align(Alignment.TopCenter).padding(top = 56.dp))
                        status?.let {
                            Text(
                                it,
                                Modifier.align(Alignment.BottomCenter).background(Color(0xCC000000)).padding(12.dp),
                                color = if (failed) MaterialTheme.colorScheme.error else Color.White,
                            )
                        }
                    }
                } else {
                    Scaffold(
                        topBar = { TopAppBar(title = { Text(title) }) },
                        bottomBar = {
                            Surface(color = MaterialTheme.colorScheme.surfaceVariant) {
                                Text(
                                    status ?: stringResource(R.string.running),
                                    Modifier.fillMaxWidth().padding(16.dp),
                                    color = if (failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                                    style = MaterialTheme.typography.bodyMedium,
                                )
                            }
                        },
                    ) { padding ->
                        val vertical = rememberScrollState()
                        // La console suit la sortie du jeu.
                        LaunchedEffect(output) { vertical.scrollTo(vertical.maxValue) }
                        Column(Modifier.fillMaxSize().padding(padding).verticalScroll(vertical)) {
                            Text(
                                output,
                                Modifier.horizontalScroll(rememberScrollState()).padding(12.dp),
                                fontFamily = FontFamily.Monospace,
                                fontSize = 11.sp,
                                lineHeight = 13.sp,
                                softWrap = false,
                            )
                        }
                    }
                }
            }
        }
        // Une seule exécution par processus : l'activité n'est pas recréée (configChanges).
        if (savedInstanceState == null) {
            // Ce thread ne fait qu'attendre la fin du jeu, qui tourne sur ses propres threads (runtime).
            PortRunner.nativeLibraryDir = applicationInfo.nativeLibraryDir
            val driver = VulkanDrivers.selected(this)?.library
            val dxvk = VulkanDrivers.dxvk(this)
            Thread({ PortRunner.run(port, this, driver, dxvk, showPerf) }, "rcn-game").start()
        }
    }

    override fun onOutput(stream: Int, data: ByteArray) {
        synchronized(pending) {
            pending.append(String(data, Charsets.UTF_8))
            if (refreshPosted) return
            refreshPosted = true
        }
        runOnUiThread(::flushOutput)
    }

    private fun flushOutput() {
        val text = synchronized(pending) {
            refreshPosted = false
            pending.toString().also { pending.setLength(0) }
        }
        // Seule la fin d'une sortie très longue est gardée à l'écran.
        output = (output + text).takeLast(MAX_OUTPUT)
    }

    override fun onVideo(width: Int, height: Int) {
        runOnUiThread {
            if (video == null) enterGameScreen()
            video = width to height
        }
    }

    /** Plein écran, paysage, écran toujours allumé. */
    private fun enterGameScreen() {
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.getInsetsController(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    override fun onResult(status: Int, exitCode: Int, error: String) {
        runOnUiThread {
            flushOutput()
            failed = status != PortRunner.OK
            this.status = if (failed) getString(R.string.stopped_error, error) else getString(R.string.finished, exitCode)
            // Un jeu à l'écran qui se termine de lui-même rend la main à la liste.
            if (!failed && video != null) finish()
        }
    }

    // --- Surface d'affichage ---

    override fun surfaceCreated(holder: SurfaceHolder) = PortRunner.setSurface(holder.surface)

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = PortRunner.setSurface(holder.surface)

    override fun surfaceDestroyed(holder: SurfaceHolder) = PortRunner.setSurface(null)

    // --- Manette (et clavier, avec les touches habituelles de ces jeux) ---

    private fun updatePad() {
        val show = when (padMode) {
            VirtualPadMode.ALWAYS -> true
            VirtualPadMode.NEVER -> false
            VirtualPadMode.AUTO -> !VirtualPadSettings.gamepadConnected(this)
        }
        if (!show && (virtualButtons != 0 || virtualAxes.any { it != 0f })) {
            virtualButtons = 0
            virtualAxes = FloatArray(6)
            sendPad()
            sendAxes()
        }
        showPad = show
    }

    private fun sendPad() = PortRunner.setPad(keyButtons or axisButtons or virtualButtons)

    /** Par axe, la plus marquée des deux manettes. */
    private fun sendAxes() {
        val a = FloatArray(6) { i -> strongest(physicalAxes[i], virtualAxes[i]) }
        PortRunner.setAxes(a[0], a[1], a[2], a[3], a[4], a[5])
    }

    /** Une manette physique sert : la manette virtuelle s'efface (mode automatique). */
    private fun physicalInput(source: Int) {
        if (padMode == VirtualPadMode.AUTO && showPad &&
            (source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD || source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
        ) {
            updatePad()
            if (showPad) showPad = false
        }
    }

    /** Clavier de l'appareil montré ou caché (bouton, ou demande du jeu). */
    private fun setKeyboard(show: Boolean) {
        val view = keyboardView ?: return
        keyboardShown = show
        if (show) view.show() else view.hide()
    }

    override fun onTextInput(active: Boolean) = runOnUiThread { setKeyboard(active) }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        // Clavier (physique ou clavier virtuel à touches, type Hacker's Keyboard) : les frappes sont remises
        // au jeu. La manette garde ses boutons.
        val gamepad = event.source and (InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK) != 0
        if (!gamepad) {
            val vk = Keys.of(event.keyCode)
            when {
                vk != 0 -> PortRunner.key(vk, event.action == KeyEvent.ACTION_DOWN)
                event.action == KeyEvent.ACTION_DOWN && event.unicodeChar != 0 ->
                    PortRunner.keyText(String(Character.toChars(event.unicodeChar)))
                else -> return super.dispatchKeyEvent(event)
            }
            return true
        }
        val button = Pad.ofKey(event.keyCode) ?: return super.dispatchKeyEvent(event)
        physicalInput(event.source)
        keyButtons = if (event.action == KeyEvent.ACTION_UP) keyButtons and button.inv() else keyButtons or button
        sendPad()
        return true
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0 || event.action != MotionEvent.ACTION_MOVE) {
            return super.dispatchGenericMotionEvent(event)
        }
        physicalInput(event.source)
        // Croix (HAT) : boutons de direction. Sticks et gâchettes : axes analogiques.
        val hx = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        axisButtons = (if (hx < -STICK_THRESHOLD) Pad.LEFT else if (hx > STICK_THRESHOLD) Pad.RIGHT else 0) or
            (if (hy < -STICK_THRESHOLD) Pad.UP else if (hy > STICK_THRESHOLD) Pad.DOWN else 0)
        val device = event.device
        // Stick droit : Z / RZ (la plupart des manettes), sinon RX / RY.
        val zAxes = device?.getMotionRange(MotionEvent.AXIS_RZ, event.source) != null
        physicalAxes[0] = dead(event.getAxisValue(MotionEvent.AXIS_X))
        physicalAxes[1] = dead(event.getAxisValue(MotionEvent.AXIS_Y))
        physicalAxes[2] = dead(event.getAxisValue(if (zAxes) MotionEvent.AXIS_Z else MotionEvent.AXIS_RX))
        physicalAxes[3] = dead(event.getAxisValue(if (zAxes) MotionEvent.AXIS_RZ else MotionEvent.AXIS_RY))
        physicalAxes[4] = maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)).coerceIn(0f, 1f)
        physicalAxes[5] = maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)).coerceIn(0f, 1f)
        sendPad()
        sendAxes()
        return true
    }

    private fun strongest(a: Float, b: Float) = if (kotlin.math.abs(a) >= kotlin.math.abs(b)) a else b

    /** Zone morte d'un stick : les petits écarts au repos ne comptent pas. */
    private fun dead(v: Float) = if (kotlin.math.abs(v) < STICK_DEAD_ZONE) 0f else v

    override fun onResume() {
        super.onResume()
        // Manette branchée ou débranchée pendant que l'application était en arrière-plan.
        updatePad()
    }

    override fun onDestroy() {
        super.onDestroy()
        (getSystemService(INPUT_SERVICE) as android.hardware.input.InputManager).unregisterInputDeviceListener(devices)
        runCatching { unregisterReceiver(usb) } // pas enregistré si le jeu était introuvable
        // Le jeu tourne peut-être encore sur ses threads : la fin du processus l'arrête.
        if (isFinishing) Process.killProcess(Process.myPid())
    }

    companion object {
        const val EXTRA_PORT = "port"
        private const val MAX_OUTPUT = 200_000
        private const val STICK_THRESHOLD = 0.5f
        private const val STICK_DEAD_ZONE = 0.12f
    }
}

/** Boutons de la manette vus par le jeu : mêmes bits que RCN_PAD_* (rcn_host.h). */
object Pad {
    const val UP = 1 shl 0
    const val DOWN = 1 shl 1
    const val LEFT = 1 shl 2
    const val RIGHT = 1 shl 3
    const val A = 1 shl 4
    const val B = 1 shl 5
    const val X = 1 shl 6
    const val Y = 1 shl 7
    const val START = 1 shl 8
    const val SELECT = 1 shl 9
    const val L = 1 shl 10
    const val R = 1 shl 11
    const val L3 = 1 shl 12
    const val R3 = 1 shl 13
    const val LT = 1 shl 14
    const val RT = 1 shl 15

    /** Bouton d'une touche de manette ou de clavier, ou null si elle n'est pas transmise au jeu. */
    fun ofKey(keyCode: Int): Int? = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> UP
        KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> B
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_C -> X
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_D -> Y
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_ESCAPE -> SELECT
        KeyEvent.KEYCODE_BUTTON_L1 -> L
        KeyEvent.KEYCODE_BUTTON_R1 -> R
        KeyEvent.KEYCODE_BUTTON_L2 -> LT
        KeyEvent.KEYCODE_BUTTON_R2 -> RT
        KeyEvent.KEYCODE_BUTTON_THUMBL -> L3
        KeyEvent.KEYCODE_BUTTON_THUMBR -> R3
        else -> null
    }
}

/** Toucher de l'image (sans manette virtuelle) : pointeur de la souris du jeu et clic gauche à cet endroit. */
@Composable
private fun MouseTouches(videoWidth: Int, videoHeight: Int) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val width = constraints.maxWidth.toFloat()
        val height = constraints.maxHeight.toFloat()
        val area = remember(width, height, videoWidth, videoHeight) { videoRect(width, height, videoWidth, videoHeight) }
        Box(
            Modifier.fillMaxSize().pointerInput(area) {
                awaitPointerEventScope {
                    val clicking = HashSet<Long>()
                    while (true) {
                        val event = awaitPointerEvent()
                        for (change in event.changes) {
                            val id = change.id.value
                            if (!change.previousPressed) {
                                clicking.add(id)
                                Mouse.press(change.position, area)
                            } else if (!change.pressed) {
                                if (clicking.remove(id)) Mouse.release(change.position, area)
                            } else {
                                Mouse.move(change.position, area)
                            }
                        }
                        event.changes.forEach { it.consume() }
                    }
                }
            },
        )
    }
}

/** Boutons du clavier : tabulation, puis clavier tactile ; à droite du bouton Menu de [VirtualPad] (à sa
 *  hauteur), ou en haut à droite. */
@Composable
private fun KeyboardButton(besideMenu: Boolean, onClick: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val density = LocalDensity.current
        val width = constraints.maxWidth.toFloat()
        val height = constraints.maxHeight.toFloat()
        val unit = kotlin.math.min(with(density) { 40f * this.density }, kotlin.math.min(width, height) / 9f)
        val x = if (besideMenu) width / 2 + unit * 2.0f else width - unit * 1.6f
        val y = unit * 0.5f
        // Tabulation, à côté du bouton du clavier : certains jeux l'attendent dans leurs champs de saisie
        // (NieR: Automata, pseudo), et le clavier d'Android n'en a pas. À droite quand la manette occupe le
        // centre (le panneau de statistiques est juste à gauche), à gauche sans elle.
        Box(
            Modifier
                .offset { IntOffset((x + (if (besideMenu) unit * 1.3f else -unit * 1.3f)).toInt(), y.toInt()) }
                .size(with(density) { (unit * 1.1f).toDp() }, with(density) { (unit * 0.7f).toDp() })
                .clip(RoundedCornerShape(50))
                .background(Color(0x33FFFFFF))
                .clickable { Keys.press(Keys.TAB) },
            contentAlignment = Alignment.Center,
        ) {
            Text("TAB", color = Color.White.copy(alpha = 0.8f), fontSize = with(density) { (unit * 0.28f).toSp() })
        }
        Box(
            Modifier
                .offset { IntOffset(x.toInt(), y.toInt()) }
                .size(with(density) { (unit * 1.1f).toDp() }, with(density) { (unit * 0.7f).toDp() })
                .clip(RoundedCornerShape(50))
                .background(Color(0x33FFFFFF))
                .clickable(onClick = onClick),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.Keyboard,
                contentDescription = stringResource(R.string.keyboard),
                tint = Color.White.copy(alpha = 0.8f),
                modifier = Modifier.size(with(density) { (unit * 0.5f).toDp() }),
            )
        }
    }
}
