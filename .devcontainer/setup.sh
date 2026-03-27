#!/bin/bash
set -e

echo "==> Installing ARM toolchain and build tools..."
sudo apt-get update -q
sudo apt-get install -y -q \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3 \
    ninja-build \
    pkg-config

echo "==> Cloning Raspberry Pi Pico SDK..."
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
cd /opt/pico-sdk
git submodule update --init --depth 1   # pulls in TinyUSB

echo "==> Setting PICO_SDK_PATH globally..."
echo 'export PICO_SDK_PATH=/opt/pico-sdk' | sudo tee /etc/profile.d/pico.sh
echo 'export PICO_SDK_PATH=/opt/pico-sdk' >> ~/.bashrc
export PICO_SDK_PATH=/opt/pico-sdk

echo "==> Creating build directory..."
cd /workspaces/pico-synth       # Codespaces mounts your repo here
mkdir -p build
cd build
cmake .. -G Ninja

echo ""
echo "✅ Setup complete!"
echo ""
echo "To build your firmware, run:"
echo "  cd build && ninja"
echo ""
echo "The output file will be at:"
echo "  build/pico_synth.uf2"
