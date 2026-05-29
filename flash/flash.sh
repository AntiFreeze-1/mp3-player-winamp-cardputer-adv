#!/bin/bash
# Flash Winamp firmware to M5Stack Cardputer-Adv
# Usage: ./flash.sh [port]
# Default port: /dev/ttyUSB0

PORT="${1:-/dev/ttyUSB0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Flashing to $PORT ..."

esptool.py \
  --chip esp32s3 \
  --port "$PORT" \
  --baud 921600 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 8MB \
  0x0000  "$SCRIPT_DIR/bootloader.bin" \
  0x8000  "$SCRIPT_DIR/partitions.bin" \
  0xe000  "$SCRIPT_DIR/boot_app0.bin" \
  0x10000 "$SCRIPT_DIR/Winamp.bin"

echo "Done."
