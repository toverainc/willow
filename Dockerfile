FROM espressif/idf:v4.4.4

ENV DEBIAN_FRONTEND="noninteractive"

RUN apt-get -qq update
RUN apt-get -qq install \
	git \
	libusb-1.0-0 \
	python3 \
	python3-pip \
	python-is-python3

RUN useradd --create-home --uid 1000 build
COPY --chown=1000 container.gitconfig /home/build/.gitconfig

ENV PATH="$PATH:/sallow/.local/bin"
WORKDIR /sallow

ENV ADF_VER="v2.5"
RUN \
  cd /opt/esp/idf && \
  curl https://raw.githubusercontent.com/espressif/esp-adf/$ADF_VER/idf_patches/idf_v4.4_freertos.patch | patch -p1
