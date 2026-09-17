FROM golang:1.22-bookworm AS go
WORKDIR /opt/fecad
COPY artifact/ ./artifact/
COPY claims/ ./claims/
COPY install.sh ./
RUN apt-get update && apt-get install -y --no-install-recommends iproute2 && rm -rf /var/lib/apt/lists/*
RUN ./install.sh
ENV PATH=/opt/fecad/artifact/bin:$PATH
CMD ["bash"]

FROM go AS full
RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake build-essential libomp-dev libntl-dev libgmp-dev git \
 && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --recursive https://github.com/zhouzhangwalker/HE3DB artifact/thirdparty/HE3DB \
 && git clone --depth 1 --recursive https://github.com/Errantry73/Engorgio artifact/thirdparty/Engorgio \
 && ./install.sh --full
CMD ["bash"]
