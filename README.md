# Record and Upload WAV Files to HTTP Server

- [中文版本](./README_CN.md)
- Basic Example: ![alt text](../../../docs/_static/level_basic.png "Basic Example")


## Example Brief

This example demonstrates the process of using HTTP stream to upload voice data to an HTTP server created by Python and saving it as a WAV file.

After the development board and HTTP server are running correctly, press the [Rec] key on the audio board to record and upload the voice data to the HTTP server via HTTP streaming, and release the key to stop the recording. At the same time, the HTTP server on the PC side writes the received data into the WAV file and name it after the receiving time.

The complete pipeline of this example is as follows:

```c
mic ---> codec_chip ---> i2s_stream ---> http_stream >>>> [Wi-Fi] >>>> http_server ---> wav_file
```


## Environment Setup


#### Hardware Required

This example runs on the boards that are marked with a green checkbox in the [table](../../README.md#compatibility-of-examples-with-espressif-audio-boards). Please remember to select the board in menuconfig as discussed in Section [Configuration](#configuration) below.


## Build and Flash


### Default IDF Branch

This example supports IDF release/v3.3 and later branches. By default, it runs on ADF's built-in branch `$ADF_PATH/esp-idf`.


### Configuration

The default board for this example is `ESP32-Lyrat V4.3`, if you need to run this example on other development boards, select the board in menuconfig, such as `ESP32-Lyrat-Mini V1.1`.

```c
menuconfig > Audio HAL > ESP32-Lyrat-Mini V1.1
```

Configure the Wi-Fi connection information first. Go to `menuconfig> Example Configuration` and fill in the `Wi-Fi SSID` and `Wi-Fi Password`.

```c
menuconfig > Example Configuration > (myssid) WiFi SSID > (myssid) WiFi Password
```

Then, configure the URI of the HTTP server on the PC. Please make sure that the development board and the HTTP server are in the same Wi-Fi local area network (LAN). For example, if the LAN IP address of the HTTP server is `192.168.5.72`, go to `menuconfig> Example Configuration` and configure the URI as `http://192.168.5.72:8000/upload`.

```c
menuconfig > Example Configuration > (http://192.168.5.72:8000/upload) Server URL to send data
```


### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output (replace `PORT` with your board's serial port name):

```
idf.py -p PORT flash monitor
```

To exit the serial monitor, type ``Ctrl-]``.

See [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/index.html) for full steps to configure and build an ESP-IDF project.


## How to Use the Example


### Example Functionality

- Fist of all, run a Python script with python 2.7. The development board and HTTP server should be connected to the same Wi-Fi network.
-The script server.py is in the root directory of this example, and make sure that this directory is writable. Then, run `python server.py`, and the log is as follows:

```c
python2 server.py
Serving HTTP on 192.168.5.72 port 8000
```

- After the example starts running, it will connect to Wi-Fi first. After a successful connection, press the [Rec] key to record, and the recorded voice will be uploaded to the directory of the HTTP server. The file is named after the time and date of uploading, the audio sampling rate, and the number of channels, which are separated by underscores, such as `20210918T070420Z_16000_16_2.wav`.

- The log of the development board is as follows:

```c
I (0) cpu_start: App cpu up.
I (526) heap_init: Initializing. RAM available for dynamic allocation:
I (533) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (539) heap_init: At 3FFB9AD8 len 00026528 (153 KiB): DRAM
I (545) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (552) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (558) heap_init: At 40095C68 len 0000A398 (40 KiB): IRAM
I (564) cpu_start: Pro cpu start user code
I (247) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
I (277) REC_RAW_HTTP: [ 1 ] Initialize Button Peripheral & Connect to wifi network
I (297) wifi:wifi driver task: 3ffc2c98, prio:23, stack:3584, core=0
I (347) wifi:wifi firmware version: 5f8804c
I (347) wifi:config NVS flash: enabled
I (347) wifi:config nano formating: disabled
I (347) wifi:Init dynamic tx buffer num: 32
I (347) wifi:Init data frame dynamic rx buffer num: 32
I (357) wifi:Init management frame dynamic rx buffer num: 32
I (357) wifi:Init management short buffer num: 32
I (367) wifi:Init static rx buffer size: 1600
I (367) wifi:Init static rx buffer num: 10
I (367) wifi:Init dynamic rx buffer num: 32
W (377) phy_init: failed to load RF calibration data (0xffffffff), falling back to full calibration
I (547) phy: phy_version: 4180, cb3948e, Sep 12 2019, 16:39:13, 0, 2
I (567) wifi:mode : sta (94:b9:7e:65:c2:44)
I (1907) wifi:new:<11,0>, old:<1,0>, ap:<255,255>, sta:<11,0>, prof:1
I (2887) wifi:state: init -> auth (b0)
I (2897) wifi:state: auth -> assoc (0)
I (2907) wifi:state: assoc -> run (10)
I (2917) wifi:connected with esp32, aid = 2, channel 11, BW20, bssid = fc:ec:da:b7:11:c7
I (2917) wifi:security type: 4, phy: bgn, rssi: -32
I (2917) wifi:pm start, type: 1

I (2977) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (4777) REC_RAW_HTTP: [ 2 ] Start codec chip
E (4777) gpio: gpio_install_isr_service(412): GPIO isr service already installed
I (4797) REC_RAW_HTTP: [3.0] Create audio pipeline for recording
I (4797) REC_RAW_HTTP: [3.1] Create http stream to post data to server
I (4807) REC_RAW_HTTP: [3.2] Create i2s stream to read audio data from codec chip
I (4817) REC_RAW_HTTP: [3.3] Register all elements to audio pipeline
I (4817) REC_RAW_HTTP: [3.4] Link it together [codec_chip]-->i2s_stream->http_stream-->[http_server]
W (4847) PERIPH_TOUCH: _touch_init
I (4847) REC_RAW_HTTP: [ 4 ] Press [Rec] button to record, Press [Mode] to exit
I (11447) REC_RAW_HTTP: [ * ] [Rec] input key event, resuming pipeline ...
Total bytes written: 141312
I (14937) REC_RAW_HTTP: [ * ] [Rec] key released, stop pipeline ...
W (14937) AUDIO_ELEMENT: IN-[http] AEL_IO_ABORT
W (14937) HTTP_STREAM: No output due to stopping
I (14937) REC_RAW_HTTP: [ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker
E (14957) TRANS_TCP: tcp_poll_read select error 104, errno = Connection reset by peer, fd = 54
I (14957) REC_RAW_HTTP: [ + ] HTTP client HTTP_STREAM_FINISH_REQUEST
W (14967) AUDIO_PIPELINE: Without stop, st:1
W (14967) AUDIO_PIPELINE: Without wait stop, st:1
```

- The log on the HTTP server is as follows:

```c
python2 server.py
Serving HTTP on 192.168.5.72 port 8000
Audio information, sample rates: 16000, bits: 16, channel(s): 2
Total bytes received: 141312
192.168.5.187 - - [18/Sep/2021 15:04:20] "POST /upload HTTP/1.1" 200 -
```

- Finally, press the [Mode] key to exit the example.

```c
W (197635) REC_RAW_HTTP: [ * ] [Set] input key event, exit the demo ...
I (197635) REC_RAW_HTTP: [ 5 ] Stop audio_pipeline
W (197635) AUDIO_PIPELINE: Without stop, st:1
W (197635) AUDIO_PIPELINE: Without wait stop, st:1
W (197645) AUDIO_ELEMENT: [i2s] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197655) AUDIO_ELEMENT: [http] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197655) AUDIO_PIPELINE: There are no listener registered
W (197675) AUDIO_PIPELINE: There are no listener registered
W (197675) AUDIO_ELEMENT: [http] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197685) AUDIO_ELEMENT: [i2s] Element has not create when AUDIO_ELEMENT_TERMINATE
I (197695) wifi:state: run -> init (0)
I (197695) wifi:pm stop, total sleep time: 186442255 us / 194856817 us

I (197695) wifi:new:<11,0>, old:<11,0>, ap:<255,255>, sta:<11,0>, prof:1
W (197705) PERIPH_WIFI: Wi-Fi disconnected from SSID esp32, auto-reconnect disabled, reconnect after 1000 ms
I (197715) wifi:flush txq
I (197715) wifi:stop sw txq
I (197725) wifi:lmac stop hw txq
I (197725) wifi:Deinit lldesc rx mblock:10
```


### Example Log

A complete log is as follows:

```c
I (0) cpu_start: App cpu up.
I (526) heap_init: Initializing. RAM available for dynamic allocation:
I (533) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (539) heap_init: At 3FFB9AD8 len 00026528 (153 KiB): DRAM
I (545) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (552) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (558) heap_init: At 40095C68 len 0000A398 (40 KiB): IRAM
I (564) cpu_start: Pro cpu start user code
I (247) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
I (277) REC_RAW_HTTP: [ 1 ] Initialize Button Peripheral & Connect to wifi network
I (297) wifi:wifi driver task: 3ffc2c98, prio:23, stack:3584, core=0
I (347) wifi:wifi firmware version: 5f8804c
I (347) wifi:config NVS flash: enabled
I (347) wifi:config nano formating: disabled
I (347) wifi:Init dynamic tx buffer num: 32
I (347) wifi:Init data frame dynamic rx buffer num: 32
I (357) wifi:Init management frame dynamic rx buffer num: 32
I (357) wifi:Init management short buffer num: 32
I (367) wifi:Init static rx buffer size: 1600
I (367) wifi:Init static rx buffer num: 10
I (367) wifi:Init dynamic rx buffer num: 32
W (377) phy_init: failed to load RF calibration data (0xffffffff), falling back to full calibration
I (547) phy: phy_version: 4180, cb3948e, Sep 12 2019, 16:39:13, 0, 2
I (567) wifi:mode : sta (94:b9:7e:65:c2:44)
I (1907) wifi:new:<11,0>, old:<1,0>, ap:<255,255>, sta:<11,0>, prof:1
I (2887) wifi:state: init -> auth (b0)
I (2897) wifi:state: auth -> assoc (0)
I (2907) wifi:state: assoc -> run (10)
I (2917) wifi:connected with esp32, aid = 2, channel 11, BW20, bssid = fc:ec:da:b7:11:c7
I (2917) wifi:security type: 4, phy: bgn, rssi: -32
I (2917) wifi:pm start, type: 1

I (2977) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (4777) REC_RAW_HTTP: [ 2 ] Start codec chip
E (4777) gpio: gpio_install_isr_service(412): GPIO isr service already installed
I (4797) REC_RAW_HTTP: [3.0] Create audio pipeline for recording
I (4797) REC_RAW_HTTP: [3.1] Create http stream to post data to server
I (4807) REC_RAW_HTTP: [3.2] Create i2s stream to read audio data from codec chip
I (4817) REC_RAW_HTTP: [3.3] Register all elements to audio pipeline
I (4817) REC_RAW_HTTP: [3.4] Link it together [codec_chip]-->i2s_stream->http_stream-->[http_server]
W (4847) PERIPH_TOUCH: _touch_init
I (4847) REC_RAW_HTTP: [ 4 ] Press [Rec] button to record, Press [Mode] to exit
I (11447) REC_RAW_HTTP: [ * ] [Rec] input key event, resuming pipeline ...
Total bytes written: 141312
I (14937) REC_RAW_HTTP: [ * ] [Rec] key released, stop pipeline ...
W (14937) AUDIO_ELEMENT: IN-[http] AEL_IO_ABORT
W (14937) HTTP_STREAM: No output due to stopping
I (14937) REC_RAW_HTTP: [ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker
E (14957) TRANS_TCP: tcp_poll_read select error 104, errno = Connection reset by peer, fd = 54
I (14957) REC_RAW_HTTP: [ + ] HTTP client HTTP_STREAM_FINISH_REQUEST
W (14967) AUDIO_PIPELINE: Without stop, st:1
W (14967) AUDIO_PIPELINE: Without wait stop, st:1
W (197635) REC_RAW_HTTP: [ * ] [Set] input key event, exit the demo ...
I (197635) REC_RAW_HTTP: [ 5 ] Stop audio_pipeline
W (197635) AUDIO_PIPELINE: Without stop, st:1
W (197635) AUDIO_PIPELINE: Without wait stop, st:1
W (197645) AUDIO_ELEMENT: [i2s] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197655) AUDIO_ELEMENT: [http] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197655) AUDIO_PIPELINE: There are no listener registered
W (197675) AUDIO_PIPELINE: There are no listener registered
W (197675) AUDIO_ELEMENT: [http] Element has not create when AUDIO_ELEMENT_TERMINATE
W (197685) AUDIO_ELEMENT: [i2s] Element has not create when AUDIO_ELEMENT_TERMINATE
I (197695) wifi:state: run -> init (0)
I (197695) wifi:pm stop, total sleep time: 186442255 us / 194856817 us

I (197695) wifi:new:<11,0>, old:<11,0>, ap:<255,255>, sta:<11,0>, prof:1
W (197705) PERIPH_WIFI: Wi-Fi disconnected from SSID esp32, auto-reconnect disabled, reconnect after 1000 ms
I (197715) wifi:flush txq
I (197715) wifi:stop sw txq
I (197725) wifi:lmac stop hw txq
I (197725) wifi:Deinit lldesc rx mblock:10
```

The complete log on the HTTP server is as follows:

```c
python2 server.py
Serving HTTP on 192.168.5.72 port 8000
Audio information, sample rates: 16000, bits: 16, channel(s): 2
Total bytes received: 141312
192.168.5.187 - - [18/Sep/2021 15:04:20] "POST /upload HTTP/1.1" 200 -
```


## Troubleshooting

If your development board cannot upload the voice to the HTTP server, please check the following configuration:
1. Whether the Wi-Fi configuration of the development board is correct.
2. Whether the development board has been connected to Wi-Fi and obtained the IP address successfully.
3. Whether the HTTP script URI on the server side is correctly configured.
4. Whether the HTTP script on the server side is running correctly.
5. Whether the development board and HTTP server are in the same Wi-Fi network.


## Technical Support and Feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/viewforum.php?f=20) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-adf/issues)

We will get back to you as soon as possible.
