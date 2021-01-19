# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# Paths

set(thirdparty-libs
        ${CMAKE_SOURCE_DIR}/../../libMotionCam/thirdparty)

set(libmotioncam-src
        ${CMAKE_SOURCE_DIR}/../../libMotionCam/libMotionCam)

#
# DNG
#

add_library( # Sets the name of the library.
        dng-sdk

        # Sets the library as a static library.
        STATIC

        # Source files
        ${thirdparty-libs}/dng_sdk/dng/dng_1d_function.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_1d_table.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_abort_sniffer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_area_task.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_bad_pixels.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_bottlenecks.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_camera_profile.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_color_space.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_color_spec.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_date_time.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_exceptions.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_exif.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_file_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_filter_task.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_fingerprint.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_gain_map.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_globals.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_host.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_hue_sat_map.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_ifd.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_image_writer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_iptc.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_jpeg_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_jpeg_memory_source.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_lens_correction.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_linearization_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_lossless_jpeg.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_matrix.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_memory.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_memory_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_misc_opcodes.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_mosaic_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_mutex.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_negative.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_opcode_list.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_opcodes.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_orientation.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_parse_utils.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_pixel_buffer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_point.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_preview.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_pthread.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_rational.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_read_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_rect.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_ref_counted_block.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_reference.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_render.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_resample.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_safe_arithmetic.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_shared.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_simple_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_spline.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_string.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_string_list.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tag_types.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_temperature.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tile_iterator.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tone_curve.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_utils.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_validate.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_xmp.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_xy_coord.cpp)

# Include directories
target_include_directories(
        dng-sdk PUBLIC
        ${thirdparty-libs}/dng_sdk)

#
# Exiv2
#

add_library(libexpat STATIC IMPORTED)
set_target_properties(libexpat PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/expat/lib/libexpat.a)

add_library(exiv2 STATIC IMPORTED)
set_target_properties(exiv2 PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/exiv2/lib/libexiv2.a)

add_library(exiv2-xmp STATIC IMPORTED)
set_target_properties(exiv2-xmp PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/exiv2/lib/libexiv2-xmp.a)

#
# Halide libraries
#

add_library(camera_preview2 STATIC IMPORTED)
set_target_properties(camera_preview2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/camera_preview2.a)

add_library(camera_preview3 STATIC IMPORTED)
set_target_properties(camera_preview3 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/camera_preview3.a)

add_library(camera_preview4 STATIC IMPORTED)
set_target_properties(camera_preview4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/camera_preview4.a)

add_library(hdr_mask STATIC IMPORTED)
set_target_properties(hdr_mask PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/hdr_mask.a)

add_library(hdr_input STATIC IMPORTED)
set_target_properties(hdr_input PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/hdr_input.a)

add_library(generate_edges STATIC IMPORTED)
set_target_properties(generate_edges PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/generate_edges.a)

add_library(measure_image STATIC IMPORTED)
set_target_properties(measure_image PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/measure_image.a)

add_library(deinterleave_raw STATIC IMPORTED)
set_target_properties(deinterleave_raw PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/deinterleave_raw.a)

#

add_library(preview_portrait2 STATIC IMPORTED)
set_target_properties(preview_portrait2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_portrait2.a)

add_library(preview_landscape2 STATIC IMPORTED)
set_target_properties(preview_landscape2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_landscape2.a)

add_library(preview_reverse_portrait2 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_portrait2.a)

add_library(preview_reverse_landscape2 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_landscape2.a)

#

add_library(preview_portrait4 STATIC IMPORTED)
set_target_properties(preview_portrait4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_portrait4.a)

add_library(preview_landscape4 STATIC IMPORTED)
set_target_properties(preview_landscape4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_landscape4.a)

add_library(preview_reverse_portrait4 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_portrait4.a)

add_library(preview_reverse_landscape4 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_landscape4.a)

#

add_library(preview_portrait8 STATIC IMPORTED)
set_target_properties(preview_portrait8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_portrait8.a)

add_library(preview_landscape8 STATIC IMPORTED)
set_target_properties(preview_landscape8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_landscape8.a)

add_library(preview_reverse_portrait8 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_portrait8.a)

add_library(preview_reverse_landscape8 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/preview_reverse_landscape8.a)

#

add_library(postprocess STATIC IMPORTED)
set_target_properties(postprocess PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/postprocess.a)

add_library(forward_transform STATIC IMPORTED)
set_target_properties(forward_transform PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/forward_transform.a)

add_library(fuse_image STATIC IMPORTED)
set_target_properties(fuse_image PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/fuse_image.a)

add_library(inverse_transform STATIC IMPORTED)
set_target_properties(inverse_transform PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/inverse_transform.a)

add_library(halide_runtime STATIC IMPORTED)
set_target_properties(halide_runtime PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/${ANDROID_ABI}/halide_runtime.a)

#
# OpenCV
#

# Dependencies

add_library(tegra-hal STATIC IMPORTED)
set_target_properties(tegra-hal PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/libtegra_hal.a)

add_library(tbb STATIC IMPORTED)
set_target_properties(tbb PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/libtbb.a)

add_library(tiff STATIC IMPORTED)
set_target_properties(tiff PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/liblibtiff.a)

add_library(jpeg STATIC IMPORTED)
set_target_properties(jpeg PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/liblibjpeg-turbo.a)

add_library(png STATIC IMPORTED)
set_target_properties(png PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/liblibpng.a)

add_library(imf STATIC IMPORTED)
set_target_properties(imf PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/libIlmImf.a)

add_library(cpufeatures STATIC IMPORTED)
set_target_properties(cpufeatures PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/libcpufeatures.a)

add_library(webp STATIC IMPORTED)
set_target_properties(webp PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/liblibwebp.a)

add_library(openjp2 STATIC IMPORTED)
set_target_properties(openjp2 PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/liblibopenjp2.a)

add_library(ittnotify STATIC IMPORTED)
set_target_properties(ittnotify PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/thirdparty/libs/${ANDROID_ABI}/libittnotify.a)

# OpenCV libs
add_library(opencv-flann STATIC IMPORTED)
set_target_properties(opencv-flann PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_flann.a)

add_library(opencv-calib3d STATIC IMPORTED)
set_target_properties(opencv-calib3d PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_calib3d.a)

add_library(opencv-features2d STATIC IMPORTED)
set_target_properties(opencv-features2d PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_features2d.a)

add_library(opencv-xfeatures2d STATIC IMPORTED)
set_target_properties(opencv-xfeatures2d PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_xfeatures2d.a)

add_library(opencv-video STATIC IMPORTED)
set_target_properties(opencv-video PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_video.a)

add_library(opencv-imgproc STATIC IMPORTED)
set_target_properties(opencv-imgproc PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_imgproc.a)

add_library(opencv-ximgproc STATIC IMPORTED)
set_target_properties(opencv-ximgproc PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_ximgproc.a)

add_library(opencv-imgcodecs STATIC IMPORTED)
set_target_properties(opencv-imgcodecs PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_imgcodecs.a)

add_library(opencv-photo STATIC IMPORTED)
set_target_properties(opencv-photo PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_photo.a)

add_library(opencv-core STATIC IMPORTED)
set_target_properties(opencv-core PROPERTIES IMPORTED_LOCATION
        ${thirdparty-libs}/opencv/libs/${ANDROID_ABI}/libopencv_core.a)

#
# Processing library
#

add_library( # Sets the name of the library.
        motion-cam

        # Sets the library as a static library.
        STATIC

        # Source files
        ${thirdparty-libs}/json11/json11/json11.cpp
        ${thirdparty-libs}/miniz/miniz.c
        ${thirdparty-libs}/miniz/miniz_zip.c
        ${thirdparty-libs}/miniz/miniz_tinfl.c
        ${thirdparty-libs}/miniz/miniz_tdef.c
        ${libmotioncam-src}/source/CameraProfile.cpp
        ${libmotioncam-src}/source/Color.cpp
        ${libmotioncam-src}/source/ImageOps.cpp
        ${libmotioncam-src}/source/ImageProcessor.cpp
        ${libmotioncam-src}/source/Logger.cpp
        ${libmotioncam-src}/source/Measure.cpp
        ${libmotioncam-src}/source/RawBufferManager.cpp
        ${libmotioncam-src}/source/RawContainer.cpp
        ${libmotioncam-src}/source/Temperature.cpp
        ${libmotioncam-src}/source/Settings.cpp
        ${libmotioncam-src}/source/Util.cpp)

# Include directories
target_include_directories(motion-cam PUBLIC
        ${libmotioncam-src}/include
        ${libmotioncam-src}/halide/${ANDROID_ABI}
        ${thirdparty-libs}/json11
        ${thirdparty-libs}/miniz
        ${thirdparty-libs}/halide/include
        ${thirdparty-libs}/opencl
        ${thirdparty-libs}/queue
        ${thirdparty-libs}/atomic_queue
        ${thirdparty-libs}/exiv2/include
        ${thirdparty-libs}/dng_sdk
        ${thirdparty-libs}/opencv/include
        ${thirdparty-libs}/opencv/thirdparty/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDNG_SUPPORT -DqAndroid -DqDNGThreadSafe")

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
        native-camera

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        src/main/cpp/NativeCamera.cpp
        src/main/cpp/Processor.cpp
        src/main/cpp/JavaUtils.cpp
        src/main/cpp/NativeCameraBridgeListener.cpp
        src/main/cpp/NativeRawPreviewListener.cpp
        src/main/cpp/camera/CaptureSessionManager.cpp
        src/main/cpp/camera/CameraSession.cpp
        src/main/cpp/camera/ClHelper.cpp
        src/main/cpp/camera/NativeClBuffer.cpp
        src/main/cpp/camera/RawImageConsumer.cpp
)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library(log log)
find_library(z z)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries(native-camera
        motion-cam
        dng-sdk

        # Exiv2
        exiv2
        exiv2-xmp
        libexpat

        # Halide libraries
        camera_preview2
        camera_preview3
        camera_preview4
        hdr_mask
        hdr_input
        generate_edges
        measure_image
        deinterleave_raw
        preview_portrait2
        preview_reverse_portrait2
        preview_landscape2
        preview_reverse_landscape2
        preview_portrait4
        preview_reverse_portrait4
        preview_landscape4
        preview_reverse_landscape4
        preview_portrait8
        preview_reverse_portrait8
        preview_landscape8
        preview_reverse_landscape8
        postprocess
        forward_transform
        fuse_image
        inverse_transform
        halide_runtime

        # Thirdparty libraries
        opencv-calib3d
        opencv-xfeatures2d
        opencv-features2d
        opencv-flann
        opencv-imgcodecs
        opencv-video
        opencv-imgproc
        opencv-ximgproc
        opencv-imgcodecs
        opencv-photo
        opencv-core
        ittnotify
        tegra-hal
        tbb
        tiff
        jpeg
        png
        imf
        cpufeatures
        webp
        openjp2

        # Android libraries
        camera2ndk
        android
        mediandk
        jnigraphics
        EGL
        ${log}
        ${z}
        )
