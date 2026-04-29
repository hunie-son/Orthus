FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

# Install Build Tools, OpenSSH Client, AND sshpass
RUN apt-get update && apt-get install -y \
    build-essential libssl-dev cmake git openssh-client sshpass \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Source
COPY src/ ./src/
COPY run_bm1.sh ./

# Compile Host
RUN g++ -std=c++17 -O3 -Wno-deprecated-declarations -o host src/host.cpp src/crypto.cpp src/data.cpp src/model.cpp -lcrypto

# Compile TD Binary
RUN g++ -std=c++17 -O3 -Wno-deprecated-declarations -o td-run src/td-run.cpp -lcrypto

RUN chmod +x run_bm1.sh
CMD ["./run_bm1.sh", "iris", "mlp"]
