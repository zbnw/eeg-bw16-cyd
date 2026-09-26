# BW16 EEG byte bridge

The BW16 receives the EEG module's UART bytes on PB2 (Arduino D5) at 57600 8N1
and forwards those same bytes to the USB/log UART at 115200 8N1. EEG TX goes to
PB2 and EEG GND goes to BW16 GND. EEG RX and BW16 PB1 remain unconnected.

Set the PC serial terminal to the currently enumerated port, 115200 baud, and **HEX display**. The stream
contains the original packet bytes without ASCII conversion, added spaces, or
line breaks. For example, a raw packet with value zero appears as these eight
bytes:

    AA AA 04 80 02 00 00 7D

The bridge forwards every received byte, including metrics packets and any bad
or partial packets. It does not interpret, modify, or filter the EEG stream.
Boot ROM messages may appear briefly when the BW16 resets; the application
itself emits no diagnostic text. Changing the PC terminal's baud rate does not
change the EEG side's 57600-baud setting.

## Build and upload

Target: `realtek:AmebaD:Ai-Thinker_BW16`, AmebaD core 3.1.7.

```powershell
arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 --build-path .build/bw16_eeg_reader sketches/bw16_eeg_reader
$bwTools = "$env:LOCALAPPDATA/Arduino15/packages/realtek/tools/ameba_d_tools/1.1.3"
$bwPort = 'COMx' # Replace with the current BW16 port.
Get-FileHash .build/bw16_eeg_reader/km0_km4_image2.bin
Get-FileHash "$bwTools/km0_km4_image2.bin"
# Proceed only if both hashes match.
& "$bwTools/upload_image_tool_windows.exe" $bwTools $bwPort Ai-Thinker_BW16 Enable Disable 1500000
```

Close serial monitors before upload. The vendor uploader's auto-upload setting
worked on this board. Require `All images are sent successfully!` in its output.
The installed Arduino CLI upload command does not discover this core's
specially named firmware binary.

This diagnostic sketch forwards incoming bytes as-is, including malformed and
partial packets. Validate it with a local capture; no EEG recordings are included
in this repository.

Disconnect USB, programming hardware, chargers, and other wired equipment before
electrodes touch a person. The UART ground connection is not galvanic isolation.
