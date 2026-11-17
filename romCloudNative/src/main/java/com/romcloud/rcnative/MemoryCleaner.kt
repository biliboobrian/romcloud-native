package com.romcloud.rcnative

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager

/**
 * Libère la mémoire avant un jeu lourd : demande à Android d'arrêter les processus d'arrière-plan des
 * autres applications (leurs mémoire vive et zone d'échange reviennent au système). Android ne laisse pas
 * arrêter l'application au premier plan ni les services du système ; un jeu comme Clair Obscur, qui prend
 * presque toute la mémoire du téléphone, gagne pourtant beaucoup à ce que les autres applications (réseaux
 * sociaux, navigateur…) ne gardent pas des Go dans la zone d'échange.
 */
object MemoryCleaner {
    data class Result(val availableBefore: Long, val availableAfter: Long, val apps: Int)

    fun available(context: Context): Long {
        val am = context.getSystemService(ActivityManager::class.java)
        val info = ActivityManager.MemoryInfo()
        am.getMemoryInfo(info)
        return info.availMem
    }

    /** À appeler hors du fil de l'interface (quelques secondes). */
    fun clean(context: Context): Result {
        val before = available(context)
        val am = context.getSystemService(ActivityManager::class.java)
        val pm = context.packageManager
        // Applications visibles : celles qui ont une icône de lancement (déclaré dans <queries>), plus les
        // applications installées que le système nous laisse voir.
        val packages = mutableSetOf<String>()
        val launcher = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        @Suppress("DEPRECATION")
        pm.queryIntentActivities(launcher, 0).mapTo(packages) { it.activityInfo.packageName }
        @Suppress("DEPRECATION")
        pm.getInstalledApplications(PackageManager.GET_META_DATA).mapTo(packages) { it.packageName }
        packages.remove(context.packageName)
        var count = 0
        for (name in packages) {
            try {
                am.killBackgroundProcesses(name)
                count++
            } catch (_: SecurityException) {
                // Refusé pour celle-ci : on continue avec les autres.
            }
        }
        // Le système récupère la mémoire peu à peu.
        Thread.sleep(2000)
        return Result(before, available(context), count)
    }
}
