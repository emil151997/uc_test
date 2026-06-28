# uc_test

## Common Description
C-Written Program to parse Telemetry Information from Binary File

## Algorithm Description
- Step 1. Start reading Binary File
- Step 2. Find First sync marker
- Step 3. If Wound, Start Writing to File From Sync Market
- Step 4. Search For Next Sync Marker. 
- Step 5. If Found, Calculate Frame Size (shown in console, count from 0) and Repeat Step 3-5. Otherwise, Step 6
- Step 6. Write to File Last Bytes
- Step 7. Close Both Files

## Usage
### Requirements
- Work Environment:
  - Arch: x86_64
  - Linux OS 
- Toolchains:
  - GCC C Compiler
  - Make

### Build
To build the program, you can use a script `build.sh` (with executing rights) from project directory or just execute `make clean && make` in console.

### Run
To run the program, you can use a script `run.sh`(with executing rights) from project directory or just execute `./uc_test` in console. Please make sure that the udk_dump.bin file is in the same directory as the executable file.