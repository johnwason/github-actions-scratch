#!/bin/sh

set -e

mkdir -p src
tar xf source.tar.gz --strip-components=1 -C src

cmake -GNinja \
  ${CMAKE_ARGS} \
  -DBUILD_SHARED_LIBS=ON \
  -DTESSERACT_ENABLE_TESTING=OFF \
  -DTESSERACT_ENABLE_EXAMPLES=OFF \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DPython3_EXECUTABLE=$PYTHON \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF\
  -S src/tesseract_python \
  -B build_dir

cmake --build build_dir --config Release -- -j$CPU_COUNT

$PYTHON -m pip install --no-deps --ignore-installed --no-build-isolation -vvv ./build_dir/python
$PYTHON -m pip install --no-deps --ignore-installed --no-build-isolation -vvv ./src/tesseract_viewer_python
