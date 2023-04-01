# Sallow client for Willow (air-infer-api)

## Getting Started

Run ```./utils.sh setup``` and cross your fingers and toes.

### Config

The only supported hardware currently is the ESP32-S3-Korvo-2. Anything else will take some work - (we'll deal with this later).

For the time being you will need to run ```./utils.sh config``` and navigate to "Sallow Configuration" to fill in your WiFi SSID and password.

Once you've provided those press 'q'. When prompted to save, do that. Ignore my credentials (that's a TO-DO) - but if you get this far and come to ***REMOVED*** let's hang out before you wardrive me!

### Flash

A current TO-DO is to figure out how to handle serial ports dynamically. For the time being you will need to edit the PORT variable in the ```utils.sh``` script to point to wherever the ESP USB to TTY shows up on your system.

Once you have done that, run ```./utils.sh flash```. It should build, flash, and connect you to the serial monitor.

### Run

If you have made it this far - congratulations! You will see output like this:

```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fce3808,len:0x1664
load:0x403c9700,len:0xbb8
load:0x403cc700,len:0x2f74
entry 0x403c9954
I (24) boot: ESP-IDF v4.4.2-155-g23d5a582cb-dirty 2nd stage bootloader
I (25) boot: compile time 21:23:35
I (25) boot: chip revision: 0
I (28) boot.esp32s3: Boot SPI Speed : 80MHz
I (33) boot.esp32s3: SPI Mode       : DIO
I (38) boot.esp32s3: SPI Flash Size : 16MB
I (42) boot: Enabling RNG early entropy source...
I (48) boot: Partition Table:
I (51) boot: ## Label            Usage          Type ST Offset   Length
I (59) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (66) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (74) boot:  2 factory          factory app      00 00 00010000 00200000
I (81) boot: End of partition table
I (85) esp_image: segment 0: paddr=00010020 vaddr=3c0a0020 size=2130ch (135948) map
I (118) esp_image: segment 1: paddr=00031334 vaddr=3fc96580 size=045d8h ( 17880) load
I (122) esp_image: segment 2: paddr=00035914 vaddr=40374000 size=0a704h ( 42756) load
I (133) esp_image: segment 3: paddr=00040020 vaddr=42000020 size=93c70h (605296) map
I (242) esp_image: segment 4: paddr=000d3c98 vaddr=4037e704 size=07e78h ( 32376) load
I (250) esp_image: segment 5: paddr=000dbb18 vaddr=50000000 size=00010h (    16) load
I (257) boot: Loaded app from partition at offset 0x10000
I (258) boot: Disabling RNG early entropy source...
I (271) opi psram: vendor id : 0x0d (AP)
I (271) opi psram: dev id    : 0x02 (generation 3)
I (271) opi psram: density   : 0x03 (64 Mbit)
I (275) opi psram: good-die  : 0x01 (Pass)
I (280) opi psram: Latency   : 0x01 (Fixed)
I (285) opi psram: VCC       : 0x01 (3V)
I (289) opi psram: SRF       : 0x01 (Fast Refresh)
I (295) opi psram: BurstType : 0x01 (Hybrid Wrap)
I (300) opi psram: BurstLen  : 0x01 (32 Byte)
I (305) opi psram: Readlatency  : 0x02 (10 cycles@Fixed)
I (311) opi psram: DriveStrength: 0x00 (1/1)
W (316) PSRAM: DO NOT USE FOR MASS PRODUCTION! Timing parameters will be updated in future IDF version.
I (327) spiram: Found 64MBit SPI RAM device
I (331) spiram: SPI RAM mode: sram 80m
I (335) spiram: PSRAM initialized, cache is in normal (1-core) mode.
I (342) cpu_start: Pro cpu up.
I (346) cpu_start: Starting app cpu, entry point is 0x40375494
0x40375494: call_start_cpu1 at /home/kris/projects/willow/sallow/build/../deps/esp-adf/esp-idf/components/esp_system/port/cpu_start.c:148

I (0) cpu_start: App cpu up.
I (772) spiram: SPI SRAM memory test OK
I (781) cpu_start: Pro cpu start user code
I (781) cpu_start: cpu freq: 240000000
I (781) cpu_start: Application information:
I (784) cpu_start: Project name:     sallow
I (789) cpu_start: App version:      362aafc-dirty
I (794) cpu_start: Compile time:     Mar 31 2023 21:23:24
I (800) cpu_start: ELF file SHA256:  382881ad951c1ac4...
I (806) cpu_start: ESP-IDF:          v4.4.2-155-g23d5a582cb-dirty
I (813) heap_init: Initializing. RAM available for dynamic allocation:
I (820) heap_init: At 3FC9E968 len 0004ADA8 (299 KiB): D/IRAM
I (827) heap_init: At 3FCE9710 len 00005724 (21 KiB): STACK/DRAM
I (833) heap_init: At 3FCF0000 len 00008000 (32 KiB): DRAM
I (839) heap_init: At 600FE000 len 00002000 (8 KiB): RTCRAM
I (846) spiram: Adding pool of 8192K of external SPI memory to heap allocator
I (854) spi_flash: detected chip: gd
I (858) spi_flash: flash io: dio
I (862) sleep: Configure to isolate all GPIO pins in sleep state
I (869) sleep: Enable automatic switching of GPIO sleep configuration
I (876) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
I (886) spiram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (906) SALLOW: [ 1 ] Initialize Button Peripheral & Connect to wifi network
E (2286) wifi:Association refused temporarily, comeback time 1048 mSec
W (4336) PERIPH_WIFI: Wi-Fi disconnected from SSID ***REMOVED***, auto-reconnect enabled, reconnect after 1000 ms
W (5356) PERIPH_WIFI: WiFi Event cb, Unhandle event_base:WIFI_EVENT, event_id:4
W (5386) wifi:<ba-add>idx:0 (ifx:0, b4:fb:e4:80:c9:62), tid:6, ssn:1, winSize:64
I (5906) SALLOW: [ 1 ] Start codec chip
W (5926) I2C_BUS: i2c_bus_create:58: I2C bus has been already created, [port:0]
W (5946) ES7210: Enable TDM mode. ES7210_SDP_INTERFACE2_REG12: 2
I (5956) SALLOW: [2.0] Create audio pipeline for playback
I (5956) SALLOW: [2.1] Create http stream to read data
I (5966) SALLOW: [2.2] Create wav decoder to decode wav file
I (5966) SALLOW: [2.3] Create i2s stream to write data to codec chip
W (5976) I2S: APLL not supported on current chip, use I2S_CLK_D2CLK as default clock source
I (5986) SALLOW: [2.4] Register all elements to audio pipeline
I (5986) SALLOW: [2.5] Link it together http_stream-->wav_decoder-->i2s_stream-->[codec_chip]
I (6026) SALLOW: [2.6] Set up  uri (http as http_stream, wav as wav_decoder, and default output is i2s)
I (6026) SALLOW: [3.0] Create audio pipeline for recording
I (6036) SALLOW: [3.1] Create http stream to post data to server
I (6046) SALLOW: [3.2] Create i2s stream to read audio data from codec chip
E (6046) I2S: register I2S object to platform failed
I (6056) SALLOW: [3.3] Register all elements to audio pipeline
I (6066) SALLOW: [3.4] Link it together [codec_chip]-->i2s_stream->http_stream-->[http_server]
W (6076) I2S: APLL not supported on current chip, use I2S_CLK_D2CLK as default clock source
W (6076) I2S: APLL not supported on current chip, use I2S_CLK_D2CLK as default clock source
I (6086) SALLOW: [ 5 ] Press [Rec] button to record, Press [Mode] to exit
W (10586) wifi:<ba-add>idx:1 (ifx:0, b4:fb:e4:80:c9:62), tid:0, ssn:0, winSize:64
```

Ignore most of the warnings/errors, especially PSRAM - that's one of my (least) favorite aspects of ESP-IDF work...

At this point you can press and hold the record button (furthest to the right). When you release, and if you said anything and have a speaker connected, you will hear the T5 voice repeat whatever you said.


## TO-DO

- We currently don't use the [AFE](https://www.espressif.com/en/solutions/audio-solutions/esp-afe) interface. There's no automatic gain control, acoustic echo cancelation, etc so Whisper isn't doing as well as it should. There are also probably acoustic issues with the out of the box mics on the dev kit.
- There are some strange buffering/timing issues. You will notice audio clipping depending on timing with button presses. I've tweaked quite a few fundamental things in the SDK to improve this (and the S3 is a big help) but we will need to look at this further.
- ESP-ADF audio [pipelines](https://espressif-docs.readthedocs-hosted.com/projects/esp-adf/en/latest/api-reference/framework/audio_pipeline.html) are somewhat difficult to manage. We're currently POSTing (stream at least) raw WAV frames to the air-infer-api endpoint with some hints on sample rate, etc. We then come back from the ESP to the air-infer-api with a separate HTTP GET playback pipeline to fetch a static file. Ideally we could return a formatted version of the TTS response in the body and play that directly. The current approach adds unnecessary latency and is completely unsuitable for production use. The use of [raw streams](https://espressif-docs.readthedocs-hosted.com/projects/esp-adf/en/latest/api-reference/streams/index.html#raw-stream) is potentially appropriate here. Initial approach is probably to use PSRAM ```malloc()``` to buffer audio response OR, given the flexibility of pipelines we may be able to directly stream the response from the POST to i2s output to avoid memory allocation and potential buffer overruns with longer segments.
- Speaking of pipelines, we currently throw hammers at managing them with forced ```audio_pipeline_wait_for_stop()```, etc which probably adds to our latency and timing issues. At least it doesn't crash :). This could and should be MUCH better.
- Wake word engine, VAD, mDNS, HA integration, LCD (with ESP BOX), etc.
- We definitely will need to be able to parse JSON.
- Much more.
