package com.romcloud.rcnative

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * Réglages d'un jeu (moteur reconnu par [GameSettings]) : préréglages appliqués tout de suite, puis
 * chaque réglage ; « Enregistrer » écrit les valeurs choisies.
 */
@Composable
fun GameSettingsDialog(port: Port, config: GameConfig, onDismiss: () -> Unit, onSaved: (String) -> Unit) {
    // Relecture après un préréglage.
    var generation by remember { mutableStateOf(0) }
    val options = remember(generation) { config.options() }
    val values = remember(generation) {
        mutableStateMapOf<String, String>().apply {
            for (o in options) put(o.id, if (o is ChoiceOption) o.value else if ((o as ToggleOption).value) "1" else "0")
        }
    }
    var applied by remember { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.game_settings)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(port.name, style = MaterialTheme.typography.titleSmall)
                Text(
                    stringResource(R.string.game_settings_engine, config.engine),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (config.presets.isNotEmpty()) {
                    Text(stringResource(R.string.settings_presets), Modifier.padding(top = 12.dp), style = MaterialTheme.typography.labelLarge)
                    Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for ((id, label) in config.presets) {
                            OutlinedButton(onClick = {
                                config.applyPreset(id)
                                applied = label
                                generation++
                            }, contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp)) { Text(label) }
                        }
                    }
                    applied?.let {
                        Text(stringResource(R.string.settings_preset_applied, it), style = MaterialTheme.typography.bodySmall)
                    }
                }
                HorizontalDivider(Modifier.padding(vertical = 8.dp))
                for (option in options) {
                    when (option) {
                        is ChoiceOption -> ChoiceRow(option, values[option.id] ?: option.value) { values[option.id] = it }
                        is ToggleOption -> ToggleRow(option, values[option.id] == "1") { values[option.id] = if (it) "1" else "0" }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                config.save(values.toMap())
                onSaved(port.name)
            }) { Text(stringResource(R.string.save)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) } },
    )
}

@Composable
private fun ChoiceRow(option: ChoiceOption, value: String, onChange: (String) -> Unit) {
    var open by remember { mutableStateOf(false) }
    val current = option.choices.firstOrNull { it.first == value }?.second ?: value
    Row(Modifier.fillMaxWidth().clickable { open = true }.padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(option.label)
            option.help?.let { Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }
        Box {
            Text(current, color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(start = 12.dp))
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                for ((v, label) in option.choices) {
                    DropdownMenuItem(text = { Text(label) }, onClick = {
                        onChange(v)
                        open = false
                    })
                }
            }
        }
    }
}

@Composable
private fun ToggleRow(option: ToggleOption, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(option.label)
            option.help?.let { Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }
        Switch(checked = checked, onCheckedChange = null)
    }
}
