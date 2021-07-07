FROM ubuntu:16.04

ARG NDK_VER=r15c

ENV LANG="en_US.UTF-8" \
    LANGUAGE="en_US.UTF-8" \
    LC_ALL="en_US.UTF-8" \
    ANDROID_NDK_HOME="/opt/android-ndk" \
    ANDROID_HOME="/opt/android-sdk-linux" \
    ANDROID_NDK_VERSION="${NDK_VER}" \
    CMAKE_VERSION="3.14.0" \
    DOCKER_VERSION="18.03.1" \
    DOCKER_COMPOSE_VERSION="1.21.2"


###############################################################################
# Update apt
###############################################################################
RUN apt-get update -qq

###############################################################################
# Basics
###############################################################################
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y \
    locales \
    # Do Locale gen
    && locale-gen en_US.UTF-8

RUN DEBIAN_FRONTEND=noninteractive apt-get -y install \
    git \
    curl \
    rsync \
    sudo \
    expect \
    # Python
    python \
    python-dev \
    python-pip \
    # Common, useful
    build-essential \
    zip \
    unzip \
    tree \
    clang \
    awscli \
    # For PPAs
    software-properties-common

###############################################################################
# Docker & Docker Compose
###############################################################################
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y \
    apt-transport-https \
    ca-certificates
RUN curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
RUN set -ex \
    && sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" \
    && DEBIAN_FRONTEND=noninteractive apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-cache policy docker-ce \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    docker-ce=${DOCKER_VERSION}~ce-0~ubuntu

RUN set -ex \
    && curl -L https://github.com/docker/compose/releases/download/${DOCKER_COMPOSE_VERSION}/docker-compose-`uname -s`-`uname -m` -o /usr/local/bin/docker-compose \
    && chmod +x /usr/local/bin/docker-compose \
    && docker-compose --version

###############################################################################
# Dependencies to execute Android builds
###############################################################################
RUN dpkg --add-architecture i386
RUN apt-get update -qq
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y openjdk-8-jdk libc6:i386 libstdc++6:i386 libgcc1:i386 libncurses5:i386 libz1:i386

###############################################################################
# Android SDK Manager
###############################################################################
RUN cd /opt \
    && curl -L https://dl.google.com/android/repository/sdk-tools-linux-4333796.zip -o android-sdk-tools.zip \
    && unzip -q android-sdk-tools.zip -d ${ANDROID_HOME} \
    && rm android-sdk-tools.zip

ENV PATH ${PATH}:${ANDROID_HOME}/tools:${ANDROID_HOME}/tools/bin:${ANDROID_HOME}/platform-tools

###############################################################################
# Install SDK packages
###############################################################################
# Accept licenses before installing components
RUN yes | sdkmanager --licenses

# Platform tools
RUN sdkmanager "tools" "platform-tools"

# SDKs
# Please keep these in descending order!
# The `yes` is for accepting all non-standard tool licenses.
# You can get a list of available packages by booting the container and running
# sdkmanager --list
# Please keep all sections in descending order!
RUN yes | sdkmanager \
    "platforms;android-21" \
    "build-tools;28.0.3" \
    "extras;android;m2repository" \
    "extras;google;m2repository" \
    "cmake;3.6.4111459"

###############################################################################
# Gradle
###############################################################################
RUN apt-get -y install gradle \
    && gradle -v

###############################################################################
# Maven
###############################################################################
RUN apt-get purge maven maven2 \
    && apt-get update \
    && apt-get -y install maven \
    && mvn --version

###############################################################################
# Android NDK
###############################################################################
RUN set -ex \
    && mkdir -p /tmp/android-ndk-tmp \
    && cd /tmp/android-ndk-tmp \
    && curl -LO https://dl.google.com/android/repository/android-ndk-${ANDROID_NDK_VERSION}-linux-x86_64.zip \
    && unzip -q android-ndk-${ANDROID_NDK_VERSION}-linux-x86_64.zip \
    && mv ./android-ndk-${ANDROID_NDK_VERSION} ${ANDROID_NDK_HOME}

# add to PATH
ENV PATH ${PATH}:${ANDROID_NDK_HOME}

###############################################################################
# CMake
###############################################################################
RUN set -ex \
    && mkdir -p /tmp/cmake-tmp \
    && cd /tmp/cmake-tmp \
    && curl -LO https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
    && tar xzf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz --strip-components 1 -C /usr \
    && cmake --version

###############################################################################
# OpenSSL
###############################################################################
RUN set -ex \
    && ([ -d /opt/openssl ] && rm -rf /opt/openssl) || true \
    && mkdir -p /tmp/build \
    && cd /tmp/build \
    && git clone --branch OpenSSL_1_1_1-stable --depth 1 https://github.com/openssl/openssl.git \
    && cd openssl \
    && export CROSS_COMPILE=aarch64-linux-android- \
    && export OLD_PATH=$PATH \
    && export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/:$PATH \
    && ./Configure android-arm64 no-asm -fPIC \
        no-md2 no-rc5 no-rfc3779 no-sctp no-ssl-trace no-zlib no-hw no-mdc2 \
        no-seed no-idea no-camellia no-bf no-dsa no-ssl3 no-capieng \
        no-unit-test no-tests \
        -DSSL_FORBID_ENULL -DOPENSSL_NO_DTLS1 -DOPENSSL_NO_HEARTBEATS \
        --prefix=/opt/openssl --openssldir=/opt/openssl \
    && make -j \
    && make install_sw \
    && export PATH=$OLD_PATH \
    && unset CROSS_COMPILE

###############################################################################
# Cleanup
###############################################################################
RUN set -ex \
    && cd \
    && apt-get update -qq \
    && apt-get clean \
    && rm -rf /tmp/*
