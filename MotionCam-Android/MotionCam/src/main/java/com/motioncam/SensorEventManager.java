package com.motioncam;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;

import com.motioncam.camera.NativeCameraBuffer;

import org.apache.commons.math3.stat.descriptive.DescriptiveStatistics;

public class SensorEventManager implements SensorEventListener {
    private static final int WINDOW_SIZE = 16;

    private SensorManager mSensorManager;

    private final float[] mAccelerometerReading = new float[3];
    private final float[] mMagnetometerReading = new float[3];
    private final float[] mRotationMatrix = new float[9];
    private final float[] mOrientationAngles = new float[3];

    private DescriptiveStatistics mOrientationPitch = new DescriptiveStatistics(WINDOW_SIZE);
    private DescriptiveStatistics mOrientationRoll = new DescriptiveStatistics(WINDOW_SIZE);

    private NativeCameraBuffer.ScreenOrientation mOrientation;
    private SensorEventHandler mListener;

    interface SensorEventHandler {
        void onOrientationChanged(NativeCameraBuffer.ScreenOrientation orientation);
    }

    SensorEventManager(Context context, SensorEventHandler listener) {
        mSensorManager = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
        mListener = listener;
    }

    void enable() {
        // Set up sensor listeners
        if(mSensorManager != null) {
            Sensor accelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            if (accelerometer != null) {
                mSensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_UI, SensorManager.SENSOR_DELAY_UI);
            }

            Sensor magneticField = mSensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
            if (magneticField != null) {
                mSensorManager.registerListener(this, magneticField, SensorManager.SENSOR_DELAY_UI, SensorManager.SENSOR_DELAY_UI);
            }
        }
    }

    void disable() {
        if(mSensorManager != null)
            mSensorManager.unregisterListener(this);
    }

    private void detectOrientation(SensorEvent event) {
        // Copy sensor data
        if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
            System.arraycopy(event.values, 0, mAccelerometerReading, 0, mAccelerometerReading.length);
        }
        else if (event.sensor.getType() == Sensor.TYPE_MAGNETIC_FIELD) {
            System.arraycopy(event.values, 0, mMagnetometerReading, 0, mMagnetometerReading.length);
        }

        // Update rotation matrix, which is needed to update orientation angles.
        SensorManager.getRotationMatrix(mRotationMatrix, null, mAccelerometerReading, mMagnetometerReading);

        // Get device orientation
        SensorManager.getOrientation(mRotationMatrix, mOrientationAngles);

        // Basic portrait/landscape detection
        int pitch = (int) Math.round(Math.toDegrees(mOrientationAngles[1]));
        int roll = (int) Math.round(Math.toDegrees(mOrientationAngles[2]));

        mOrientationRoll.addValue(roll);
        mOrientationPitch.addValue(pitch);

        if(mOrientationRoll.getN() < WINDOW_SIZE || mOrientationPitch.getN() < WINDOW_SIZE)
            return;

        roll = (int) Math.round(mOrientationRoll.getMean());
        pitch = (int) Math.round(mOrientationPitch.getMean());

        if((mOrientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT || mOrientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT)
                && roll > -30
                && roll < 30 )
        {
            if(pitch > 0)
                setOrientation(NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT);
            else
                setOrientation(NativeCameraBuffer.ScreenOrientation.PORTRAIT);
        }
        else {
            if(Math.abs(pitch) >= 30) {
                if(pitch > 0)
                    setOrientation(NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT);
                else
                    setOrientation(NativeCameraBuffer.ScreenOrientation.PORTRAIT);
            }
            else {
                if(roll > 0)
                    setOrientation(NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE);
                else
                    setOrientation(NativeCameraBuffer.ScreenOrientation.LANDSCAPE);
            }
        }
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        detectOrientation(event);
    }

    private void setOrientation(NativeCameraBuffer.ScreenOrientation screenOrientation) {
        if(mOrientation == screenOrientation)
            return;

        mOrientation = screenOrientation;
        mListener.onOrientationChanged(mOrientation);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    public NativeCameraBuffer.ScreenOrientation getOrientation() {
        return mOrientation;
    }
}
