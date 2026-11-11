package com.romcloud.rcnative

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

class PortStoreTest {
    @Test
    fun portIdKeepsOnlySafeCharacters() {
        assertEquals("NieR_Automata", PortStore.portId("NieR:Automata"))
        assertEquals("demo", PortStore.portId("  demo "))
        assertEquals("______etc", PortStore.portId("../../etc"))
        assertEquals("port", PortStore.portId(""))
        assertEquals(64, PortStore.portId("a".repeat(200)).length)
    }

    @Test
    fun archiveEntriesCannotLeaveThePortFolder() {
        val root = File(System.getProperty("java.io.tmpdir"), "rcn-test-root")
        assertNotNull(PortStore.entryFile(root, "libgame.so"))
        assertNotNull(PortStore.entryFile(root, "data/level1.bin"))
        assertNull(PortStore.entryFile(root, "../outside.so"))
        assertNull(PortStore.entryFile(root, "data/../../outside.so"))
        // Dossier voisin dont le nom commence comme celui du jeu.
        assertNull(PortStore.entryFile(root, "../rcn-test-root-2/libgame.so"))
    }
}
