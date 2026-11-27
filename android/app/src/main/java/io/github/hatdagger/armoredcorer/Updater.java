package io.github.hatdagger.armoredcorer;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * SYSTEM > SETTINGS > UPDATES (docs/ANDROID.md). The game's C side
 * (src/platform/sdl3/update.c) calls these static methods over JNI and
 * reads the state every frame; the work runs on a thread of its own.
 *
 * Every push to main publishes its APK on the "android-latest" GitHub
 * release with a version.txt beside it (build number, commit, date). A
 * build number higher than this app's versionCode is an update. The APK
 * is streamed straight into an Android PackageInstaller session; Android
 * then shows its own "Do you want to update this app?" screen.
 */
public final class Updater {
    static final String BASE =
        "https://github.com/Hatdagger/Armored-CoreR/releases/download/android-latest/";
    static final String ACTION = "io.github.hatdagger.armoredcorer.INSTALL_RESULT";

    /* keep in step with UPDATE_* in update.h */
    public static final int IDLE = 0, CHECKING = 1, UP_TO_DATE = 2, AVAILABLE = 3,
        DOWNLOADING = 4, CONFIRM = 5, FAILED = 6, NEEDS_PERMISSION = 7, NEWER_HERE = 8;

    static volatile int state = IDLE;
    static volatile int progress = 0;          /* 0..100 while downloading */
    static volatile int remoteBuild = 0;
    static volatile String remoteText = "";    /* "abc1234  2026-11-27" */
    static volatile String message = "";
    static volatile boolean receiverOn = false;

    private Updater() {}

    public static int state() { return state; }
    public static int progress() { return progress; }
    public static int remoteBuild() { return remoteBuild; }
    public static String remoteText() { return remoteText; }
    public static String message() { return message; }

    @SuppressWarnings("deprecation")
    public static int localBuild(Activity a) {
        try { return info(a).versionCode; } catch (Exception e) { return 0; }
    }

    public static String localName(Activity a) {
        try { return info(a).versionName; } catch (Exception e) { return ""; }
    }

    private static PackageInfo info(Activity a) throws Exception {
        return a.getPackageManager().getPackageInfo(a.getPackageName(), 0);
    }

    private static boolean busy() {
        final int s = state;
        return s == CHECKING || s == DOWNLOADING;
    }

    /** Reads version.txt from the release. */
    public static void check(final Activity a) {
        if (busy()) return;
        state = CHECKING;
        message = "";
        new Thread(new Runnable() {
            public void run() {
                try {
                    HttpURLConnection c = open(BASE + "version.txt");
                    BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream(), "UTF-8"));
                    final String build = r.readLine(), commit = r.readLine(), date = r.readLine();
                    r.close();
                    c.disconnect();
                    remoteBuild = Integer.parseInt(build.trim());
                    remoteText = (commit != null ? commit.trim() : "") + "  " + (date != null ? date.trim() : "");
                    final int here = localBuild(a);
                    state = remoteBuild > here ? AVAILABLE : remoteBuild < here ? NEWER_HERE : UP_TO_DATE;
                } catch (Exception e) {
                    fail("Could not reach GitHub: " + e.getMessage());
                }
            }
        }, "acr-update-check").start();
    }

    /** Downloads the APK into an install session and asks Android to install it. */
    public static void install(final Activity a) {
        if (busy() || remoteBuild <= localBuild(a)) return;
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            /* Android's "Install unknown apps" switch for this app */
            state = NEEDS_PERMISSION;
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                      Uri.parse("package:" + a.getPackageName()));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                a.startActivity(i);
            } catch (Exception e) {
                fail("Allow \"Install unknown apps\" for Armored-CoreR in Android's settings.");
            }
            return;
        }
        state = DOWNLOADING;
        progress = 0;
        message = "";
        new Thread(new Runnable() {
            public void run() { download(a); }
        }, "acr-update-download").start();
    }

    private static void download(Activity a) {
        PackageInstaller.Session session = null;
        try {
            listen(a);
            HttpURLConnection c = open(BASE + "armored-corer-android.apk");
            final long size = c.getContentLength();
            PackageInstaller pi = a.getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams p =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            p.setAppPackageName(a.getPackageName());
            if (size > 0) p.setSize(size);
            final int id = pi.createSession(p);
            session = pi.openSession(id);
            InputStream in = c.getInputStream();
            OutputStream out = session.openWrite("armored-corer.apk", 0, size > 0 ? size : -1);
            byte[] buf = new byte[1 << 16];
            long done = 0;
            for (int n; (n = in.read(buf)) > 0; ) {
                out.write(buf, 0, n);
                done += n;
                if (size > 0) progress = (int) (done * 100 / size);
            }
            session.fsync(out);
            out.close();
            in.close();
            c.disconnect();
            progress = 100;

            Intent result = new Intent(ACTION).setPackage(a.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) flags |= 0x02000000;   /* FLAG_MUTABLE: the installer adds its answer */
            PendingIntent pend = PendingIntent.getBroadcast(a, id, result, flags);
            state = CONFIRM;
            session.commit(pend.getIntentSender());
            session.close();
        } catch (Exception e) {
            if (session != null) session.abandon();
            fail("Download failed: " + e.getMessage());
        }
    }

    /** The installer's answer: show its confirm screen, or what went wrong. */
    private static void listen(Activity a) {
        if (receiverOn) return;
        BroadcastReceiver r = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ctx, Intent i) {
                final int s = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                if (s == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                    Intent confirm = (Intent) i.getParcelableExtra(Intent.EXTRA_INTENT);
                    if (confirm != null) {
                        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        ctx.startActivity(confirm);
                    }
                    state = CONFIRM;
                } else if (s == PackageInstaller.STATUS_SUCCESS) {
                    state = UP_TO_DATE;   /* Android normally restarts the app first */
                } else if (s == PackageInstaller.STATUS_FAILURE_ABORTED) {
                    state = AVAILABLE;
                    message = "Install cancelled.";
                } else {
                    final String m = i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                    fail("Install failed" + (m != null ? ": " + m : "."));
                }
            }
        };
        IntentFilter f = new IntentFilter(ACTION);
        Context app = a.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) app.registerReceiver(r, f, 4 /* RECEIVER_NOT_EXPORTED */);
        else app.registerReceiver(r, f);
        receiverOn = true;
    }

    private static HttpURLConnection open(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);   /* GitHub sends release files on from another https host */
        c.setRequestProperty("Cache-Control", "no-cache");
        c.setRequestProperty("User-Agent", "Armored-CoreR-updater");
        final int code = c.getResponseCode();
        if (code == 404) throw new Exception("no Android release published yet");
        if (code != 200) throw new Exception("HTTP " + code);
        return c;
    }

    private static void fail(String m) {
        message = m != null ? m : "Something went wrong.";
        state = FAILED;
    }
}
