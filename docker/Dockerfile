FROM ubuntu:focal

CMD ["/bin/bash"]

WORKDIR /root

RUN apt-get update && apt-get upgrade -y -o Dpkg::Options::="--force-confold"

RUN DEBIAN_FRONTEND=noninteractive apt-get install -y \
  git \
  unzip \
  wget \
  build-essential \
  cmake \
  llvm \
  lldb \
  clang \
  valgrind \
  libboost-all-dev

RUN update-alternatives --set c++ "/usr/bin/clang++"

COPY park /bin/park


