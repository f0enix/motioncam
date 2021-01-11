# About

Motion Cam is a camera application for Android that replaces the entire camera pipeline. It consumes RAW images and uses computational photography to combine them to reduce noise.

You can install it from the [Play Store](https://play.google.com/store/apps/details?id=com.motioncam)

#### Dual exposure

Dual exposure is similar to the feature found in the Google Camera. The two sliders control the exposure compensation and tonemapping.

![GIF](https://user-images.githubusercontent.com/508688/104108822-d32b6600-52bf-11eb-8ebb-fb7966950462.gif)

#### Zero shutter lag burst capture

![GIF](https://user-images.githubusercontent.com/508688/104165122-dd00b680-53f0-11eb-95bf-edd1098910cc.gif)

## Overview

### Camera Preview

Motion Cam uses the GPU to generate a real time preview of the camera from its RAW data. It uses a simplified pipeline to produce an accurate representation of what the final image will look like. This means it is possible to adjust the tonemapping, contrast and colour settings in real time.

### Noise Reduction

The denoising algorithm uses bayer RAW images as input. Motion Cam treats the RAW data as four colour channels (red, blue and two green channels). It starts by creating an optical flow map between a set of images and the reference image utilising [Fast Optical Flow using Dense Inverse Search](https://arxiv.org/abs/1603.03590). Then, each colour channel is transformed into the wavelet domain using a [dual tree wavelet transform](https://en.wikipedia.org/wiki/Complex_wavelet_transform#Dual-tree_complex_wavelet_transform). The wavelet coefficients are fused with the low pass subband acting as a guide to minimize artifacts due to errors in the optical flow map from occlusion or alignment failure.

The amount of noise present in the image is used to determine how many RAW images are merged together. It is estimated from the high frequency subband of the wavelet transform. A well-lit scene may not need much noise reduction whereas a low light scene will have greater noise and require more images.

### Demosaicing

Most modern cameras use a bayer filter. This means the RAW image is subsampled and consists of 25% red, 25% blue and 50% green pixels. There are more green pixels because human vision is most sensitive to green light. The output from the denoising algorithm is demosaiced and colour corrected into an sRGB image. Motion Cam uses the algorithm [Color filter array demosaicking: New method and performance measures by Lu and Tan](https://pdfs.semanticscholar.org/37d2/87334f29698e451282f162cb4bc4f1f352d9.pdf).

### Tonemapping

Motion Cam uses the algorithm [exposure fusion](https://mericam.github.io/exposure_fusion/index.html) for tonemapping. The algorithm blends multiple different exposures to produce an HDR image. Instead of capturing multiple exposures, it artificially generates the overexposed image and uses the original exposure as inputs to the algorithm. The shadows slider in the app controls the overexposed image.

### Sharpening and Detail Enhancement

The details of the image are enhanced and sharpened with the [Guided filter](http://kaiminghe.com/eccv10/). Motion Cam uses two levels to enhance finer and courser details of the output.

## Getting started

### MacOS

Install the following dependencies:

```
brew install cmake llvm python
```

Set the environment variables:

```
export ANDROID_NDK=[Path to Android NDK]
export LLVM_DIR=/usr/local/Cellar/llvm/[Installed LLVM version]
```

Run the ```./setupenv``` script to compile the dependencies needed by the project.

### Ubuntu

Install the following dependencies:

```
apt install git build-essential llvm-dev cmake clang libclang-dev
```

Set the environment variables:

```
export ANDROID_NDK=[Path to Android NDK]
export LLVM_DIR=/usr
```

Run the ```./setupenv``` script to compile the dependencies needed by the project.

## Running the application

After setting up the environment, open the project MotionCam-Android with Android Studio. It should compile and run.

### Generating code

MotionCam uses [Halide](https://github.com/halide/Halide) to generate the code for most of its algorithms. The generators can be found in ```libMotionCam/generators```. If you make any changes to the generator sources, use the script ```generate.sh``` to regenerate them.
