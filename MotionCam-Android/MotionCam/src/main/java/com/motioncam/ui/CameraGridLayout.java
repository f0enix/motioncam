package com.motioncam.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

public class CameraGridLayout extends FrameLayout {
    Paint mPaint;

    public CameraGridLayout(@NonNull Context context) {
        super(context);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);

        createPaint();
        setWillNotDraw(false);
    }

    public CameraGridLayout(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);

        createPaint();
        setWillNotDraw(false);
    }

    void createPaint() {
        mPaint = new Paint();

        mPaint.setAntiAlias(true);
        mPaint.setStrokeWidth(1);
        mPaint.setStyle(Paint.Style.STROKE);
        mPaint.setColor(Color.argb(96, 255, 255, 255));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int width = getWidth();
        int height = getHeight();

        canvas.drawLine(width/3, 0, width/3, height, mPaint);
        canvas.drawLine(width/3*2, 0, width/3*2, height, mPaint);

        canvas.drawLine(0, height/3, width, height/3, mPaint);
        canvas.drawLine(0, height/3*2, width, height/3*2, mPaint);
    }
}
