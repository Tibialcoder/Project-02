@echo off
"C:\\Users\\hiury\\AppData\\Local\\Android\\Sdk\\cmake\\3.22.1\\bin\\cmake.exe" ^
  "-HC:\\Users\\hiury\\OneDrive\\Documentos\\Project 02\\v0.1\\Project-02-v0.1\\app\\src\\main\\cpp" ^
  "-DCMAKE_SYSTEM_NAME=Android" ^
  "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" ^
  "-DCMAKE_SYSTEM_VERSION=24" ^
  "-DANDROID_PLATFORM=android-24" ^
  "-DANDROID_ABI=arm64-v8a" ^
  "-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a" ^
  "-DANDROID_NDK=C:\\Users\\hiury\\AppData\\Local\\Android\\Sdk\\ndk\\25.1.8937393" ^
  "-DCMAKE_ANDROID_NDK=C:\\Users\\hiury\\AppData\\Local\\Android\\Sdk\\ndk\\25.1.8937393" ^
  "-DCMAKE_TOOLCHAIN_FILE=C:\\Users\\hiury\\AppData\\Local\\Android\\Sdk\\ndk\\25.1.8937393\\build\\cmake\\android.toolchain.cmake" ^
  "-DCMAKE_MAKE_PROGRAM=C:\\Users\\hiury\\AppData\\Local\\Android\\Sdk\\cmake\\3.22.1\\bin\\ninja.exe" ^
  "-DCMAKE_CXX_FLAGS=-std=c++17 -O3" ^
  "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=C:\\Users\\hiury\\OneDrive\\Documentos\\Project 02\\v0.1\\Project-02-v0.1\\app\\build\\intermediates\\cxx\\Debug\\23174n15\\obj\\arm64-v8a" ^
  "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=C:\\Users\\hiury\\OneDrive\\Documentos\\Project 02\\v0.1\\Project-02-v0.1\\app\\build\\intermediates\\cxx\\Debug\\23174n15\\obj\\arm64-v8a" ^
  "-DCMAKE_BUILD_TYPE=Debug" ^
  "-BC:\\Users\\hiury\\OneDrive\\Documentos\\Project 02\\v0.1\\Project-02-v0.1\\app\\.cxx\\Debug\\23174n15\\arm64-v8a" ^
  -GNinja ^
  "-DANDROID_STL=c++_shared"
