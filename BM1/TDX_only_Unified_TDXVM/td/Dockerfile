FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential libssl-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY src/ .

# Ensure /data exists
RUN mkdir -p /data

# Compile
RUN g++ -std=c++17 -O3 -o td-init td-init.cpp -lcrypto
RUN g++ -std=c++17 -O3 -o td-run td-run.cpp -lcrypto

# Default CMD (Overridden by docker-compose but good for standalone)
CMD ["sh", "-c", "cp /app/td-init /data/ && cp /app/td-run /data/ && /data/td-init && sleep infinity"]
