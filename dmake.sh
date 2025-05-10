#!/bin/bash
mkdir -p build
cd build

TIMESTAMP=$(date +'%Y%m%d-%H%M%S')
LOG_FILE="compile-term-${TIMESTAMP}.log"
CMAKE_PREFIX_PATH="~/Documents/QT/6.9.0/gcc_64"

echo "--- CMake Configure Step ---" > "${LOG_FILE}"
cmake .. -D CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} -D CMAKE_BUILD_TYPE=Debug >> "${LOG_FILE}" 2>&1

if [ $? -ne 0 ]; then
    echo "CMake configure failed, check ${LOG_FILE}!"
    exit 1
fi

echo "" >> "${LOG_FILE}"
echo "--- CMake Build Step ---" >> "${LOG_FILE}"
cmake --build . --parallel 8 >> "${LOG_FILE}" 2>&1

if [ $? -ne 0 ]; then
   echo "CMake build failed, check ${LOG_FILE}!"
    exit 1
fi

echo "Build Succeeded with record ${LOG_FILE}!"
