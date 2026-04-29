FROM ubuntu:22.04

RUN apt-get update && apt-get install -y build-essential cmake git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build & install OpenFHE with serialization
WORKDIR /opt
RUN git clone --depth 1 https://github.com/openfheorg/openfhe-development.git
WORKDIR /opt/openfhe-development/build
RUN cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr/local/openfhe \
          -DBUILD_SERIALIZATION=ON \
          .. \
 && make -j"$(nproc)" && make install

RUN echo "/usr/local/openfhe/lib" > /etc/ld.so.conf.d/openfhe.conf && ldconfig

WORKDIR /app
COPY src/ ./
RUN chmod +x td-bootstrap-init

# Compile td-bootstrap
RUN g++ -std=c++17 -O2 \
    -I/usr/local/openfhe/include \
    -I/usr/local/openfhe/include/openfhe \
    -I/usr/local/openfhe/include/openfhe/core \
    -I/usr/local/openfhe/include/openfhe/pke \
    -I/usr/local/openfhe/include/openfhe/binfhe \
    td-bootstrap.cpp \
    -L/usr/local/openfhe/lib \
    -lOPENFHEpke -lOPENFHEcore -lOPENFHEbinfhe -lpthread -ldl \
    -o td-bootstrap

ENTRYPOINT ["sh","-c","./td-bootstrap-init && sleep infinity"]

