package com.iisacc.dreamscapes;

import android.Manifest;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import org.qtproject.qt.android.bindings.QtActivity;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class DreamscapesActivity extends QtActivity {
    private static final int PHOTO_PERMISSION = 9074;
    private static final ExecutorService photoWorker = Executors.newSingleThreadExecutor();
    private Runnable pendingPhoto;
    private long pendingRequest;
    private static native void photoSaveFinished(long request, String asset, String error);

    public void savePhoto(String path, String mime, long request) {
        runOnUiThread(() -> {
            Runnable save = () -> photoWorker.execute(() -> importPhoto(path, mime, request));
            // Android 10+ lets apps add their own media without reading other photos.
            if (Build.VERSION.SDK_INT <= 28
                    && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                       != PackageManager.PERMISSION_GRANTED) {
                if (pendingPhoto != null) {
                    photoSaveFinished(request, "", "Another photo permission request is in progress.");
                    return;
                }
                pendingPhoto = save;
                pendingRequest = request;
                requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, PHOTO_PERMISSION);
            } else save.run();
        });
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(requestCode, permissions, grants);
        if (requestCode != PHOTO_PERMISSION || pendingPhoto == null) return;
        Runnable save = pendingPhoto;
        long request = pendingRequest;
        pendingPhoto = null;
        if (grants.length > 0 && grants[0] == PackageManager.PERMISSION_GRANTED) save.run();
        else photoSaveFinished(request, "", "Allow Dreamscapes to access storage in system settings, then try again.");
    }

    private void importPhoto(String path, String mime, long request) {
        ContentResolver resolver = getApplicationContext().getContentResolver();
        Uri asset = null;
        try {
            File source = new File(path);
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, source.getName());
            values.put(MediaStore.Images.Media.MIME_TYPE, mime);
            if (Build.VERSION.SDK_INT >= 29) {
                values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/Dreamscapes");
                values.put(MediaStore.Images.Media.IS_PENDING, 1);
            } else {
                File directory = new File(Environment.getExternalStoragePublicDirectory(
                        Environment.DIRECTORY_PICTURES), "Dreamscapes");
                if (!directory.isDirectory() && !directory.mkdirs())
                    throw new IOException("Could not create the photo folder.");
                // Legacy MediaStore needs a unique destination path; never overwrite another image.
                values.put(MediaStore.Images.Media.DATA,
                        new File(directory, UUID.randomUUID() + "-" + source.getName()).getPath());
            }
            asset = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (asset == null) throw new IOException("Could not create a photo library entry.");
            try (FileInputStream input = new FileInputStream(source);
                 OutputStream output = resolver.openOutputStream(asset, "w")) {
                if (output == null) throw new IOException("Could not open the photo library entry.");
                byte[] buffer = new byte[1024 * 1024];
                for (int size; (size = input.read(buffer)) != -1;) output.write(buffer, 0, size);
            }
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues publish = new ContentValues();
                publish.put(MediaStore.Images.Media.IS_PENDING, 0);
                if (resolver.update(asset, publish, null, null) != 1)
                    throw new IOException("Could not publish the saved photo.");
            }
            photoSaveFinished(request, asset.toString(), "");
        } catch (Exception error) {
            if (asset != null) {
                try { resolver.delete(asset, null, null); } catch (Exception ignored) { }
            }
            photoSaveFinished(request, "", "Could not save to Photos: " + error.getLocalizedMessage());
        }
    }
}
