FROM 123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/ubuntu-16.04:x64

ENV NODE_VERSION="10"

###############################################################################
# NodeJS
###############################################################################
RUN set -ex \
    && curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.35.0/install.sh | bash \
    && export NVM_DIR="$HOME/.nvm" \
    && [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh" \
    && command -v nvm \
    && nvm install $NODE_VERSION \
    && ln -s `nvm which $NODE_VERSION` /usr/local/bin/node \
    && ln -s `nvm which $NODE_VERSION` /usr/local/bin/nodejs \
    && ln -s `dirname $(nvm which $NODE_VERSION)`/npm /usr/local/bin/npm \
    && node --version \
    && npm --version

###############################################################################
# Cleanup
###############################################################################
RUN set -ex \
    && cd \
    && apt-get update -qq \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
