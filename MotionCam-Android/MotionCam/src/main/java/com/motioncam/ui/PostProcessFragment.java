package com.motioncam.ui;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.util.Pair;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.viewpager2.widget.ViewPager2;

import com.motioncam.R;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.CameraManualControl;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.NativeCameraInfo;
import com.motioncam.camera.NativeCameraSessionBridge;
import com.motioncam.camera.PostProcessSettings;
import com.motioncam.databinding.PreviewSettingsBinding;
import com.motioncam.model.CameraProfile;
import com.motioncam.model.PostProcessViewModel;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.processor.ProcessorReceiver;
import com.motioncam.processor.ProcessorService;

import java.io.File;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

import static com.motioncam.CameraActivity.TAG;

public class PostProcessFragment extends Fragment implements
        AsyncNativeCameraOps.CaptureImageListener,
        ProcessorReceiver.Receiver, AsyncNativeCameraOps.SharpnessMeasuredListener {

    public static final String ARGUMENT_NATIVE_CAMERA_HANDLE            = "nativeCameraHandle";
    public static final String ARGUMENT_NATIVE_CAMERA_ID                = "nativeCameraId";
    public static final String ARGUMENT_NATIVE_CAMERA_IS_FRONT_FACING   = "isCameraFrontFacing";

    private static final int LOW_QUALITY_PREVIEW_REFRESH_TIME_MS = 100;
    private static final int NUM_BUFFERS_TO_SELECT_SHARPEST_FROM = 4;

    private PostProcessViewModel mViewModel;
    private ViewPager2 mPreviewPager;
    private RecyclerView mSmallPreviewList;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private NativeCameraSessionBridge mNativeCamera;
    private NativeCameraInfo mSelectedCamera;
    private ProcessorReceiver mProgressReceiver;
    private long mPreviousPreviewUpdateTime;

    private ViewPager2.OnPageChangeCallback mPreviewPageChanged = new ViewPager2.OnPageChangeCallback() {
        @Override
        public void onPageSelected(int position) {
            onNewImageSelected(position);
        }
    };

    public static PostProcessFragment newInstance() {
        return new PostProcessFragment();
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {

        return inflater.inflate(R.layout.post_process_fragment, container, false);
    }

    static private String FormatWithLeadingSign(float n) {
        if(n >= 0) {
            return String.format(Locale.US, "+%.2f", n);
        }

        return String.format(Locale.US, "%.2f", n);
    }

    private void setupObservers(PreviewSettingsBinding dataBinding) {
        // Light
        mViewModel.shadows.observe(getViewLifecycleOwner(), (value) -> {
            String displayText = String.format(Locale.US, "%d%%", value);
            dataBinding.shadowsText.setText(displayText);
            setPreviewDirty();
        });

        mViewModel.whitePoint.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.whitePointText.setText(String.format(Locale.US, "%d", value - 50));
            setPreviewDirty();
        });

        mViewModel.contrast.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.contrastText.setText(String.format(Locale.US, "%d%%", value));
            setPreviewDirty();
        });

        mViewModel.blacks.observe(getViewLifecycleOwner(), (value) ->  {
            dataBinding.blacksText.setText(String.format(Locale.US, "%d%%", value));
            setPreviewDirty();
        });

        mViewModel.exposure.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.exposureText.setText(String.format("%s EV", FormatWithLeadingSign(mViewModel.getExposureSetting())));
            setPreviewDirty();
        });

        // Saturation
        mViewModel.saturation.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.saturationText.setText(String.format(Locale.US, "%d", value - 50));
            setPreviewDirty();
        });

        mViewModel.greenSaturation.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.greensSaturationText.setText(String.format(Locale.US, "%d", value - 50));
            setPreviewDirty();
        });

        mViewModel.blueSaturation.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.bluesSaturationText.setText(String.format(Locale.US, "%d", value - 50));
            setPreviewDirty();
        });

        // White balance
        mViewModel.temperature.observe(getViewLifecycleOwner(), (value) ->  {
            dataBinding.temperatureText.setText(String.format(Locale.US, "%dk", mViewModel.getTemperatureSetting()));
            setPreviewDirty();
        });

        mViewModel.tint.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.tintText.setText(String.format(Locale.US, "%d%%", mViewModel.getTintSetting()));
            setPreviewDirty();
        });

        // Detail
        mViewModel.sharpness.observe(getViewLifecycleOwner(), (value) -> dataBinding.sharpnessText.setText(String.format(Locale.US, "%d%%", mViewModel.sharpness.getValue())));

        mViewModel.detail.observe(getViewLifecycleOwner(), (value) -> {
            dataBinding.detailText.setText(String.format(Locale.US, "%d%%", mViewModel.detail.getValue()));
            setPreviewDirty();
        });

        // Noise reduction
        mViewModel.numMergeImages.observe(getViewLifecycleOwner(), (value) -> dataBinding.numMergeImagesText.setText(String.format(Locale.US, "%d", value)));

        mViewModel.spatialDenoiseAggressiveness.observe(getViewLifecycleOwner(), (value) -> {
            PostProcessViewModel.SpatialDenoiseAggressiveness spatialNoiseWeight = PostProcessViewModel.SpatialDenoiseAggressiveness.GetFromOption(value);

            if(spatialNoiseWeight == PostProcessViewModel.SpatialDenoiseAggressiveness.OFF) {
                dataBinding.spatialNoiseText.setText(
                        Objects.requireNonNull(getActivity()).getString(R.string.denoise_off));
            }
            else if(spatialNoiseWeight == PostProcessViewModel.SpatialDenoiseAggressiveness.NORMAL) {
                dataBinding.spatialNoiseText.setText(
                        Objects.requireNonNull(getActivity()).getString(R.string.denoise_normal));
            }
            else {
                dataBinding.spatialNoiseText.setText(
                        Objects.requireNonNull(getActivity()).getString(R.string.denoise_aggressive));
            }
        });

        mPreviousPreviewUpdateTime = 0;
    }

    private void onSettingsEstimated(PostProcessSettings postProcessSettings) {
        List<NativeCameraBuffer> allBuffers = mViewModel.getAvailableImages(mNativeCamera);

        // Flip if front facing
        postProcessSettings.flipped = mSelectedCamera.isFrontFacing;

        // Set adapter for preview pager
        PreviewAdapter pagerAdapter =
                new PreviewAdapter(getContext(),
                        mAsyncNativeCameraOps,
                        allBuffers);

        mPreviewPager.setAdapter(pagerAdapter);

        // Set up adapter for small preview list
        SmallPreviewListAdapter smallPreviewListAdapter =
                new SmallPreviewListAdapter(
                        getContext(),
                        mAsyncNativeCameraOps,
                        postProcessSettings,
                        allBuffers);

        // Monitor changes to small preview list
        smallPreviewListAdapter.setSelectionListener(((index, buffer, previewBitmap) -> {
            pagerAdapter.updatePreview(index, previewBitmap);
            mPreviewPager.setCurrentItem(index, false);
        }));

        mSmallPreviewList.setAdapter(smallPreviewListAdapter);

        // Measure sharpness among the first few images so we can set the sharpest one as the initial selection
        if(!allBuffers.isEmpty()) {
            List<NativeCameraBuffer> buffersToMeasure =
                    allBuffers.subList(0, Math.min(NUM_BUFFERS_TO_SELECT_SHARPEST_FROM, allBuffers.size()));

            mAsyncNativeCameraOps.measureSharpness(buffersToMeasure, this);
        }
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        mViewModel = new ViewModelProvider(this).get(PostProcessViewModel.class);

        mSmallPreviewList = Objects.requireNonNull(getView()).findViewById(R.id.previewList);
        mPreviewPager = getView().findViewById(R.id.previewPager);

        // Set layout manager for preview list
        LinearLayoutManager layoutManager =
                new LinearLayoutManager(getContext(), LinearLayoutManager.HORIZONTAL, false);

        mSmallPreviewList.setLayoutManager(layoutManager);

        // Bind to model
        PreviewSettingsBinding dataBinding = PreviewSettingsBinding.bind(getView().findViewById(R.id.previewSettings));

        dataBinding.setViewModel(mViewModel);
        dataBinding.setLifecycleOwner(this);

        // Set up observers
        setupObservers(dataBinding);

        // If no camera handle passed in, we can't do anything useful
        if(getArguments() == null) {
            return;
        }

        // Create native camera from handle
        long nativeCameraHandle = getArguments()
                .getLong(ARGUMENT_NATIVE_CAMERA_HANDLE, NativeCameraSessionBridge.INVALID_NATIVE_HANDLE);

        String cameraId = getArguments().getString(ARGUMENT_NATIVE_CAMERA_ID);
        boolean cameraFrontFacing = getArguments().getBoolean(ARGUMENT_NATIVE_CAMERA_IS_FRONT_FACING, false);

        mViewModel.isFlipped.setValue(cameraFrontFacing);

        mNativeCamera = new NativeCameraSessionBridge(nativeCameraHandle);
        mSelectedCamera = new NativeCameraInfo(cameraId, cameraFrontFacing, false);

        mNativeCamera.initImageProcessor();

        // Progress receiver for processor
        mProgressReceiver = new ProcessorReceiver(new Handler());

        // Create preview generator
        mAsyncNativeCameraOps = new AsyncNativeCameraOps(mNativeCamera);

        // Handle buttons
        getView().findViewById(R.id.saveBtn).setOnClickListener((view) -> onSaveClicked());
        getView().findViewById(R.id.asShotWhiteBalanceBtn).setOnClickListener((view) -> onAsShotClicked());

        int[] seekBarsIds = new int[] {
                R.id.shadowsSeekBar,
                R.id.whitePointSeekBar,
                R.id.exposureSeekBar,
                R.id.contrastSeekBar,
                R.id.blacksSeekBar,
                R.id.saturationSeekBar,
                R.id.greensSaturationSeekBar,
                R.id.bluesSeekBar,
                R.id.detailSeekBar,
                R.id.temperatureSeekBar,
                R.id.tintSeekBar
        };

        for(int seekBarId : seekBarsIds) {
            getView().findViewById(seekBarId).setOnTouchListener((view, event) -> {
                onTouchedSettingSeekBar(event);
                return false;
            });
        }

        // Monitor page changes on pager
        mPreviewPager.registerOnPageChangeCallback(mPreviewPageChanged);

        // Estimate post process settings
        LiveData<PostProcessSettings> settings =
                mViewModel.estimateSettings(getContext(), mNativeCamera, mSelectedCamera, mAsyncNativeCameraOps);

        settings.observe(getViewLifecycleOwner(), this::onSettingsEstimated);
    }

    private void onTouchedSettingSeekBar(MotionEvent event) {
        if(event.getAction() == MotionEvent.ACTION_UP) {
            updatePreview(AsyncNativeCameraOps.PreviewSize.LARGE);
        }
    }

    private void onAsShotClicked() {
        PostProcessSettings estimatedSettings = mViewModel.getEstimatedSettings();
        if(estimatedSettings != null) {
            mViewModel.setTemperature(estimatedSettings.temperature);
            mViewModel.setTint(estimatedSettings.tint);
        }
    }

    private void setPreviewDirty() {
        long time = System.currentTimeMillis();

        if(time - mPreviousPreviewUpdateTime > LOW_QUALITY_PREVIEW_REFRESH_TIME_MS) {
            updatePreview(AsyncNativeCameraOps.PreviewSize.MEDIUM);
            mPreviousPreviewUpdateTime = time;
        }
    }

    private void updatePreview(AsyncNativeCameraOps.PreviewSize previewSize) {
        PreviewAdapter adapter = (PreviewAdapter) mPreviewPager.getAdapter();
        if(adapter != null) {
            adapter.updatePreview(mPreviewPager.getCurrentItem(), mViewModel.getPostProcessSettings(), previewSize);
        }
    }

//    private void updatePreviewAspectRatio(NativeCameraBuffer.ScreenOrientation screenOrientation) {
//        ConstraintLayout mainLayout = (ConstraintLayout) getView();
//
//        // Update preview pager dimensions ratio if we are changing to a different orientation
//        FrameLayout previewFrame = (FrameLayout) Objects.requireNonNull(mainLayout).getViewById(R.id.previewFrame);
//
//        String setDimensionRatio = ((ConstraintLayout.LayoutParams) previewFrame.getLayoutParams()).dimensionRatio;
//        String newDimensionRatio = "H,4:3";
//
//        if(screenOrientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
//            newDimensionRatio = "H,4:3";
//        }
//        else if(screenOrientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT) {
//            newDimensionRatio = "H,3:4";
//        }
//
//        if(!newDimensionRatio.equals(setDimensionRatio)) {
//            TransitionManager.beginDelayedTransition(mainLayout);
//
//            ConstraintSet constraintSet = new ConstraintSet();
//
//            constraintSet.clone(mainLayout);
//            constraintSet.setDimensionRatio(R.id.previewFrame, newDimensionRatio);
//            constraintSet.applyTo(mainLayout);
//        }
//    }

    private void onSaveClicked() {
        if(mNativeCamera == null) {
            return;
        }

        PreviewAdapter adapter = (PreviewAdapter) mPreviewPager.getAdapter();

        Integer numMergeImages = mViewModel.numMergeImages.getValue();
        if(numMergeImages == null)
            numMergeImages = 3; // Default to 3

        if(adapter != null) {
            // Disable save button and show progress until we have captured the image
            Objects.requireNonNull(getView()).findViewById(R.id.saveBtn).setEnabled(false);

            // Flash preview frame to indicate capture
            getView().findViewById(R.id.previewFrame).setAlpha(0.25f);
            getView().findViewById(R.id.previewFrame)
                    .animate()
                    .alpha(1.0f)
                    .setDuration(250)
                    .start();

            // Save the image async
            NativeCameraBuffer buffer = adapter.getBuffer(mPreviewPager.getCurrentItem());

            if(buffer != null) {
                mAsyncNativeCameraOps.captureImage(
                        buffer.timestamp,
                        numMergeImages,
                        mViewModel.getWriteDng(),
                        mViewModel.getPostProcessSettings(),
                        CameraProfile.generateCaptureFile(getContext()).getPath(),
                        this);
            }
        }
    }

    private void onNewImageSelected(int index) {
        if(mSmallPreviewList.getAdapter() != null) {
            ((SmallPreviewListAdapter) mSmallPreviewList.getAdapter()).setSelectedItem(index);
            mSmallPreviewList.scrollToPosition(index);
        }

        // Update orientation on view
        List<NativeCameraBuffer> images = mViewModel.getAvailableImages(mNativeCamera);

        String isoText = String.valueOf(images.get(index).iso);
        String shutterSpeedText = CameraManualControl.GetClosestShutterSpeed(images.get(index).exposureTime).toString();

        ((TextView) Objects.requireNonNull(getView()).findViewById(R.id.isoText)).setText(isoText);
        ((TextView) getView().findViewById(R.id.shutterSpeedText)).setText(shutterSpeedText);

        updatePreview(AsyncNativeCameraOps.PreviewSize.LARGE);
    }

    @Override
    public void onPause() {
        super.onPause();

        mProgressReceiver.setReceiver(null);
    }

    @Override
    public void onResume() {
        super.onResume();

        // Hide progress bar if it was shown before
        View v = getView();
        if(v != null) {
            v.findViewById(R.id.saveProgressBar).setVisibility(View.INVISIBLE);
        }

        mProgressReceiver.setReceiver(this);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();

        if(mAsyncNativeCameraOps != null)
            mAsyncNativeCameraOps.close();

        if(mNativeCamera != null) {
            mNativeCamera.destroy();
        }
    }

    @Override
    public void onCaptured(long handle) {
        Log.i(TAG, "Image captured!");

        // Restore save button
        Objects.requireNonNull(getView())
                .findViewById(R.id.saveBtn).setEnabled(true);

        // In debug mode we don't want to delete the intermediate file
        //SharedPreferences sharedPrefs = getActivity().getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        //boolean debugMode = sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_DEBUG_MODE, false);

        // Start service to process the image
        Intent intent = new Intent(getActivity(), ProcessorService.class);

        intent.putExtra(ProcessorService.METADATA_PATH_KEY, CameraProfile.getRootOutputPath(getContext()).getPath());
        intent.putExtra(ProcessorService.DELETE_AFTER_PROCESSING_KEY, true);
        intent.putExtra(ProcessorService.RECEIVER_KEY, mProgressReceiver);

        Objects.requireNonNull(getActivity()).startService(intent);
    }

    @Override
    public void onProcessingStarted(File filePath) {
        View v = getView();
        if(v != null) {
            v.findViewById(R.id.saveProgressBar).setVisibility(View.VISIBLE);
            ((ProgressBar) v.findViewById(R.id.saveProgressBar)).setProgress(0);
        }
    }

    @Override
    public void onProcessingProgress(File filePath, int progress) {
        View v = getView();
        if(v != null) {
            v.findViewById(R.id.saveProgressBar).setVisibility(View.VISIBLE);
            ((ProgressBar) v.findViewById(R.id.saveProgressBar)).setProgress(progress);
        }
    }

    @Override
    public void onProcessingCompleted(File file) {
        View v = getView();
        if(v != null) {
            v.findViewById(R.id.saveProgressBar).setVisibility(View.INVISIBLE);
        }
    }

    @Override
    public void onSharpnessMeasured(List<Pair<NativeCameraBuffer, Double>> sharpnessList) {
        if(sharpnessList.isEmpty())
            return;

        // Sort by sharpness metric
        Pair<NativeCameraBuffer, Double> sharpestBuffer = sharpnessList.get(sharpnessList.size() - 1);
        List<NativeCameraBuffer> buffers = mViewModel.getAvailableImages(mNativeCamera);

        int sharpestBufferIndex = 0;

        for(int i = 0; i < buffers.size(); i++) {
            if(buffers.get(i).compareTo(sharpestBuffer.first) == 0) {
                sharpestBufferIndex = i;
                break;
            }
        }

        mPreviewPager.setCurrentItem(sharpestBufferIndex);
    }
}
