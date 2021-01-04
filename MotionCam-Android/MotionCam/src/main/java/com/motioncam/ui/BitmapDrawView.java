package com.motioncam.ui;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

import com.motioncam.camera.NativeCameraBuffer;

public class BitmapDrawView extends View {
    private Bitmap mBitmap;
    private RectF mSize;
    private Matrix mTransform;

    public BitmapDrawView(Context context) {
        super(context);
    }

    public BitmapDrawView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    protected void onDraw(Canvas canvas) {
        if(mBitmap != null && mTransform != null) {
            canvas.drawBitmap(mBitmap, mTransform, null);
        }
    }

    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);

        if(mSize == null) {
            mSize = new RectF(0, 0, w, h);
        }
        else {
            mSize.set(0, 0, w, h);
        }

        updateDrawRect();
    }

    public void setBitmap(Bitmap bitmap) {
        mBitmap = bitmap;

        updateDrawRect();
        invalidate();
    }

    private void updateDrawRect() {
        if(mSize == null || mBitmap == null)
            return;

        if(mTransform == null)
            mTransform = new Matrix();

        mTransform.reset();

        mTransform.setPolyToPoly(
                new float[]{
                        0.f, 0.f, // top left
                        mBitmap.getWidth(), 0.f, // top right
                        0.f, mBitmap.getHeight(), // bottom left
                        mBitmap.getWidth(), mBitmap.getHeight(), // bottom right
                }, 0,
                new float[]{
                        mSize.width(), 0.f, // top left
                        mSize.width(), mSize.height(), // top right
                        0.f, 0.f, // bottom left
                        0.f, mSize.height(), // bottom right
                }, 0, 4);
    }
}
