#!/bin/bash
set -euxo pipefail

HALIDE_PATH="../../thirdparty/halide"

if [ ! -d ${HALIDE_PATH} ] 
then
    echo "Halide is missing. Run setupenv.sh first"
    exit 1
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
	export DYLD_LIBRARY_PATH=${HALIDE_PATH}/lib
else
	export LD_LIBRARY_PATH=${HALIDE_PATH}/lib
fi

rm -rf tmp
mkdir -p tmp

g++ DenoiseGenerator.cpp ${HALIDE_PATH}/share/tools/GenGen.cpp -v -g -o3 -std=c++17 -I ${HALIDE_PATH}/include -L ${HALIDE_PATH}/lib -lHalide -lpthread -ldl -o ./tmp/denoise_generator
g++ PostProcessGenerator.cpp ${HALIDE_PATH}/share/tools/GenGen.cpp -v -g -o3 -std=c++17 -Wall -I ${HALIDE_PATH}/include -L ${HALIDE_PATH}/lib -lHalide -lpthread -ldl -o ./tmp/postprocess_generator
g++ CameraPreviewGenerator.cpp ${HALIDE_PATH}/share/tools/GenGen.cpp -v -g -o3 -std=c++17 -Wall -I ${HALIDE_PATH}/include -L ${HALIDE_PATH}/lib -lHalide -lpthread -ldl -o ./tmp/camera_preview_generator

function build_denoise() {
	TARGET=$1
	ARCH=$2
	FLAGS="no_runtime"

	echo "[$ARCH] Building denoise_generator"
	./tmp/denoise_generator -g denoise_generator -f fuse_denoise -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} input0.type=uint16 input1.type=uint16 pendingOutput.type=float32 output.type=float32

	echo "[$ARCH] Building forward_transform_generator"
	./tmp/denoise_generator -g forward_transform_generator -f forward_transform -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} input.type=uint16 levels=6

	echo "[$ARCH] Building fuse_image_generator"
	./tmp/denoise_generator -g fuse_image_generator -f fuse_image -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} input.type=uint16 reference.size=6 reference.type=float32 intermediate.size=6 intermediate.type=float32

	echo "[$ARCH] Building inverse_transform_generator"
	./tmp/denoise_generator -g inverse_transform_generator -f inverse_transform -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} input.size=6
}

function build_postprocess() {
	TARGET=$1
	ARCH=$2
	FLAGS="no_runtime"

	echo "[$ARCH] Building hdr_mask_generator"
	./tmp/postprocess_generator -g hdr_mask_generator -f hdr_mask -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building linear_image_generator"
	./tmp/postprocess_generator -g linear_image_generator -f linear_image -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building measure_image_generator"
	./tmp/postprocess_generator -g measure_image_generator -f measure_image -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building generate_edges_generator"
	./tmp/postprocess_generator -g generate_edges_generator -f generate_edges -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building deinterleave_raw_generator"
	./tmp/postprocess_generator -g deinterleave_raw_generator -f deinterleave_raw -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building postprocess_generator"
	./tmp/postprocess_generator -g postprocess_generator -f postprocess -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS}

	echo "[$ARCH] Building preview_generator2 rotation=0"
	./tmp/postprocess_generator -g preview_generator -f preview_landscape2 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=0 tonemap_levels=8 downscale_factor=2

	echo "[$ARCH] Building preview_generator2 rotation=90"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_portrait2 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=90 tonemap_levels=8 downscale_factor=2

	echo "[$ARCH] Building preview_generator2 rotation=-90"
	./tmp/postprocess_generator -g preview_generator -f preview_portrait2 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=-90 tonemap_levels=8 downscale_factor=2

	echo "[$ARCH] Building preview_generator2 rotation=180"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_landscape2 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=180 tonemap_levels=8 downscale_factor=2

	echo "[$ARCH] Building preview_generator4 rotation=0"
	./tmp/postprocess_generator -g preview_generator -f preview_landscape4 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=0 tonemap_levels=7 downscale_factor=4

	echo "[$ARCH] Building preview_generator4 rotation=90"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_portrait4 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=90 tonemap_levels=7 downscale_factor=4

	echo "[$ARCH] Building preview_generator4 rotation=-90"
	./tmp/postprocess_generator -g preview_generator -f preview_portrait4 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=-90 tonemap_levels=7 downscale_factor=4

	echo "[$ARCH] Building preview_generator4 rotation=180"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_landscape4 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=180 tonemap_levels=7 downscale_factor=4

	echo "[$ARCH] Building preview_generator8 rotation=0"
	./tmp/postprocess_generator -g preview_generator -f preview_landscape8 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=0 tonemap_levels=6 downscale_factor=8

	echo "[$ARCH] Building preview_generator8 rotation=90"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_portrait8 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=90 tonemap_levels=6 downscale_factor=8

	echo "[$ARCH] Building preview_generator8 rotation=-90"
	./tmp/postprocess_generator -g preview_generator -f preview_portrait8 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=-90 tonemap_levels=6 downscale_factor=8

	echo "[$ARCH] Building preview_generator8 rotation=180"
	./tmp/postprocess_generator -g preview_generator -f preview_reverse_landscape8 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} rotation=180 tonemap_levels=6 downscale_factor=8
}

function build_camera_preview() {
	TARGET=$1
	ARCH=$2
	FLAGS="no_runtime"

	# RAW10
	echo "[$ARCH] Building camera_preview_generator2_raw10"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview2_raw10 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=8 downscale_factor=2 pixel_format=0

	echo "[$ARCH] Building camera_preview_generator3_raw10"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview3_raw10 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=7 downscale_factor=3 pixel_format=0

	echo "[$ARCH] Building camera_preview_generator4_raw10"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview4_raw10 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=6 downscale_factor=4 pixel_format=0

	# RAW16
	echo "[$ARCH] Building camera_preview_generator2_raw16"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview2_raw16 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=8 downscale_factor=2 pixel_format=1

	echo "[$ARCH] Building camera_preview_generator3_raw16"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview3_raw16 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=7 downscale_factor=3 pixel_format=1

	echo "[$ARCH] Building camera_preview_generator4_raw16"
	./tmp/camera_preview_generator -g camera_preview_generator -f camera_preview4_raw16 -e static_library,h -o ../halide/${ARCH} target=${TARGET}-${FLAGS} tonemap_levels=6 downscale_factor=4 pixel_format=1
}

function build_runtime() {
	TARGET=$1
	ARCH=$2

	echo "[$ARCH] Building halide_runtime_base"
	./tmp/camera_preview_generator -r halide_runtime -e static_library,h -o ../halide/${ARCH} target=${TARGET}

	mv ../halide/${ARCH}/halide_runtime.a ../halide/${ARCH}/halide_runtime_host.a

	echo "[$ARCH] Building halide_runtime_opencl"
	./tmp/camera_preview_generator -r halide_runtime -e static_library,h -o ../halide/${ARCH} target=${TARGET}-opencl-cl_half

	mv ../halide/${ARCH}/halide_runtime.a ../halide/${ARCH}/halide_runtime_opencl.a
}

mkdir -p ../halide/host

build_denoise host host
build_postprocess host host
build_camera_preview host host
build_runtime host host

mkdir -p ../halide/arm64-v8a

build_denoise arm-64-android arm64-v8a
build_postprocess arm-64-android arm64-v8a
build_camera_preview arm-64-android-opencl-cl_half arm64-v8a
build_runtime arm-64-android arm64-v8a
