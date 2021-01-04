package com.motioncam.ui;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.util.Size;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.motioncam.R;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.PostProcessSettings;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class SmallPreviewListAdapter extends RecyclerView.Adapter<SmallPreviewListAdapter.ViewHolder>
        implements AsyncNativeCameraOps.PreviewListener {

    private static final int PREVIEW_HEIGHT_DP = 80;

    // Selection listener
    interface OnSelectionChangedListner {
        void onSelectionChanged(int index, NativeCameraBuffer buffer, Bitmap itemPreview);
    }

    // View holder
    class ViewHolder extends RecyclerView.ViewHolder {
        ImageView mImageView;
        ViewGroup mFrame;
        TextView mRelativeTimeText;

        ViewHolder(@NonNull View itemView) {
            super(itemView);

            mFrame = itemView.findViewById(R.id.previewFrame);
            mImageView = itemView.findViewById(R.id.preview);
            mRelativeTimeText = itemView.findViewById(R.id.relativeTimeText);
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
    }

    private LayoutInflater mInflater;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private PostProcessSettings mPostProcessSettings;
    private List<Item> mItems;
    private int mSelectedIndex;
    private OnSelectionChangedListner mSelectionListener;

    SmallPreviewListAdapter(Context context,
                           AsyncNativeCameraOps asyncNativeCameraOps,
                           PostProcessSettings settings,
                           List<NativeCameraBuffer> buffers)
    {
        mInflater = LayoutInflater.from(context);
        mAsyncNativeCameraOps = asyncNativeCameraOps;
        mPostProcessSettings = settings;

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
    public SmallPreviewListAdapter.ViewHolder onCreateViewHolder(@NonNull ViewGroup viewGroup, int i) {
        View view = mInflater.inflate(R.layout.preview_list_item, viewGroup, false);
        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull SmallPreviewListAdapter.ViewHolder viewHolder, int index) {
        if(mItems.get(index).preview == null) {
            mAsyncNativeCameraOps.generatePreview(
                    mItems.get(index).buffer, mPostProcessSettings, AsyncNativeCameraOps.PreviewSize.SMALL, null, this);

            viewHolder.mImageView.setImageBitmap(null);
        }
        else {
            viewHolder.mImageView.setImageBitmap(mItems.get(index).preview);
        }

        Size previewSize = mAsyncNativeCameraOps.getPreviewSize(AsyncNativeCameraOps.PreviewSize.SMALL, mItems.get(index).buffer);

        int height = Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, PREVIEW_HEIGHT_DP, Resources.getSystem().getDisplayMetrics()));
        int width = Math.round(height * previewSize.getWidth() / (float) previewSize.getHeight());

        viewHolder.mImageView.setLayoutParams(new LinearLayout.LayoutParams(width, height));

        if(index == mSelectedIndex)
            viewHolder.mFrame.setBackgroundResource(R.drawable.preview_image_border);
        else
            viewHolder.mFrame.setBackground(null);

        long timestampDiffNs = mItems.get(index).buffer.timestamp - mItems.get(0).buffer.timestamp;
        float timestampDiffSeconds = timestampDiffNs / (1000.0f * 1000.0f * 1000.0f);

        viewHolder.mRelativeTimeText.setText(String.format(Locale.getDefault(), "%.2f s", timestampDiffSeconds));

        // Keep track of selected item
        viewHolder.mImageView.setOnClickListener(v -> {
            setSelectedItem(index);

            if(mSelectionListener != null)
                mSelectionListener.onSelectionChanged(mSelectedIndex, mItems.get(mSelectedIndex).buffer, mItems.get(mSelectedIndex).preview);
        });
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    public long getItemId(int position) {
        return mItems.get(position).buffer.timestamp;
    }

    void setSelectedItem(int index) {
        notifyItemChanged(mSelectedIndex);
        mSelectedIndex = index;
        notifyItemChanged(mSelectedIndex);
    }

    void setSelectionListener(OnSelectionChangedListner listener) {
        mSelectionListener = listener;
    }

    @Override
    public void onAttachedToRecyclerView(@NonNull RecyclerView recyclerView) {
        super.onAttachedToRecyclerView(recyclerView);
        recyclerView.setItemAnimator(null);
    }

    @Override
    public void onPreviewAvailable(NativeCameraBuffer buffer, Bitmap image) {
        for(int i = 0; i < mItems.size(); i++) {
            if(mItems.get(i).buffer.equals(buffer))
            {
                mItems.get(i).preview = image;
                notifyItemChanged(i);

                break;
            }
        }
    }
}
