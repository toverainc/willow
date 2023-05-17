# Hello Willow Users!

Many users across various forums, social media, etc are starting to receive their hardware! I have enabled Github [discussions](https://github.com/toverainc/willow/discussions) to centralize these great conversations - stop by, introduce yourself, and let us know how things are going with Willow! Between Github discussions and issues we can all work together to make sure our early adopters have the best experience possible!

# Willow - A Practical, Open Source, Privacy-Focused Platform for Voice Assistants and other Applications

Willow is an [ESP IDF](https://github.com/espressif/esp-idf) based project primarily targetting the [ESP BOX](https://github.com/espressif/esp-box) hardware from Espressif. Our goal is to provide Amazon Echo/Google Home competitive performance, accuracy, cost, and functionality with [Home Assistant](https://www.home-assistant.io/) and other platforms - 100% open source and completely self-hosted by the user with "ready for the kitchen counter" low cost commercially available hardware.

<img src="https://github.com/espressif/esp-box/blob/master/docs/_static/esp32_s3_box.png" width="400px" />

**FAST** - [Watch the demo](https://www.youtube.com/watch?v=8ETQaLfoImc). Response times faster than Alexa/Echo or Google Home. From end of speech to action completed in 500ms or less.

**ACCURATE** - High wake word accuracy, low false activation, and powered by a server you host and control using Whisper or command recognition solely on the device.

**RELIABLE** - We've tested thousands of cycles of voice commands with a < 1% failure rate. No one likes to repeat themselves!

**FLEXIBLE** - Use a server anywhere or don't use a server at all with command recognition on the device. Have the results go anywhere you want. Integrate with whatever you want. Completely open source so it does what you want, only what you want, and only how you want it. No more annoying extra prompts or sales pitches to upsell you. Supports multiple wake words with more coming soon.

**PRIVATE** - Check the source. Build and flash yourself. Proxy through another server to inspect traffic. Use on your own server. Use only local commands. Use on a network without access to the internet. Dig as deep as you want because you're not going to find anything fishy here!

**PRACTICAL AND NOT UGLY** - Ready to go! Take it out of the box, flash, and put it in your home or office in minutes without getting looks from people wondering what that "thing" is. Install as many as you like.

**CHEAP** - Approximately $50 hardware cost (plus USB-C power supply). Fully assembled. Done.

**LOW POWER** - 100mW power usage.

Current supported features include:

- Wake Word Engine. Say "Hi ESP" or "Alexa" (user configurable) and start talking!
- Voice Activity Detection. When you stop talking it will stop recording and take action.
- Support for Home Assistant! Simply configure Willow with your Home Assistant server address and access token.
- Support for other platforms. As long as your configured endpoint can take an HTTP POST you can do anything with the speech output!
- Good far-field performance. We've tested wake and speech recognition from roughly 25 feet away in challenging environments with good results.
- Good audio quality - Willow provides features such as automatic gain control, noise separation, etc.
- Support for challenging Wi-Fi environments. Willow can (optionally) use audio compression to reduce airtime on 2.4 GHz Wi-Fi in cases where it's very busy.
- LCD and touchscreen. The ESP BOX has color LCD and capacitive mult-point touchscreen. We support them with an initial user interface.
- Completely on device speech command recognition and support for our (soon to be released) open source Whisper-powered inference server (Tovera hosted best-effort example inference server provided). Configure up to 400 commands completely on device or self-host our (coming soon) inference server to transcribe any speech!

All with hardware you can order today from Amazon, Adafruit, The Pi Hut, Mouser, or other preferred vendor for (approximately) $50 USD. Add a USB-C power supply and go!

## Getting Started

Configuring and building Willow for the ESP BOX is a multi-step process. We're working on improving that but for now...

### System Dependencies

We use [tio](https://github.com/tio/tio) as a serial monitor so you will need to install that.

Ubuntu/Debian:

```sudo apt-get install tio```

Arch Linux:

```sudo yay -S tio```

Mac (with homebrew):

```brew install tio```

### Clone this repo

```git clone https://github.com/toverainc/willow.git && cd willow```

### Container

We use Docker (also supports podman) for the build container. To build the container with docker:

```./utils.sh build-docker```

Once the container has finished building you will need to enter it for all following commands:

```./utils.sh docker```

### Install

Once inside the container install the environment:

```./utils.sh install```

### Start Configuration

Start the config process:

```./utils.sh config```

## ESP BOX LITE NOTE: FOR USERS WHO PURCHASED THE ESP BOX LITE
You will need to build for the ESP BOX LITE. From the main menu, select:
Audio HAL ---> Audio Board ---> ESP32-S3-BOX-Lite

Return to main menu and continue.

### Willow Configuration

Navigate to "Willow Configuration" to fill in your Wi-Fi SSID, Wi-Fi password (supports WPA/WPA2/WPA3), and your Willow server URI (best-effort Tovera hosted example provided).

For Home Assistant you will also need to create a [long lived access token](https://developers.home-assistant.io/docs/auth_api/#:~:text=Long%2Dlived%20access%20tokens%20can,access%20token%20for%20current%20user.) and configure your server address. By default we use ```homeassistant.local``` which should use mDNS to resolve your local Home Assistant instance. Put your long lived access token in the text input area. We recommend testing both your Home Assistant server address and token before flashing.

If your Home Assistant server requires TLS make sure to select it.

There are also various other configuration options for speaker volume, display brightness, NTP, etc.

If you want to change the wake word from the default "Hi ESP" you can navigate from the main menu to ```ESP Speech Recognition --> Select wake words --->``` and select Alexa or whichever. NOTE: If changing the wake word *ALWAYS* use the ```wn9``` variants.

Once you've provided those press 'q'. When prompted to save, do that.

### Build and exit container

```./utils.sh build```

When the build completes successfully you can exit the container.

### Connect the ESP BOX

It's getting real now - plug it in!

### Back on the host - set serial port

To do anything involving the serial port you will need to set the ```PORT``` environment variable for all further invocations of ```utils.sh```. 

With recent versions of ```tio``` you can use ```tio -L``` to list available ports. On Linux you can check ```dmesg``` and look for the path of the recently connected ESP BOX (furthest at the bottom, hopefully). On Linux it's ```/dev/ACM*``` and on Mac it's ```/dev/usbmodem*```.

Examples:

Linux:

```export PORT=/dev/ttyACM0```

Mac:

```export PORT=/dev/cu.usbmodem2101```

### Flash

For out of the box/factory new ESP BOX hardware you will need to (one time) erase the factory flash before flashing Willow:

```./utils.sh erase-flash```

Once you have done that you can flash:

```./utils.sh flash```

It should flash and connect you to the serial monitor.

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

You should see some help text on the display to use your configured wake word. Try some built in Home Assistant [intents](https://www.home-assistant.io/integrations/conversation/) like:

- "(Your wake word) Turn on bedroom lights"
- "(Your wake word) Turn off kitchen lights"

The available commands and specific names, etc will depend on your Home Assistant configuration.

You can also provide free-form speech to get an idea of the accuracy and speed provided by our inference server implementation. The commands will fail unless you've defined them in Home Assistant but the display will show the speech recognition results to get your imagination going.

You can now repeat the erase and flash process for as many devices as you want!

### Exit serial monitor
To exit ```tio``` you need to press 'CTRL+t' and then 'q'. Or you can unplug your device and ```tio``` will wait until you reconnect it.

### Start serial monitor
If you want to see what your device is up to you can start the serial monitor anytime:

```./utils.sh monitor```

## Things went sideways - reset
In the event your environment gets out of whack we have a helper to reset:

```./utils.sh destroy```

As the plentiful messages indicate it's a destructive process but it will reset your environment. After it completes you can start from the top and try again.

## Recover from a bad flash

ESP devices are very robust to flashing failures but it can happen! If you end up "bricking" your device you can erase the flash:

```./utils.sh erase-flash```

NOTE: Depending on how tight of a boot loop your device is in you may need to run ```erase-flash``` multiple times to get the timing right. It will eventually "catch" and successfully erase the flash. When it reports successful erase you can flash again:

```./utils.sh flash```

## Advanced Usage

```utils.sh``` will attempt to load environment variables from ```.env```. You can define your ```PORT``` here to avoid needing to define it over and over.

The ESP-IDF, ESP-ADF, ESP-SR, LVGL, etc libraries have a plethora of configuration options. DO NOT change anything outside of "Willow Configuration" (other than wake word) unless you know what you are doing.

If you want to quickly and easily flash multiple devices or distribute a combined firmware image you can use the ```dist``` arguments to ```utils.sh```:

```./utils.sh dist``` - builds the combined flash image (```willow-dist.bin```)

```./utils.sh flash-dist``` - flashes the combined flash image

This combined firmware image can be used with any ESP flashing tool like the web flasher [ESP Tool](https://espressif.github.io/esptool-js/) so you can send firmware images to your less technical friends! Just make sure to erase the flash first and use offset 0x0 with those tools as we include the bootloader.

## Development

Development usually involves a few steps:

1) Code - do your thing!
2) Build
3) Flash

Unless you change the wake word and/or are using local command recognition (Multinet) you can selectively flash only the application partition. This avoids long flash times with the wakenet and multinet model partition, etc:

```./utils.sh build```

```./utils.sh flash-app```

## The Future (in no particular order)

### Multiple Languages
Willow supports UTF characters and our inference server implementation supports all the languages of Whisper. We have some polishing to do here but it is coming very soon. For the interface language on device we're looking for translation help!

### Performance Improvements
Willow and air-infer-api/Multinet already provide "faster-than-Alexa" responsiveness for a voice user interface. However, there are multiple obvious optimizations we're aware of:

- ESP ADF pipeline handing (we're waiting on ESP-ADF 2.6 with ESP-IDF 5)
- Websockets for inference server (avoids TLS handshake and connection establishment for each session)
- Code in general (we're new to ESP IDF and it could use review)
- Various performance-related sdkconfig parameters (again, we're new to ESP IDF)
- Likely many, many more

These enhancements alone should dramatically improve responsiveness.

### No CUDA
The air-infer-api inference server (open source release soon) will run CPU only but the performance on CPU is not comparable to heavily optimized implementations like [whisper.cpp](https://github.com/ggerganov/whisper.cpp). For an Alexa/Echo competitive voice interface we currently believe that our implementation with CUDA or local Multinet (for limited commands) is the best approach. However, we also understand that isn't practical or preferred for many users. Between on device Multinet command recognition and further development on CPU-only Whisper implementations, ROCm, etc we will get there. That said, if you can make the audio streaming API work you can use any speech to text and text to speech implementation you want!

### TTS Output
Given the capabilities of Whisper speech commands like "What is the weather in Sofia, Bulgaria?" are transcribed but need to match a command (like a Home Assistant intent) on the destination. Our inference server implementation has a text to speech engine and Home Assistant has a variety of options as well. In the event the final response to a given command results in audio output we can play that via the speakers in the ESP BOX (not yet supported).

### Higher Quality Audio Output
The ESP BOX supports bluetooth. In applications where higher quality audio is desired (music streaming, etc) we can support pairing to bluetooth speaker devices. Who knows? Eventually we may even design our own device with better internal speakers...

### LCD and Touchscreen Improvements
The ESP BOX has a multi-point capacitive touchscreen and support for many GUI elements. We currently only provide basic features like touch screen to wake up, a little finger cursor thing, and a Cancel button to cancel/interrupt command streaming. There's a lot more work to do here!

### Buttons
The ESP BOX has buttons and who doesn't like configuring buttons to do things?!

### Audio on device
We currently beep once for success and twice for failure. It's not the most annoying beep in the world but it's not exactly pleasant either. We're going to include some pleasant chimes for success and failure as well as some basic status reporting like "Could not connect to server", etc.

###  Easy Start
Docker, building, configuring, flashing, etc is a pain. There are several approaches we plan to take to avoid this and ease the barrier to entry for users to get started.

### Dynamic Configuration
With something like a Willow Home Assistant component and websocket support we can enable all kinds of interesting dynamic configuration updates and tighter overall configurations.

### Over the Air Firmware Updates
ESP IDF and ESP BOX has robust support for over the air firmware (OTA) updates. Down the road we will support them.

### Multiple Devices
The good news is the far-field wake word recognition and speech recognition performance is very good. The bad news is if you have multiple devices in proximity they are all likely to wake and process speech simultaneously. Commands will still work but multiple confirmation/error beeps and hammering your destination command endpoint is less than ideal. We have a few ideas about dealing with this too.

### Custom Wake Word
Espressif has a [wake word customization service](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html) that enables us (and commercial users) to create custom wake words. We plan to create a "Hi Willow" or similar wake word and potentially others depending on input from the community.

### GPIO
The ESP BOX provides 16 GPIOs to the user that are readily accessed from sockets on the rear of the device. We plan to make these configurable by the user to enable all kinds of interesting maker/DIY functions.
