// BLE UART Service UUIDs (Nordic UART Service)
const NUS_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // Write to device
const NUS_RX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // Receive from device

class BLEConnection {
    constructor() {
        this.device = null;
        this.server = null;
        this.service = null;
        this.txCharacteristic = null;
        this.rxCharacteristic = null;
        this.connected = false;
        this.onReceive = null;
        this.onConnectionChange = null;
        this.responseBuffer = '';
    }

    async connect() {
        try {
            // Request device - filter by UART service OR name containing "Rocket"
            this.device = await navigator.bluetooth.requestDevice({
                filters: [
                    { services: [NUS_SERVICE_UUID] },
                    { namePrefix: 'Rocket' }
                ]
            });

            // Add disconnect listener
            this.device.addEventListener('gattserverdisconnected', () => {
                this.handleDisconnect();
            });

            // Connect to GATT server
            this.server = await this.device.gatt.connect();

            // Get UART service
            this.service = await this.server.getPrimaryService(NUS_SERVICE_UUID);

            // Get TX characteristic (for writing to device)
            this.txCharacteristic = await this.service.getCharacteristic(NUS_TX_UUID);

            // Get RX characteristic (for receiving from device)
            this.rxCharacteristic = await this.service.getCharacteristic(NUS_RX_UUID);

            // Start notifications for RX
            await this.rxCharacteristic.startNotifications();
            this.rxCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
                this.handleReceive(event);
            });

            this.connected = true;
            if (this.onConnectionChange) {
                this.onConnectionChange(true, this.device.name);
            }

            return { success: true, deviceName: this.device.name };
        } catch (error) {
            console.error('BLE connection error:', error);
            return { success: false, error: error.message };
        }
    }

    disconnect() {
        if (this.device && this.device.gatt.connected) {
            this.device.gatt.disconnect();
        }
        this.handleDisconnect();
    }

    handleDisconnect() {
        this.connected = false;
        this.device = null;
        this.server = null;
        this.service = null;
        this.txCharacteristic = null;
        this.rxCharacteristic = null;
        if (this.onConnectionChange) {
            this.onConnectionChange(false, null);
        }
    }

    handleReceive(event) {
        const value = event.target.value;
        const decoder = new TextDecoder('utf-8');
        const text = decoder.decode(value);

        // Buffer incoming data
        this.responseBuffer += text;

        // Process complete lines
        const lines = this.responseBuffer.split('\n');

        // Keep the last incomplete line in buffer
        this.responseBuffer = lines.pop() || '';

        // Send complete lines to callback
        for (const line of lines) {
            if (line.trim() && this.onReceive) {
                this.onReceive(line.trim());
            }
        }
    }

    async send(command) {
        if (!this.connected || !this.txCharacteristic) {
            throw new Error('Not connected');
        }

        // Add newline if not present
        if (!command.endsWith('\n')) {
            command += '\n';
        }

        const encoder = new TextEncoder();
        const data = encoder.encode(command);

        // BLE has a max packet size (usually 20 bytes for older devices, 512 for newer)
        // Split into chunks if needed
        const chunkSize = 20;
        for (let i = 0; i < data.length; i += chunkSize) {
            const chunk = data.slice(i, i + chunkSize);
            await this.txCharacteristic.writeValue(chunk);
        }

        return true;
    }

    isConnected() {
        return this.connected;
    }

    getDeviceName() {
        return this.device ? this.device.name : null;
    }
}

// Check if Web Bluetooth is supported
function isBLESupported() {
    return navigator.bluetooth !== undefined;
}

// Export for use in app.js
window.BLEConnection = BLEConnection;
window.isBLESupported = isBLESupported;
