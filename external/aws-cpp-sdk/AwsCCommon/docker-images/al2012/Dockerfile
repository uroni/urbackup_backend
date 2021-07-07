# Stock image build from internal AL2012.3 image on the wiki
FROM 123124136734.dkr.ecr.us-east-1.amazonaws.com/amzn-linux:2012.3

ARG openssl_dir=/opt/openssl

# 3.13.5 is the last version to work with ancient glibc
ENV CMAKE_VERSION=3.13.5
ENV PYTHON_VERSION=3.7.3

# workaround to allow yum to work in a container, stolen shamelessly from
# https://github.com/moby/moby/issues/10180#issuecomment-296977038
RUN set -ex \
    && rpm --rebuilddb \
    && yum update -y \
    && yum install -y tar gcc gcc-c++ git libffi-devel zlib-devel

###############################################################################
# CMake
###############################################################################
RUN set -ex \
    && cd /tmp \
    && curl -LO https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
    && tar xzf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz -C /usr --strip-components 1 \
    && cmake --version

###############################################################################
# OpenSSL
###############################################################################
RUN set -ex \
    && mkdir -p /tmp/build \
    && cd /tmp/build \
    && git clone https://github.com/openssl/openssl.git \
    && pushd openssl \
    && git checkout OpenSSL_1_0_2-stable \
    && ./config -fPIC \
        no-md2 no-rc5 no-rfc3779 no-sctp no-ssl-trace no-zlib no-hw no-mdc2 \
        no-seed no-idea no-camellia no-bf no-dsa no-ssl3 no-capieng \
        no-unit-test no-tests \
        -DSSL_FORBID_ENULL -DOPENSSL_NO_DTLS1 -DOPENSSL_NO_HEARTBEATS \
        --prefix=${openssl_dir} --openssldir=${openssl_dir} \
    && make -j depend \
    && make -j \
    && make install_sw \
    && LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${openssl_dir}/lib ${openssl_dir}/bin/openssl version

###############################################################################
# Python
###############################################################################

RUN set -ex \
    && cd /tmp \
    && curl -LO https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tgz \
    && tar xzf Python-3.7.3.tgz \
    && cd Python-${PYTHON_VERSION} \
    && ./configure --with-openssl=${openssl_dir} \
    && make -sj \
    && make install

###############################################################################
# Elasticurl
###############################################################################


###############################################################################
# Cleanup
###############################################################################
RUN set -ex \
    && yum clean all \
    && rm -rf /tmp/*
