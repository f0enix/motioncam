package com.motioncam.ui;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.RequestBuilder;
import com.bumptech.glide.load.engine.DiskCacheStrategy;
import com.github.chrisbanes.photoview.PhotoView;
import com.motioncam.R;

import java.io.File;
import java.util.ArrayList;

public class CameraCapturePreviewAdapter extends RecyclerView.Adapter<CameraCapturePreviewAdapter.ViewHolder> {
    // View holder
    static class ViewHolder extends RecyclerView.ViewHolder {
        final private PhotoView mBitmapView;

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

            mBitmapView.setOnScaleChangeListener(
                    (scaleFactor, focusX, focusY) ->
                            mBitmapView.setAllowParentInterceptOnEdge(!(mBitmapView.getScale() > 1.01f) && !(mBitmapView.getScale() < 0.99f)));

            container.addView(mBitmapView);
        }
    }

    static private class Item {
        File preview;
        Uri output;

        Item(File preview) {
            this.preview = preview;
            this.output = null;
        }
    }

    private Context mContext;
    private LayoutInflater mInflater;
    private ArrayList<Item> mItems;

    public CameraCapturePreviewAdapter(Context context) {
        mInflater = LayoutInflater.from(context);
        mContext = context;
        mItems = new ArrayList<>();

        setHasStableIds(true);
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup viewGroup, int viewType) {
        View view = mInflater.inflate(R.layout.preview, viewGroup, false);
        return new CameraCapturePreviewAdapter.ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
        Item item = mItems.get(position);
        RequestBuilder<Drawable> glideBuilder = null;

        if(item.output != null) {
            glideBuilder = Glide.with(mContext).load(item.output);
        }
        else if(item.preview != null) {
            glideBuilder = Glide.with(mContext).load(item.preview);
        }

        if(glideBuilder != null) {
            glideBuilder
                    .diskCacheStrategy(DiskCacheStrategy.NONE)
                    .into(holder.mBitmapView);
        }

    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public long getItemId(int position) {
        return mItems.get(position).preview.hashCode();
    }

    public void add(File previewPath) {
        mItems.add(0, new Item(previewPath));
        notifyItemInserted(0);
    }

    public void complete(File internalPath, Uri output) {
        int position = -1;

        for(int i = 0; i < mItems.size(); i++) {
            if(mItems.get(i).preview.getName().endsWith(internalPath.getName())) {
                position = i;
                break;
            }
        }

        if(position >= 0) {
            mItems.get(position).output = output;

            notifyItemChanged(position);
        }
    }

    public Uri getUri(int position)  {
        if(position < 0 || position >= mItems.size())
            return null;

        return mItems.get(position).output;
    }

    public boolean isProcessing(int position) {
        if(position < 0 || position >= mItems.size())
            return false;

        return mItems.get(position).output == null;
    }

    public Uri getOutput(int position) {
        if(position < 0 || position >= mItems.size())
            return null;

        return mItems.get(position).output;
    }
}
