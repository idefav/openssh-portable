FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends build-essential autoconf automake libssl-dev zlib1g-dev python3 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY . /src
WORKDIR /src
RUN autoreconf && ./configure --prefix=/opt/ssh-udp --with-privsep-path=/var/empty --without-pam && make -j4 && make install-nokeys
RUN mkdir -p /var/empty /run/sshd /root/.ssh && useradd -M -s /bin/false sshd && echo 'root:udp-fixture-only' | chpasswd && /opt/ssh-udp/bin/ssh-keygen -A
COPY regress/udp/fixture.py /fixture.py
ENTRYPOINT ["python3", "/fixture.py"]
