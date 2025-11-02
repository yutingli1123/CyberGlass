#!/usr/bin/env python3
"""
BLE photo capture helper for CyberGlass.

This tool connects to the CyberGlass BLE image service, requests a photo
capture, receives JPEG bytes via notifications, and saves the image to disk.
"""

import argparse
import asyncio
import os
import sys
from contextlib import suppress
from datetime import datetime
from typing import Optional

from bleak import BleakClient, BleakError, BleakScanner

# BLE UUIDs must match firmware definitions (ble_image_service.h)
BLE_IMAGE_SERVICE_UUID = "b0809f0c-691c-4ffa-a5a3-651350a1479a"
BLE_IMAGE_CONTROL_CHAR_UUID = "b0809f0d-691c-4ffa-a5a3-651350a1479a"
BLE_IMAGE_DATA_CHAR_UUID = "b0809f0e-691c-4ffa-a5a3-651350a1479a"


class BLEImageReceiver:
    """Tracks BLE notifications and assembles JPEG data."""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.photo_id = None
        self.expected_size = None
        self.capture_duration_ms = None
        self.transfer_duration_ms = None
        self.buffer = bytearray()
        self.done = asyncio.Event()
        self.error = None

    def log(self, message: str):
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] {message}")

    def handle_control(self, _sender: int, data: bytearray):
        text = data.decode("utf-8", errors="ignore").strip()
        if not text:
            return

        if self.verbose or not text.startswith("progress:"):
            self.log(f"CTRL: {text}")

        if text.startswith("start:"):
            # start:<photoId>:<bytes>:<width>:<height>:<captureMs>
            parts = text.split(":")
            if len(parts) >= 6:
                try:
                    self.photo_id = int(parts[1])
                    self.expected_size = int(parts[2])
                    self.capture_duration_ms = int(parts[5])
                    self.buffer = bytearray()
                    self.done.clear()
                except ValueError:
                    self.log("WARN: malformed start payload")
            else:
                self.log("WARN: unexpected start payload")

        elif text.startswith("progress:"):
            # progress:<sentBytes>:<totalBytes>
            if self.verbose:
                try:
                    sent, total = map(int, text.split(":")[1:3])
                    percent = (sent / total) * 100 if total else 0
                    self.log(f"Progress {sent}/{total} bytes ({percent:.1f}%)")
                except (ValueError, ZeroDivisionError):
                    self.log("WARN: malformed progress payload")

        elif text.startswith("done:"):
            # done:<photoId>:<bytesSent>:<transferMs>
            parts = text.split(":")
            if len(parts) >= 4:
                try:
                    self.transfer_duration_ms = int(parts[3])
                except ValueError:
                    self.transfer_duration_ms = None
            self.done.set()

        elif text.startswith("error:"):
            self.error = text
            self.done.set()

    def handle_data(self, _sender: int, data: bytearray):
        self.buffer.extend(data)


async def find_device(name_hint: Optional[str], address: Optional[str], timeout: float) -> str:
    """Find a BLE device by address or name hint."""
    if address:
        return address

    print(f"Scanning for BLE devices (timeout {timeout}s)...")
    devices = await BleakScanner.discover(timeout=timeout)
    if not devices:
        raise RuntimeError("No BLE devices found. Ensure the board is advertising.")

    matches = []
    for dev in devices:
        dev_name = dev.name or ""
        dev_address = dev.address or ""
        if not name_hint or (dev_name and name_hint.lower() in dev_name.lower()):
            matches.append(dev)

    if not matches:
        raise RuntimeError(
            f"No device found matching name hint '{name_hint}'. "
            "Use --list to see advertising devices."
        )

    if len(matches) > 1:
        print("Multiple devices matched the hint; using the first hit:")
        for dev in matches:
            print(f" - {dev.name or 'Unknown'} ({dev.address}) [RSSI {dev.rssi}]")

    chosen = matches[0]
    print(f"Selected device: {chosen.name or 'Unknown'} ({chosen.address})")
    return chosen.address


async def capture_ble_photo(args):
    os.makedirs(args.output, exist_ok=True)

    target = await find_device(args.name, args.address, args.scan_timeout)

    receiver = BLEImageReceiver(verbose=args.verbose)

    async with BleakClient(target, timeout=args.connect_timeout) as client:
        if args.verbose:
            receiver.log(f"Connected to {target}")

        control_started = False
        notifications_started = False

        try:
            # Ensure device exposes expected service
            services = None
            get_services_attr = getattr(client, "get_services", None)
            if callable(get_services_attr):
                result = get_services_attr()
                if asyncio.iscoroutine(result):
                    services = await result
                else:
                    services = result
            if services is None and hasattr(client, "services"):
                services = client.services

            if services and hasattr(services, "get_service"):
                if services.get_service(BLE_IMAGE_SERVICE_UUID) is None:
                    receiver.log("WARN: Image service UUID not found; continuing anyway.")
            else:
                receiver.log("WARN: Image service UUID not found; continuing anyway.")

            try:
                await client.start_notify(
                    BLE_IMAGE_CONTROL_CHAR_UUID,
                    receiver.handle_control,
                    use_indications=True,
                )
                control_started = True
            except TypeError:
                await client.start_notify(BLE_IMAGE_CONTROL_CHAR_UUID, receiver.handle_control)
                control_started = True
            except BleakError as exc:
                if "CCCD" in str(exc).lower():
                    await client.start_notify(BLE_IMAGE_CONTROL_CHAR_UUID, receiver.handle_control)
                    control_started = True
                else:
                    raise
            data_channel_mode = "notifications"
            try:
                await client.start_notify(
                    BLE_IMAGE_DATA_CHAR_UUID,
                    receiver.handle_data,
                    use_indications=True,
                )
                notifications_started = True
                data_channel_mode = "indications"
            except TypeError:
                await client.start_notify(BLE_IMAGE_DATA_CHAR_UUID, receiver.handle_data)
                notifications_started = True
            except BleakError as exc:
                # Some backends require indications to be enabled separately.
                if "CCCD" in str(exc).lower():
                    await client.start_notify(BLE_IMAGE_DATA_CHAR_UUID, receiver.handle_data)
                    notifications_started = True
                else:
                    raise
            if args.verbose:
                receiver.log(f"Data channel using {data_channel_mode}")

            # Ask for status to prime state (optional)
            await client.write_gatt_char(BLE_IMAGE_CONTROL_CHAR_UUID, b"status", response=True)
            await asyncio.sleep(0.2)

            receiver.log("Requesting photo capture...")
            await client.write_gatt_char(BLE_IMAGE_CONTROL_CHAR_UUID, b"capture", response=True)

            try:
                await asyncio.wait_for(receiver.done.wait(), timeout=args.capture_timeout)
            except asyncio.TimeoutError:
                received = len(receiver.buffer)
                expected = receiver.expected_size or 0
                if expected and received >= expected:
                    receiver.log(
                        "Timeout waiting for completion, but full image payload already received."
                    )
                    receiver.done.set()
                else:
                    receiver.log(
                        f"Timeout after receiving {received} / "
                        f"{expected or 'unknown'} bytes."
                    )
                    raise TimeoutError("Timed out waiting for BLE capture to finish.")

            if receiver.error:
                raise RuntimeError(f"Device reported error: {receiver.error}")

            if receiver.expected_size is None:
                raise RuntimeError("Did not receive capture metadata.")

            if len(receiver.buffer) != receiver.expected_size:
                receiver.log(
                    f"WARN: Received {len(receiver.buffer)} bytes but expected "
                    f"{receiver.expected_size}."
                )

            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"ble_photo_{receiver.photo_id or timestamp}.jpg"
            path = os.path.join(args.output, filename)
            with open(path, "wb") as f:
                f.write(receiver.buffer)

            receiver.log(f"Image saved to {path}")
            if receiver.capture_duration_ms is not None:
                receiver.log(f"Capture time: {receiver.capture_duration_ms} ms")
            if receiver.transfer_duration_ms is not None:
                receiver.log(f"Transfer time: {receiver.transfer_duration_ms} ms")
        finally:
            if notifications_started:
                with suppress(BleakError, ValueError):
                    await client.stop_notify(BLE_IMAGE_DATA_CHAR_UUID)
            if control_started:
                with suppress(BleakError, ValueError):
                    await client.stop_notify(BLE_IMAGE_CONTROL_CHAR_UUID)


async def list_devices(timeout: float):
    print(f"Scanning for {timeout} seconds...")
    devices = await BleakScanner.discover(timeout=timeout)
    if not devices:
        print("No BLE devices found.")
        return

    for dev in devices:
        print(f"{dev.address:>20}  {dev.rssi:>4} dBm  {dev.name or 'Unknown'}")


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Capture a photo from CyberGlass over BLE."
    )
    parser.add_argument(
        "--name",
        help="Case-insensitive substring of advertised device name (default: CyberGlass)",
        default="CyberGlass",
    )
    parser.add_argument(
        "--address",
        help="BLE MAC address (overrides name scan).",
    )
    parser.add_argument(
        "--output",
        help="Directory to store captured photos.",
        default="./ble_photos",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=6.0,
        help="Seconds to scan when searching for the device (default: 6.0).",
    )
    parser.add_argument(
        "--connect-timeout",
        type=float,
        default=15.0,
        help="Seconds before aborting the BLE connection attempt (default: 15).",
    )
    parser.add_argument(
        "--capture-timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for the capture to finish once requested (default: 30).",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List nearby BLE devices and exit.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print progress notifications during transfer.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    try:
        if args.list:
            asyncio.run(list_devices(args.scan_timeout))
        else:
            asyncio.run(capture_ble_photo(args))
    except (BleakError, RuntimeError, TimeoutError) as exc:
        print(f"❌ {exc}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        sys.exit(130)


if __name__ == "__main__":
    main()
