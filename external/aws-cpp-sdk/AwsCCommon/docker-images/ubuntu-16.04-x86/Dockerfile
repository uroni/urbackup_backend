FROM 123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/ubuntu-16.04:x64

###############################################################################
# 32-bit environment
###############################################################################
RUN dpkg --add-architecture i386
RUN apt-get update -qq

###############################################################################
# OpenSSL
###############################################################################
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y gcc gcc-multilib g++ g++-multilib
RUN set -ex \
    && ([ -d /opt/openssl ] && rm -rf /opt/openssl) || true \
    && mkdir /tmp/build \
    && cd /tmp/build \
    && git clone https://github.com/openssl/openssl.git \
    && pushd openssl \
    && git checkout OpenSSL_1_1_1-stable \
    && setarch i386 ./config -fPIC -m32 \
        no-md2 no-rc5 no-rfc3779 no-sctp no-ssl-trace no-zlib no-hw no-mdc2 \
        no-seed no-idea no-camellia no-bf no-dsa no-ssl3 no-capieng \
        no-unit-test no-tests \
        -DSSL_FORBID_ENULL -DOPENSSL_NO_DTLS1 -DOPENSSL_NO_HEARTBEATS \
        --prefix=/opt/openssl --openssldir=/opt/openssl \
    && make -j \
    && sudo make install_sw \
    && LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/openssl/lib /opt/openssl/bin/openssl version
RUN DEBIAN_FRONTEND=noninteractive apt-get remove -y gcc gcc-multilib g++ g++-multilib \
    && apt autoremove -y

###############################################################################
# Cleanup
###############################################################################
RUN set -ex \
    && apt-get update -qq \
    && apt-get clean \
    && rm -rf /tmp/*
