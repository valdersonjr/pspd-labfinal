FROM openjdk:11-jre-slim

# Instalar dependências
RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    wget \
    procps \
    && rm -rf /var/lib/apt/lists/*

# Instalar Spark
ENV SPARK_VERSION=3.4.1
WORKDIR /opt
RUN wget -q https://archive.apache.org/dist/spark/spark-${SPARK_VERSION}/spark-${SPARK_VERSION}-bin-hadoop3.tgz \
    && tar -xzf spark-${SPARK_VERSION}-bin-hadoop3.tgz \
    && mv spark-${SPARK_VERSION}-bin-hadoop3 spark \
    && rm spark-${SPARK_VERSION}-bin-hadoop3.tgz

ENV SPARK_HOME=/opt/spark
ENV PATH=$PATH:$SPARK_HOME/bin:$SPARK_HOME/sbin
ENV PYSPARK_PYTHON=python3

# Instalar dependências Python
RUN pip3 install pyspark==3.4.1 flask==2.3.3

WORKDIR /app
COPY . .

EXPOSE 8082
CMD ["python3", "spark_distributed_server.py"]
