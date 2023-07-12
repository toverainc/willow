# Testing without API

Using [intray](https://github.com/Gowee/intray/):
```
./intray-x86_64-unknown-linux-musl --dir /tmp
```

Point CONFIG_SERVER_URI to http://192.168.0.10:8080/upload/full/willow.raw.
If the file already exists, intray will add a suffix. These files can be
played with ffplay:

```
ffplay -f s16le -ac 1 -ar 16000 willow.raw
```

The options depend on what is configured in the ESP firmware.

# Using addr2line

When you're experiencing crashes, you can use addr2line to lookup where the crash happens. Example:

```
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.

Core  0 register dump:
PC      : 0x42043aff  PS      : 0x00060f30  A0      : 0x82042b4d  A1      : 0x3fcaf490
A2      : 0xffffffff  A3      : 0x00000002  A4      : 0x00000020  A5      : 0x0000ff00
A6      : 0x00ff0000  A7      : 0xff000000  A8      : 0x820429a8  A9      : 0x3fcaf440
A10     : 0x00000023  A11     : 0x3fcafc88  A12     : 0x000001ef  A13     : 0x00000000
A14     : 0x00000003  A15     : 0xff000000  SAR     : 0x00000004  EXCCAUSE: 0x0000001c
EXCVADDR: 0xffffffff  LBEG    : 0x42042990  LEND    : 0x4204299c  LCOUNT  : 0x00000000


Backtrace: 0x42043afc:0x3fcaf490 0x42042b4a:0x3fcaf4b0 0x420245f6:0x3fcaf4d0 0x42024637:0x3fcaf840 0x420105ea:0x3fcaf860 0x42008b20:0x3fcaf890 0x4211bd17:0x3fcafac0
```

```
$ /opt/esp/tools/xtensa-esp32s3-elf/esp-2021r2-patch5-8.4.0/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-addr2line -e build/willow.elf 0x42043afc:0x3fcaf490 0x42042b4a:0x3fcaf4b0 0x420245f6:0x3fcaf4d0 0x42024637:0x3fcaf840 0x420105ea:0x3fcaf860 0x42008b20:0x3fcaf890 0x4211bd17:0x3fcafac0
/home/sunxiangyu/workspace/esp_sr_lib/build/../components/fst/subword.c:112
/home/sunxiangyu/workspace/esp_sr_lib/build/../components/fst/command.c:203
/home/sunxiangyu/workspace/esp_sr_lib/build/../components/multinet/multinet6_quantized.c:344
/home/sunxiangyu/workspace/esp_sr_lib/build/../components/multinet/multinet6_quantized.c:1831
/willow/deps/esp-adf/components/audio_recorder/recorder_sr.c:556 (discriminator 2)
/willow/main/main.c:558
/opt/esp/idf/components/freertos/port/port_common.c:141
```

# Generate NVS partition

nvs.csv:
```
key,type,encoding,value
WIFI,namespace,,
PSK,data,string,mypassword
SSID,data,string,myssid
```

```
/opt/esp/idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate --version 2 nvs.csv nvs.bin 0x24000
```
