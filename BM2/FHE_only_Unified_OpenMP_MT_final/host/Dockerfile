FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git ca-certificates libomp-dev \
 && rm -rf /var/lib/apt/lists/*

# --- Build OpenFHE ---
WORKDIR /opt
RUN git clone --depth 1 https://github.com/openfheorg/openfhe-development.git
WORKDIR /opt/openfhe-development/build
RUN cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr/local/openfhe \
          -DBUILD_SERIALIZATION=ON \
          .. \
 && make -j"$(nproc)" && make install

RUN echo "/usr/local/openfhe/lib" > /etc/ld.so.conf.d/openfhe.conf && ldconfig
ENV LD_LIBRARY_PATH="/usr/local/openfhe/lib:${LD_LIBRARY_PATH}"
ENV OPENFHE_INC=/usr/local/openfhe/include
ENV OPENFHE_LIB=/usr/local/openfhe/lib

WORKDIR /app

# Copy source
COPY host/host1/ ./host1/
COPY host/host2/ ./host2/
COPY host/run_fhe.sh ./

# Build Host binaries
RUN make -C host1 clean && make -C host1
RUN make -C host2 clean && make -C host2

RUN chmod +x run_fhe.sh
CMD ["./run_fhe.sh", "mnist", "mlp"]
