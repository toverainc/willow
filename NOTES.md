Tag working_button:

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
