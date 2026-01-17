# Pixy2 PixyMon Streamer

**A fork of pixy2-pixymon with HTTP streaming support for OctoPrint and other applications.**

This project extends the official PixyMon application to serve the Pixy2 camera feed via HTTP, enabling integration with 3D printer monitoring software like OctoPrint.

## Features

- **MJPEG Streaming**: Real-time video stream compatible with OctoPrint's webcam integration
- **Snapshot Endpoint**: Capture single frames on demand
- **60 FPS Support**: Matches Pixy2's maximum frame rate capability
- **Frame Caching**: Efficient encoding - only re-encodes when frames change
- **Multi-client Support**: Stream to multiple clients simultaneously
- **Configurable Port**: Set the HTTP server port via command line or config file

## HTTP Endpoints

| Endpoint                  | Description                  |
|---------------------------|------------------------------|
| `/pixy2/?action=stream`   | MJPEG stream (for OctoPrint) |
| `/pixy2/?action=snapshot` | Single JPEG frame            |
| `/pixy2/`                 | Usage information            |

## Usage

### Basic Usage

1. Connect your Pixy2 camera via USB
2. Run PixyMon
3. Access the stream at `http://localhost:8082/pixy2/?action=stream`

### OctoPrint Integration

In OctoPrint's webcam settings, set the stream URL to:

```text
http://<your-ip>:8082/pixy2/?action=stream
```

And the snapshot URL to:

```text
http://<your-ip>:8082/pixy2/?action=snapshot
```

### Configuring the Server Port

The HTTP server port defaults to **8082**. You can change it via command line:

```bash
./PixyMon --port 9000
```

Or via a config file (the command line takes priority):

**Linux**: `~/.config/Charmed Labs LLC/PixyMon.conf`

```ini
[General]
http_server_port=9000
```

**macOS**: `~/Library/Preferences/com.Charmed Labs LLC.PixyMon.plist`

**Windows**: Registry at `HKEY_CURRENT_USER\Software\Charmed Labs LLC\PixyMon`

### Firewall Configuration

To access the stream from other devices on your network, you may need to open the HTTP server port in your firewall.

**Linux (ufw)**:

```bash
sudo ufw allow 8082/tcp
```

**Linux (firewalld)**:

```bash
sudo firewall-cmd --permanent --add-port=8082/tcp
sudo firewall-cmd --reload
```

**Linux (iptables)**:

```bash
sudo iptables -A INPUT -p tcp --dport 8082 -j ACCEPT
```

**macOS**:

The macOS firewall typically allows outgoing connections. If you have the firewall enabled, go to System Preferences > Security & Privacy > Firewall > Firewall Options, and add PixyMon to the allowed applications.

**Windows**:

```powershell
netsh advfirewall firewall add rule name="PixyMon HTTP" dir=in action=allow protocol=tcp localport=8082
```

Or via Windows Defender Firewall GUI: allow inbound connections on TCP port 8082.

### Headless Operation

To run PixyMon without a display (e.g., on a headless server):

```bash
# Install Xvfb if not already installed
sudo apt install xvfb

# Run PixyMon with a virtual display
xvfb-run -a ./build/pixymon/PixyMon &
```

## Building

### Prerequisites

- Qt 6 (with network module)
- libusb-1.0

### Linux

```bash
# Install dependencies
sudo apt install qt6-base-dev libusb-1.0-0-dev

# Build
cd src/host/pixymon
qmake6
make
```

### macOS

```bash
# Install dependencies via MacPorts or Homebrew
brew install qt@6 libusb

# Build
cd src/host/pixymon
qmake6
make
```

## Directory Structure

- `/releases` - Binaries for various releases
- `/scripts` - Build scripts for Pixy software modules
- `/src/device` - Firmware that runs on the Pixy2 device
- `/src/host` - Host computer software (Windows, Linux, Mac)
  - `/src/host/pixymon` - PixyMon application with HTTP streaming

## License

All Pixy source code is provided under the terms of the [GNU General Public License v2](http://www.gnu.org/licenses/gpl-2.0.html).

For alternative licensing, contact <cmucam@cs.cmu.edu>.

## Credits

- Original Pixy2 project by [Charmed Labs](https://pixycam.com/)
- HTTP streaming implementation added for OctoPrint integration
