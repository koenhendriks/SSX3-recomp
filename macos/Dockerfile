# Cross-compile SSX3-recomp for macOS (arm64 / x86_64) from Linux.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-18 lld-18 llvm-18 cmake ninja-build meson pkg-config git curl ca-certificates \
      python3 python3-venv python3-pip glslc git-lfs libvulkan-dev xz-utils unzip file \
      libsdl2-dev libepoxy-dev libssl-dev build-essential \
    && rm -rf /var/lib/apt/lists/*
RUN for t in clang clang++ ld64.lld llvm-ar llvm-ranlib llvm-nm llvm-otool llvm-install-name-tool llvm-lipo llvm-strip; do \
      ln -sf /usr/bin/$t-18 /usr/local/bin/$t; done
WORKDIR /work
