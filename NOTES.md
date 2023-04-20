# Testing without API

Using [intray](https://github.com/Gowee/intray/):
```
./intray-x86_64-unknown-linux-musl --dir /tmp
```

Point CONFIG_SERVER_URI to http://192.168.0.10:8080/upload/full/sallow.raw.
If the file already exists, intray will add a suffix. These files can be
played with ffplay:

```
ffplay -f s16le -ac 1 -ar 16000 sallow.raw
```

The options depend on what is configured in the ESP firmware.

# Versions
## Tag working_button

* starts the HTTP POST as soon as we're pushing the REC button.
* does not use i2s_config struct to change bit or sample rate, but:

```
#define AUDIO_SAMPLE_RATE  (16000)
#define AUDIO_BITS         (16)
#define AUDIO_CHANNELS     (1)

i2s_stream_set_clk(i2s_stream_reader, AUDIO_SAMPLE_RATE, AUDIO_BITS, AUDIO_CHANNELS);
```

* resulting audio plays undistorted with following command:
```ffplay -f s16le -ac 1 -ar 16000 sallow.raw```
