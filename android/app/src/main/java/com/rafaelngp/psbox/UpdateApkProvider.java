package com.rafaelngp.psbox;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

public class UpdateApkProvider extends ContentProvider {
    public static final String AUTHORITY = "com.rafaelngp.psbox.updateprovider";

    public static Uri getUriForFile(File file) {
        return new Uri.Builder()
                .scheme("content")
                .authority(AUTHORITY)
                .appendPath(file.getName())
                .build();
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    private File resolveApkFile(Uri uri) throws FileNotFoundException {
        Context ctx = getContext();
        if (ctx == null || uri == null) {
            throw new FileNotFoundException("Invalid context or URI");
        }
        String name = uri.getLastPathSegment();
        if (name == null || name.isEmpty() || name.contains("/") || name.contains("\\") || name.contains("..") || !name.endsWith(".apk")) {
            throw new FileNotFoundException("Invalid APK filename in URI: " + uri);
        }
        File[] allowedDirs = new File[] {
            new File(ctx.getFilesDir(), "updates"),
            new File(ctx.getCacheDir(), "updates"),
            ctx.getExternalCacheDir() != null ? new File(ctx.getExternalCacheDir(), "updates") : null,
            ctx.getCacheDir(),
            ctx.getExternalCacheDir()
        };
        for (File dir : allowedDirs) {
            if (dir == null) continue;
            File candidate = new File(dir, name);
            if (candidate.exists() && candidate.isFile()) {
                try {
                    String canonDir = dir.getCanonicalPath();
                    String canonFile = candidate.getCanonicalPath();
                    if (canonFile.startsWith(canonDir + File.separator)) {
                        return candidate;
                    }
                } catch (IOException ignored) {
                }
            }
        }
        throw new FileNotFoundException("Update APK not found for URI: " + uri);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File file;
        try {
            file = resolveApkFile(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        if (projection == null) {
            projection = new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
        }
        String[] cols = new String[projection.length];
        Object[] values = new Object[projection.length];
        int i = 0;
        for (String col : projection) {
            if (OpenableColumns.DISPLAY_NAME.equals(col)) {
                cols[i] = OpenableColumns.DISPLAY_NAME;
                values[i++] = file.getName();
            } else if (OpenableColumns.SIZE.equals(col)) {
                cols[i] = OpenableColumns.SIZE;
                values[i++] = file.length();
            }
        }
        String[] finalCols = new String[i];
        Object[] finalValues = new Object[i];
        System.arraycopy(cols, 0, finalCols, 0, i);
        System.arraycopy(values, 0, finalValues, 0, i);
        MatrixCursor cursor = new MatrixCursor(finalCols, 1);
        cursor.addRow(finalValues);
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        File file = resolveApkFile(uri);
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Read-only provider");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only provider");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only provider");
    }
}
