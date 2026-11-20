package com.romcloud.rcnative

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File
import java.nio.file.Files

class PortStorageTest {
    @Test
    fun sizeCountsSubfolders() {
        val dir = Files.createTempDirectory("rcn-storage").toFile()
        try {
            File(dir, "game.rcnx").writeBytes(ByteArray(10))
            File(dir, "Users/Player").mkdirs()
            File(dir, "Users/Player/save.dat").writeBytes(ByteArray(5))
            assertEquals(15L, PortStorage.sizeOf(dir))
            assertEquals(10L, PortStorage.sizeOf(File(dir, "game.rcnx")))
            assertEquals(0L, PortStorage.sizeOf(File(dir, "absent")))
        } finally {
            dir.deleteRecursively()
        }
    }

    @Test
    fun totalsByKind() {
        val details = StorageDetails(
            listOf(
                StorageRow(null, mapOf(StorageKind.DRIVERS to 7)),
                StorageRow(null, mapOf(StorageKind.GAME to 100, StorageKind.CODE to 20, StorageKind.SAVES to 3)),
            ),
            free = 50, total = 1000,
        )
        assertEquals(7L, details.sum(StorageKind.DRIVERS))
        assertEquals(123L, details.rows[1].total)
        assertEquals(true, details.known)
    }
}
