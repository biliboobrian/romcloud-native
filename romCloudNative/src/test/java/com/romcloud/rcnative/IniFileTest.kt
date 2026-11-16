package com.romcloud.rcnative

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

class IniFileTest {
    private fun temp(text: String): File = File.createTempFile("prefs", ".ini").apply {
        deleteOnExit()
        writeText(text)
    }

    @Test
    fun readsValuesWithoutCaseOrSpaces() {
        val ini = IniFile(temp("[Display]\r\niSize W=1920\r\n  bUseTAA = 1\r\n[Grass]\r\nfGrassStartFadeDistance=7000.0000\r\n"))
        assertEquals("1920", ini.get("display", "isize w"))
        assertEquals("1", ini.get("Display", "bUseTAA"))
        assertEquals("7000.0000", ini.get("Grass", "fGrassStartFadeDistance"))
        assertNull(ini.get("Grass", "iSize W"))
    }

    @Test
    fun writesInPlaceAddsKeysAndSections() {
        val file = temp("[Display]\r\niSize W=1920\r\niSize H=1080\r\n\r\n[Grass]\r\nfGrassStartFadeDistance=7000\r\n")
        IniFile(file).apply {
            set("Display", "iSize W", "1280")
            set("Display", "bSAOEnable", "0")
            set("Water", "bUseWaterReflections", "0")
            save()
        }
        assertEquals(
            "[Display]\r\niSize W=1280\r\niSize H=1080\r\nbSAOEnable=0\r\n\r\n[Grass]\r\nfGrassStartFadeDistance=7000\r\n\r\n[Water]\r\nbUseWaterReflections=0",
            file.readText(),
        )
    }

    @Test
    fun createsMissingFile() {
        val file = File(File.createTempFile("dir", "").apply { delete(); mkdirs(); deleteOnExit() }, "SkyrimPrefs.ini")
        IniFile(file).apply {
            set("Display", "iSize W", "1280")
            save()
        }
        assertEquals("[Display]\r\niSize W=1280", file.readText())
    }
}
