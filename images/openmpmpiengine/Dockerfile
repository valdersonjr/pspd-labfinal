FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc \
    g++ \
    make \
    libopenmpi-dev \
    openmpi-bin \
    libomp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN make

EXPOSE 8081
CMD ["./engine"]