@echo off
cmake -B build -DFERRUM_LTO=OFF || exit /b
cmake --build build --config Release --target Ferrum8_VST3 --parallel || exit /b
echo Built: build\Ferrum8_artefacts\Release\VST3\Ferrum 8.vst3
pause
