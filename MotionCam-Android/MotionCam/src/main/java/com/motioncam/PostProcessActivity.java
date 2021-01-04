package com.motioncam;

import android.content.Intent;
import android.os.Bundle;

import androidx.appcompat.app.AppCompatActivity;

import com.motioncam.camera.NativeCameraSessionBridge;
import com.motioncam.ui.PostProcessFragment;

public class PostProcessActivity extends AppCompatActivity {
    public static final String INTENT_NATIVE_CAMERA_HANDLE          = "nativeCameraHandle";
    public static final String INTENT_NATIVE_CAMERA_ID              = "nativeCameraId";
    public static final String INTENT_NATIVE_CAMERA_FRONT_FACING    = "nativeCameraFrontFacing";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.post_process_activity);

        // Create native camera from handle
        Intent intent = getIntent();

        long nativeCameraHandle = NativeCameraSessionBridge.INVALID_NATIVE_HANDLE;
        String nativeCameraId = null;
        boolean isCameraFrontFacing = false;

        if(intent != null) {
            nativeCameraHandle = intent.getLongExtra(INTENT_NATIVE_CAMERA_HANDLE, NativeCameraSessionBridge.INVALID_NATIVE_HANDLE);
            nativeCameraId = intent.getStringExtra(INTENT_NATIVE_CAMERA_ID);
            isCameraFrontFacing = intent.getBooleanExtra(INTENT_NATIVE_CAMERA_FRONT_FACING, false);
        }
        else {
            Intent cameraIntent = new Intent(this, CameraActivity.class);
            startActivity(cameraIntent);

            finish();
        }

        if (savedInstanceState == null) {
            Bundle args = new Bundle();

            args.putLong(PostProcessFragment.ARGUMENT_NATIVE_CAMERA_HANDLE, nativeCameraHandle);
            args.putString(PostProcessFragment.ARGUMENT_NATIVE_CAMERA_ID, nativeCameraId);
            args.putBoolean(PostProcessFragment.ARGUMENT_NATIVE_CAMERA_IS_FRONT_FACING, isCameraFrontFacing);

            PostProcessFragment fragment = PostProcessFragment.newInstance();
            fragment.setArguments(args);

            getSupportFragmentManager().beginTransaction()
                    .replace(R.id.container, fragment)
                    .commitNow();
        }
    }
}
