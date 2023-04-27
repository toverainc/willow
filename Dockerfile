FROM ubuntu:20.04

ENV DEBIAN_FRONTEND="noninteractive"

RUN apt-get -qq update
RUN apt-get -qq install \
	cmake \
	git \
	libusb-1.0-0 \
	python3 \
	python3-pip \
	python-is-python3

RUN useradd --create-home --uid 1000 build

ENV PATH="$PATH:/sallow/.local/bin"
WORKDIR /sallow
