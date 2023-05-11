#!/bin/bash
set -e # bail on error

SALLOW_PATH=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd "$SALLOW_PATH"

export PLATFORM="esp32s3" # Current general family
export FLASH_BAUD=2000000 # Optimistic but seems to work for me for now
export CONSOLE_BAUD=2000000 # Subject to change

export DOCKER_IMAGE="sallow:latest"
export DIST_FILE="sallow-dist.bin"

# esptool ver to install
ESPTOOL_VER="4.5.1"

export ADF_PATH="$SALLOW_PATH/deps/esp-adf"

# Number of loops for torture test
TORTURE_LOOPS=300
# Delay in between loops
TORTURE_DELAY=3
# File to play
TORTURE_PLAY="misc/hi_esp_this_is_a_test_command.flac"

# Test for local environment file and use any overrides
if [ -r .env ]; then
    echo "Using configuration overrides from .env file"
    . .env
fi

check_port() {
if [ ! $PORT ]; then
    echo "You need to define the PORT environment variable to do serial stuff - exiting"
    exit 1
fi

if [ ! -c $PORT ]; then
    echo "Cannot find configured port $PORT - exiting"
    exit 1
fi
}

check_esptool() {
    if [ ! -d venv ]; then
        echo "Creating venv for esptool"
        python3 -m venv venv
        source venv/bin/activate
        echo "Installing esptool..."
        pip install -U wheel setuptools pip
        pip install esptool=="$ESPTOOL_VER"
    else
        echo "Using venv for esptool"
        source venv/bin/activate
    fi
}

check_tio() {
    if ! command -v tio &> /dev/null
    then
        echo "tio could not be found in path - you need to install it"
        echo "More information: https://github.com/tio/tio"
        exit 1
    fi
}

fix_term() {
    clear
    reset
}

do_term() {
    tio -b "$CONSOLE_BAUD" "$PORT"
}

check_build_host() {
    if [ "$BUILD_HOST_PATH" ]; then
        echo "Copying build from defined remote build host and path $BUILD_HOST_PATH"
        rm -rf build
        rsync -az --exclude esp-idf "$BUILD_HOST_PATH/build" .
    fi
}

check_container(){
    if [ -f /.dockerenv ]; then
        return
    fi

    if [ "$container" = "podman" ]; then
	    return
    fi

    echo "You need to run this command inside of a container"
    exit 1
}

check_deps() {
    if [ ! -d deps/esp-adf ]; then
        echo "You need to run install first"
        exit 1
    fi
}

do_patch() {
    cd "$SALLOW_PATH"
    cat patches/*.patch | patch -p0
}

# Managed components are... Fun...
fix_components() {
    rm -rf "$SALLOW_PATH"/managed_components/espressif__esp-sr/.component_hash
}

default_speech_commands() {
    if [ ! -r "$SALLOW_PATH"/speech_commands/commands_en.txt ]; then
        echo "Copying default speech commands"
        cp "$SALLOW_PATH"/managed_components/espressif__esp-sr/model/multinet_model/fst/commands_en.txt \
            "$SALLOW_PATH"/speech_commands/commands_en.txt
    fi
}

update_speech_commands() {
    if [ -r "$SALLOW_PATH"/speech_commands/commands_en.txt ]; then
        echo "Linking custom speech commands"
        ln -sf "$SALLOW_PATH"/speech_commands/commands_en.txt \
            "$SALLOW_PATH"/managed_components/espressif__esp-sr/model/multinet_model/fst/commands_en.txt
    fi
}

# Some of this may seem redundant but for build, clean, etc we'll probably need to do our own stuff later
case $1 in

config)
    check_container
    check_deps
    fix_components
    idf.py menuconfig
    update_speech_commands
;;

clean)
    check_container
    check_deps
    fix_components
    idf.py clean
;;

fullclean)
    check_container
    check_deps
    fix_components
    idf.py fullclean
;;

build)
    check_container
    check_deps
    fix_components
    update_speech_commands
    idf.py build
;;

build-docker|docker-build)
    docker build -t "$DOCKER_IMAGE" .
;;

docker)
    docker run --rm -it -v "$PWD":/sallow -e TERM "$DOCKER_IMAGE" /bin/bash
;;

flash)
    check_port
    check_tio
    check_esptool
    check_build_host
    cd "$SALLOW_PATH"/build
    python3 -m esptool --chip "$PLATFORM" -p "$PORT" -b "$FLASH_BAUD" --before default_reset --after hard_reset write_flash \
        @flash_args
    do_term
;;

flash-app)
    check_port
    check_tio
    check_esptool
    check_build_host
    cd "$SALLOW_PATH"/build
    python3 -m esptool --chip "$PLATFORM" -p "$PORT" -b "$FLASH_BAUD" --before=default_reset --after=hard_reset write_flash \
        @flash_app_args
    do_term
;;

dist)
    check_esptool
    check_build_host
    cd "$SALLOW_PATH"/build
    python3 -m esptool --chip "$PLATFORM" merge_bin -o "$SALLOW_PATH/$DIST_FILE" \
        @flash_args
    echo "Combined firmware image for flashing written"
    ls -lh "$SALLOW_PATH/$DIST_FILE"
;;

flash-dist|dist-flash)
    if [ ! -r "$DIST_FILE" ]; then
        echo "You need to run dist first"
        exit 1
    fi
    check_esptool
    check_build_host
    python3 -m esptool --chip "$PLATFORM" -p "$PORT" -b "$FLASH_BAUD" --before=default_reset --after=hard_reset write_flash \
        --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 "$SALLOW_PATH/$DIST_FILE"
    do_term
;;

erase-flash)
    check_esptool
    python3 -m esptool --chip "$PLATFORM" -p "$PORT" erase_flash
    echo "Flash erased. You will need to reflash."
;;

monitor)
    check_port
    check_tio
    do_term
;;

destroy)
    echo "YOU ARE ABOUT TO REMOVE THIS ENTIRE ENVIRONMENT AND RESET THE REPO. HIT ENTER TO CONFIRM."
    read
    echo "SERIOUSLY - YOU WILL LOSE WORK AND I WILL NOT STOP YOU IF YOU HIT ENTER AGAIN!"
    read
    echo "LAST CHANCE!"
    read
    #git reset --hard
    #git clean -fdx
    sudo rm -rf build/*
    sudo rm -rf deps target venv managed_components "$DIST_FILE"
    echo "Not a trace left. You will have to run setup again."
;;

install|setup)
    check_container
    if [ -d deps ]; then
        echo "You already have a deps directory - exiting"
        exit 1
    fi
    mkdir -p deps
    cd deps
    # Setup ADF
    git clone -b "$ADF_VER" https://github.com/espressif/esp-adf.git
    cd $ADF_PATH
    git submodule update --init components/esp-adf-libs

    cd $SALLOW_PATH
    cp sdkconfig.sallow sdkconfig
    idf.py reconfigure
    do_patch

    echo "You can now run ./utils.sh config and navigate to Sallow Configuration for your environment"
;;

speech-commands)
    default_speech_commands
    update_speech_commands
    idf.py clean
    idf.py build
    echo "You can now run ./utils.sh flash to update your speech commands"
;;

torture)
    echo "Running torture test for $TORTURE_LOOPS loops..."
    echo "WARNING: If testing against Tovera provided servers you will get rate-limited or blocked"

    for i in `seq 1 $TORTURE_LOOPS`; do
        echo -n "Running loop $i at" `date +"%H:%M:%S"`
        ffplay -nodisp -hide_banner -loglevel error -autoexit -i "$TORTURE_PLAY"
        sleep $TORTURE_DELAY
    done
;;

*)
    echo "Uknown argument - passing directly to idf.py"
    check_container
    idf.py "$@"
;;

esac
