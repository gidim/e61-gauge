#!/usr/bin/env python3
"""
Simple utilities for gauge calibration.
Claude Code will use these to drive the calibration process.
"""

import cv2
import serial
import serial.tools.list_ports
import time
import base64
import os
from openai import OpenAI


def find_arduino_port():
    """Find Arduino serial port automatically."""
    ports = serial.tools.list_ports.comports()

    # Try common Arduino patterns
    for port in ports:
        device = port.device.lower()
        desc = port.description.lower()
        if 'usb' in device or 'acm' in device or 'arduino' in desc or 'usb' in desc:
            return port.device

    # If no match, list all and ask
    if ports:
        print("Available serial ports:")
        for i, port in enumerate(ports):
            print(f"  {i}: {port.device} - {port.description}")
        return None

    return None


def connect_serial(port=None, baud=115200):
    """Connect to Arduino via serial."""
    if port is None:
        port = find_arduino_port()
        if port is None:
            raise Exception("No Arduino found. Please specify port manually.")

    ser = serial.Serial(port, baud, timeout=2)
    time.sleep(2)  # Wait for Arduino to reset

    # Flush any startup messages (retry — USB CDC may not be ready yet)
    for attempt in range(5):
        try:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            break
        except Exception:
            time.sleep(1)
    else:
        raise Exception(f"Device on {port} not ready after retries")
    time.sleep(0.5)
    while ser.in_waiting:
        ser.readline()

    print(f"Connected to Arduino on {port}")
    return ser


def send_command(ser, command, wait_time=0.5):
    """Send command to Arduino and return response lines."""
    ser.reset_input_buffer()
    ser.write(f"{command}\n".encode())
    time.sleep(wait_time)

    responses = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            responses.append(line)

    return responses


def capture_image(camera_index=0, rotate_180=True, save_path=None):
    """
    Capture image from webcam.

    Args:
        camera_index: Camera device index
        rotate_180: Whether to rotate image 180 degrees (camera is upside down)
        save_path: Optional path to save image

    Returns:
        numpy array of the captured image
    """
    cap = cv2.VideoCapture(camera_index)

    if not cap.isOpened():
        raise Exception(f"Could not open camera {camera_index}")

    # Let camera warm up
    time.sleep(0.5)
    for _ in range(5):
        cap.read()

    # Capture frame
    ret, frame = cap.read()
    cap.release()

    if not ret:
        raise Exception("Failed to capture image")

    # Rotate if camera is upside down
    if rotate_180:
        frame = cv2.rotate(frame, cv2.ROTATE_180)

    # Save if requested
    if save_path:
        cv2.imwrite(save_path, frame)
        print(f"Image saved to: {save_path}")

    return frame


def image_to_base64(image_path):
    """Convert image file to base64 string for API."""
    with open(image_path, "rb") as f:
        return base64.b64encode(f.read()).decode('utf-8')


def read_gauge_with_vision(image_path):
    """
    Use OpenAI vision API to read the gauge temperature.

    Args:
        image_path: Path to gauge image

    Returns:
        dict with 'temperature' (float) and 'confidence' (str)
    """
    client = OpenAI(api_key=os.environ.get("OPENAI_API_KEY"))

    # Encode image
    base64_image = image_to_base64(image_path)

    # Call vision API
    response = client.chat.completions.create(
        model="gpt-4o",
        messages=[
            {
                "role": "user",
                "content": [
                    {
                        "type": "text",
                        "text": """Look at this temperature gauge image. The gauge shows temperatures in both Celsius (inner scale, 0-150°C) and Fahrenheit (outer scale).

Please read the current needle position and tell me:
1. The temperature in Celsius (as accurately as possible)
2. Your confidence in the reading (high/medium/low)

Respond ONLY with a JSON object in this exact format:
{"temperature": <number>, "confidence": "<high/medium/low>"}

For example: {"temperature": 75.5, "confidence": "high"}"""
                    },
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": f"data:image/jpeg;base64,{base64_image}"
                        }
                    }
                ]
            }
        ],
        max_tokens=100
    )

    # Parse response
    import json
    response_text = response.choices[0].message.content.strip()

    # Extract JSON from response (in case there's extra text)
    if "```json" in response_text:
        response_text = response_text.split("```json")[1].split("```")[0].strip()
    elif "```" in response_text:
        response_text = response_text.split("```")[1].split("```")[0].strip()

    result = json.loads(response_text)
    return result


if __name__ == "__main__":
    # Quick test
    print("Testing utilities...")

    # Test camera
    print("\n1. Testing camera...")
    img = capture_image(save_path="test_capture.jpg")
    print(f"  Captured image: {img.shape}")

    # Test serial port finding
    print("\n2. Finding Arduino...")
    port = find_arduino_port()
    if port:
        print(f"  Found Arduino on: {port}")
    else:
        print("  No Arduino found automatically")

    print("\nUtilities test complete!")
