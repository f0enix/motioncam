package com.motioncam.ui;

import android.content.Context;
import android.graphics.Bitmap;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.github.chrisbanes.photoview.PhotoView;
import com.motioncam.R;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.PostProcessSettings;

import java.util.ArrayList;
import java.util.List;

public class PostProcessPreviewAdapter extends
        RecyclerView.Adapter<PostProcessPreviewAdapter.ViewHolder> implements
        AsyncNativeCameraOps.PreviewListener {

    // View holder
    static class ViewHolder extends RecyclerView.ViewHolder {
        private PhotoView mBitmapView;

        ViewHolder(@NonNull View itemView) {
            super(itemView);

            ViewGroup container = itemView.findViewById(R.id.container);
            mBitmapView = new PhotoView(container.getContext());

            LinearLayout.LayoutParams params =
                    new LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.MATCH_PARENT);

            mBitmapView.setLayoutParams(params);
            mBitmapView.setScaleLevels(1, 2, 4);

            mBitmapView.setOnScaleChangeListener((scaleFactor, focusX, focusY) -> {
                if(mBitmapView.getScale() > 1.01f || mBitmapView.getScale() < 0.99f) {
                    mBitmapView.setAllowParentInterceptOnEdge(false);
                }
                else {
                    mBitmapView.setAllowParentInterceptOnEdge(true);
                }
            });

            container.addView(mBitmapView);
        }

        void updateBitmap(Item item) {
            if(item != null) {
                mBitmapView.setImageBitmap(item.preview);
            }
        }

        PhotoView getBitmapView() {
            return mBitmapView;
        }
    }

    private static class Item {
        NativeCameraBuffer buffer;
        Bitmap preview;

        Item(NativeCameraBuffer buffer) {
            this.buffer = buffer;
        }

        @Override
        public boolean equals(Object other)
        {
            if(other instanceof Item)
                return ((Item) other).buffer.timestamp == this.buffer.timestamp;

            return false;
        }

        void setPreview(Bitmap preview) {
            this.preview = preview;
        }
    }

    private LayoutInflater mInflater;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private List<Item> mItems;
//    private Matrix mTransformMatrix;

    PostProcessPreviewAdapter(Context context, AsyncNativeCameraOps asyncNativeCameraOps, List<NativeCameraBuffer> buffers) {
        mInflater = LayoutInflater.from(context);
        mAsyncNativeCameraOps = asyncNativeCameraOps;
//        mTransformMatrix = new Matrix();

        // Create item for each timestamp
        mItems = new ArrayList<>();

        for(NativeCameraBuffer buffer : buffers) {
            mItems.add(new Item(buffer));
        }

        // We have stable ids
        setHasStableIds(true);
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup viewGroup, int viewType) {
        View view = mInflater.inflate(R.layout.preview, viewGroup, false);
        return new PostProcessPreviewAdapter.ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull ViewHolder viewHolder, int index) {
        Item item = mItems.get(index);

        viewHolder.updateBitmap(item);
        viewHolder.getBitmapView().setTag(item.buffer.timestamp);
    }

    @Override
    public void onAttachedToRecyclerView(@NonNull RecyclerView recyclerView) {
        super.onAttachedToRecyclerView(recyclerView);
        recyclerView.setItemAnimator(null);
    }

    void updatePreview(int index, Bitmap bitmap) {
        if(index >= mItems.size())
            return;

        mItems.get(index).preview = bitmap;
    }

    void updatePreview(int index, PostProcessSettings settings, AsyncNativeCameraOps.PreviewSize previewSize) {
        if(index >= mItems.size())
            return;

        mAsyncNativeCameraOps.generatePreview(mItems.get(index).buffer, settings, previewSize, mItems.get(index).preview, this);
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public long getItemId(int position) {
        return mItems.get(position).buffer.timestamp;
    }

    @Override
    public void onPreviewAvailable(NativeCameraBuffer buffer, Bitmap image) {
        for(int i = 0; i < mItems.size(); i++) {
            if (mItems.get(i).buffer.equals(buffer))
            {
                mItems.get(i).setPreview(image);
                notifyItemChanged(i);

                break;
            }
        }
    }

    NativeCameraBuffer getBuffer(int index) {
        if(index >= 0 && index < mItems.size())
            return mItems.get(index).buffer;

        return null;
    }
}
