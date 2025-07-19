FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY socketserver_http.c .
COPY Makefile .

RUN make socketserver

EXPOSE 8080

CMD ["./socketserver"]