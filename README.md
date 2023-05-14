# Willow

The only supported hardware currently is the [ESP BOX](https://github.com/espressif/esp-box).

## Getting Started

Configuring and building Willow for the ESP BOX is a multi-step process.

### Dependencies

We use [tio](https://github.com/tio/tio) as a serial monitor so you will need to install that.

Ubuntu:

```sudo apt-get install tio```

Arch Linux:

```yay -S tio```

Mac (with homebrew):

```brew install tio```

### Set serial port

To do anything involving the serial port you will need to set the ```PORT``` environment variable for all further invocations of ```utils.sh```. 

With recent versions of ```tio``` you can use ```tio -L``` to list available ports. On Linux you can check ```dmesg``` and look for the path of the recently connected ESP BOX. On Linux it's ```/dev/ACM*``` and on Mac it's ```/dev/usbmodem*```.

Examples:

Linux:

```export PORT=/dev/ttyACM0```

Mac:

```export PORT=/dev/cu.usbmodem2101```

### Container
We use Docker for the build container. To build the docker image:

```./utils.sh build-docker```

Once the container has finished building you will need to enter it for all following commands:

```./utils.sh docker```

### Install
Once inside the container install the environment:

```./utils.sh install```

### Config

Start the config process:

```./utils.sh config```

Navigate to "Willow Configuration" to fill in your WiFi SSID, WiFi password, and your Willow server URI (best-effort Tovera hosted example provided).

For Home Assistant you will also need to create a [long lived access token](https://developers.home-assistant.io/docs/auth_api/#:~:text=Long%2Dlived%20access%20tokens%20can,access%20token%20for%20current%20user.) and configure your server address. By default we use ```homeassistant.local``` which should use mDNS to resolve your local Home Assistant instance.

There are also various other configuration options for speaker volume, display brightness, NTP, etc.

Once you've provided those press 'q'. When prompted to save, do that.

### Build

```./utils.sh build```

### Flash

NOTE: On Mac you need to flash from the host, so run the flash command in another terminal with ```PORT``` defined.

For out of the box/factory new ESP BOX hardware you will (one time) need to erase the factory flash before flashing Willow:

```./utils.sh erase-flash```

Once you have done that you can flash:

```./utils.sh flash```

It should build, flash, and connect you to the serial monitor.

### Let's talk!

If you have made it this far - congratulations! You will see serial monitor output ending like this:

```
I (10414) AFE_SR: afe interface for speech recognition

I (10424) AFE_SR: AFE version: SR_V220727

I (10424) AFE_SR: Initial auido front-end, total channel: 3, mic num: 2, ref num: 1

I (10434) AFE_SR: aec_init: 1, se_init: 1, vad_init: 1

I (10434) AFE_SR: wakenet_init: 1

MC Quantized wakenet9: wakeNet9_v1h24_hiesp_3_0.63_0.635, tigger:v3, mode:2, p:0, (May  5 2023 20:32:52)
I (10704) AFE_SR: wake num: 3, mode: 1, (May  5 2023 20:32:52)

I (13:26:42.433) AUDIO_THREAD: The feed_task task allocate stack on external memory
I (13:26:42.434) AUDIO_THREAD: The fetch_task task allocate stack on external memory
I (13:26:42.442) AUDIO_THREAD: The recorder_task task allocate stack on external memory
I (13:26:42.451) WILLOW: app_main() - start_rec() finished
I (13:26:42.457) AUDIO_THREAD: The at_read task allocate stack on external memory
I (13:26:42.466) WILLOW: esp_netif_get_nr_of_ifs: 1
I (13:26:42.471) WILLOW: Startup complete. Waiting for wake word.
```

Your ESP BOX will initialize. You should see some help text on the display to use your configured wake word. Try some built in Home Assistant [intents](https://www.home-assistant.io/integrations/conversation/) like:

- "(Your wake word) Turn on master bedroom lights"
- "(Your wake work) Turn off kitchen lights"

The available commands and specific names, etc will depend on your Home Assistant configuration.

You can also provide free-form text to get an idea of the accuracy and speed provided by our inference server implementation. The commands will fail unless you've defined them in Home Assistant but the display will show the speech recognition results.

### Things went sideways - reset
In the event your environment gets out of whack we have a helper to reset:

```./utils.sh destroy```

As the plentiful messages indicate it's a very destructive process but it will reset your environment. After it completes you can start from the top and try again.

## Exit serial monitor
To exit ```tio``` you need to press CTRL+t and then 'q'.

## Recover from a bad flash

ESP devices are very robust to flashing failures but it can happen! If you end up "bricking" your device you can erase the flash and re-flash:

```./utils.sh erase-flash```

```./utils.sh flash```

## Advanced Usage

```utils.sh``` will attempt to load environment variables from ```.env```. You can define your ```PORT``` here to avoid needing to define it over and over.

The ESP-IDF, ESP-ADF, ESP-SR, LVGL, etc libraries have a plethora of configuration options. DO NOT change anything outside of "Willow Configuration" unless you know what you are doing.

If you want to quickly and easily flash multiple devices or distribute a combined firmware image you can use the ```dist``` arguments to ```utils.sh```:

```./utils.sh dist``` - builds the combined flash image (```willow-dist.bin```)

```./utils.sh flash-dist``` - flashes the combined flash image

This combined firmware image can be used with any ESP flashing tool like the web flasher [ESP Tool](https://espressif.github.io/esptool-js/) so you can send firmware images to your less technical friends!. Just make sure to use offset 0x0 as we include the bootloader.

## Development

Development usually involves a few steps:

1) Code - do your thang!
2) Build
3) Flash

Unless you change the wake word and/or are using local command recognition (Multinet) you can selectively flash the application partition alone. This avoids long flash times with the wakenet and multinet model partition, etc:

```./utils.sh build```

```./utils.sh flash-app```

## The Future (in no particular order)

### Performance improvements
Willow and air-infer-api already provide "faster-than-Alexa" responsiveness for a voice user interface. However, there are multiple obvious optimizations that could be made:

- ADF pipeline handing (we're waiting on ESP-ADF 2.6 with ESP-IDF 5)
- Websockets for inference server
- Websockets for Home Assistant
- Likely many more

These enchancements alone should dramatically improve responsiveness.

### No CUDA
The air-infer-api inference server (open source release soon) will run CPU only but the performance is not comparable to heavily optimized implementations like [whisper.cpp](https://github.com/ggerganov/whisper.cpp). For an Alexa/Echo competitive voice interface we currently believe that our implementation with CUDA is the best approach. However, we also understand that isn't practical for many users. Between on device Multinet and further development on CPU-only Whisper implementations we will get there.

### TTS Output
Given the capabilities of Whisper speech commands like "What is the weather in Sofia, Bulgaria?" are certainly possible. AIA (air-infer-api) has a text to speech engine and Home Assistant has a variety of options as well. In the event the final response to a given command results in audio output we can play that via the speakers in the ESP BOX.

### Audio Output
The ESP BOX supports bluetooth. In applications where higher quality audio is desired (music streaming, etc) we could support pairing to bluetooth speaker devices. Who knows? Eventually we may even design our own device...

### LCD and Touchscreen Improvements
The ESP BOX has a multi-point capacitive touchscreen and support for many GUI elements. We currently only provide basic features like touch screen to wake up, and a Cancel button on command streaming. There's a lot more work to do here!

### Dynamic Configuration
Docker, building, configuring, flashing, etc is a pain. There are several approaches we plan to take to avoid this and ease the barrier to entry for users to get started.

### Multiple Devices
The good news is the far-field wake word recognition and speech recognition performance is excellent. The bad news is if you have multiple devices in proximity they are all likely to wake and process speech simultaneously. We have a few ideas about dealing with this too :).

### Custom Wake Word
Espressif has a [wake word customization service](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html) that allows us (or you) to create custom wake words. We plan to create a "Hi Willow" or similar wake word.

### GPIO
The ESP BOX provides 16 GPIOs to the user. We plan to make these configurable by the user to enable all kinds of interesting maker applications.
