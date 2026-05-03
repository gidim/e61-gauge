// Application state
const state = {
    mode: 'WORK', // 'WORK' or 'CAL'
    temperature: null,
    tempZone: 'unknown',
    battery: {
        voltage: null,
        percent: null,
        charging: false
    },
    calibration: {
        cold: 0,
        green: 0,
        hot: 0
    },
    tempTestMode: false
};

// BLE connection instance
let ble = null;

// DOM elements
const elements = {};

// Initialize app
document.addEventListener('DOMContentLoaded', () => {
    // Check BLE support
    if (!isBLESupported()) {
        document.getElementById('app').innerHTML = `
            <div class="not-supported">
                <h2>Bluetooth Not Supported</h2>
                <p>Web Bluetooth is not available in this browser.</p>
                <p>Please use Chrome on Android, macOS, or Windows.</p>
                <p class="text-small mt-2">iOS Safari does not support Web Bluetooth.</p>
            </div>
        `;
        return;
    }

    // Cache DOM elements
    cacheElements();

    // Initialize BLE
    ble = new BLEConnection();
    ble.onConnectionChange = handleConnectionChange;
    ble.onReceive = handleReceive;

    // Setup event listeners
    setupEventListeners();

    // Log startup
    logTerminal('Ready to connect', 'system');
});

function cacheElements() {
    elements.connectBtn = document.getElementById('connectBtn');
    elements.statusIndicator = document.getElementById('statusIndicator');
    elements.deviceName = document.getElementById('deviceName');

    elements.tabs = document.querySelectorAll('.tab');
    elements.panels = document.querySelectorAll('.panel');

    elements.tempValue = document.getElementById('tempValue');
    elements.tempZone = document.getElementById('tempZone');
    elements.batteryLevel = document.getElementById('batteryLevel');
    elements.batteryPercent = document.getElementById('batteryPercent');
    elements.batteryVoltage = document.getElementById('batteryVoltage');
    elements.chargingIndicator = document.getElementById('chargingIndicator');

    elements.modeValue = document.getElementById('modeValue');
    elements.calModeValue = document.getElementById('calModeValue');
    elements.modeToggleBtn = document.getElementById('modeToggleBtn');

    elements.homeBtn = document.getElementById('homeBtn');
    elements.tempBtn = document.getElementById('tempBtn');
    elements.batteryBtn = document.getElementById('batteryBtn');
    elements.tempTestBtn = document.getElementById('tempTestBtn');

    elements.stepMinus100 = document.getElementById('stepMinus100');
    elements.stepMinus10 = document.getElementById('stepMinus10');
    elements.stepPlus10 = document.getElementById('stepPlus10');
    elements.stepPlus100 = document.getElementById('stepPlus100');
    elements.stepInput = document.getElementById('stepInput');
    elements.gotoStepBtn = document.getElementById('gotoStepBtn');

    elements.gotoTempInput = document.getElementById('gotoTempInput');
    elements.gotoTempBtn = document.getElementById('gotoTempBtn');

    elements.calTempInput = document.getElementById('calTempInput');
    elements.calBtn = document.getElementById('calBtn');

    elements.coldCount = document.getElementById('coldCount');
    elements.greenCount = document.getElementById('greenCount');
    elements.hotCount = document.getElementById('hotCount');

    elements.offsetInput = document.getElementById('offsetInput');
    elements.setOffsetBtn = document.getElementById('setOffsetBtn');
    elements.maxStepsInput = document.getElementById('maxStepsInput');
    elements.setMaxStepsBtn = document.getElementById('setMaxStepsBtn');

    elements.clearAllBtn = document.getElementById('clearAllBtn');
    elements.clearColdBtn = document.getElementById('clearColdBtn');
    elements.clearGreenBtn = document.getElementById('clearGreenBtn');
    elements.clearHotBtn = document.getElementById('clearHotBtn');
    elements.listBtn = document.getElementById('listBtn');
    elements.suggestBtn = document.getElementById('suggestBtn');
    elements.saveBtn = document.getElementById('saveBtn');

    elements.terminalOutput = document.getElementById('terminalOutput');
    elements.terminalInput = document.getElementById('terminalInput');
    elements.terminalSendBtn = document.getElementById('terminalSendBtn');
    elements.terminalClearBtn = document.getElementById('terminalClearBtn');
}

function setupEventListeners() {
    // Connection
    elements.connectBtn.addEventListener('click', handleConnect);

    // Tabs
    elements.tabs.forEach(tab => {
        tab.addEventListener('click', () => switchTab(tab.dataset.tab));
    });

    // Dashboard actions
    elements.homeBtn.addEventListener('click', () => sendCommand('HOME'));
    elements.tempBtn.addEventListener('click', () => sendCommand('TEMP'));
    elements.batteryBtn.addEventListener('click', () => sendCommand('BATTERY'));
    elements.tempTestBtn.addEventListener('click', () => sendCommand('TEMPTEST'));
    elements.modeToggleBtn.addEventListener('click', toggleMode);

    // Step controls
    elements.stepMinus100.addEventListener('click', () => sendCommand('-100'));
    elements.stepMinus10.addEventListener('click', () => sendCommand('-10'));
    elements.stepPlus10.addEventListener('click', () => sendCommand('+10'));
    elements.stepPlus100.addEventListener('click', () => sendCommand('+100'));
    elements.gotoStepBtn.addEventListener('click', () => {
        const step = elements.stepInput.value;
        if (step) sendCommand(`STEP ${step}`);
    });

    // Goto temp
    elements.gotoTempBtn.addEventListener('click', () => {
        const temp = elements.gotoTempInput.value;
        if (temp) sendCommand(`GOTO ${temp}`);
    });

    // Calibration
    elements.calBtn.addEventListener('click', () => {
        const temp = elements.calTempInput.value;
        if (temp) sendCommand(`CAL ${temp}`);
    });

    // Settings
    elements.setOffsetBtn.addEventListener('click', () => {
        const offset = elements.offsetInput.value;
        if (offset) sendCommand(`OFFSET ${offset}`);
    });
    elements.setMaxStepsBtn.addEventListener('click', () => {
        const maxSteps = elements.maxStepsInput.value;
        if (maxSteps) sendCommand(`MAXSTEPS ${maxSteps}`);
    });

    // Clear buttons
    elements.clearAllBtn.addEventListener('click', () => sendCommand('CLEAR ALL'));
    elements.clearColdBtn.addEventListener('click', () => sendCommand('CLEAR COLD'));
    elements.clearGreenBtn.addEventListener('click', () => sendCommand('CLEAR GREEN'));
    elements.clearHotBtn.addEventListener('click', () => sendCommand('CLEAR HOT'));

    // Calibration info
    elements.listBtn.addEventListener('click', () => sendCommand('LIST'));
    elements.suggestBtn.addEventListener('click', () => sendCommand('SUGGEST'));
    elements.saveBtn.addEventListener('click', () => sendCommand('SAVE'));

    // Terminal
    elements.terminalSendBtn.addEventListener('click', sendTerminalCommand);
    elements.terminalInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') sendTerminalCommand();
    });
    elements.terminalClearBtn.addEventListener('click', clearTerminal);
}

async function handleConnect() {
    if (ble.isConnected()) {
        ble.disconnect();
    } else {
        elements.connectBtn.disabled = true;
        elements.connectBtn.textContent = 'Connecting...';
        const result = await ble.connect();
        if (!result.success) {
            logTerminal(`Connection failed: ${result.error}`, 'error');
            elements.connectBtn.disabled = false;
            elements.connectBtn.textContent = 'Connect';
        }
    }
}

function handleConnectionChange(connected, deviceName) {
    elements.connectBtn.disabled = false;

    if (connected) {
        elements.connectBtn.textContent = 'Disconnect';
        elements.connectBtn.classList.remove('btn-primary');
        elements.connectBtn.classList.add('btn-danger');
        elements.statusIndicator.classList.add('connected');
        elements.deviceName.textContent = deviceName || 'Connected';
        logTerminal(`Connected to ${deviceName}`, 'system');

        // Request initial status
        setTimeout(() => {
            sendCommand('TEMP');
            setTimeout(() => sendCommand('BATTERY'), 500);
        }, 500);
    } else {
        elements.connectBtn.textContent = 'Connect';
        elements.connectBtn.classList.remove('btn-danger');
        elements.connectBtn.classList.add('btn-primary');
        elements.statusIndicator.classList.remove('connected');
        elements.deviceName.textContent = 'Not connected';
        logTerminal('Disconnected', 'system');
    }
}

function handleReceive(message) {
    logTerminal(message, 'received');
    parseResponse(message);
}

function parseResponse(message) {
    const msg = message.trim();

    // Temperature reading
    const tempMatch = msg.match(/(?:Current temp|Temp):\s*([\d.]+)\s*C/i);
    if (tempMatch) {
        const temp = parseFloat(tempMatch[1]);
        updateTemperature(temp);
    }

    // Zone detection from temp message
    if (msg.includes('COLD') || msg.includes('Cold')) {
        state.tempZone = 'cold';
        updateTempZoneDisplay();
    } else if (msg.includes('GREEN') || msg.includes('OPTIMAL') || msg.includes('Green')) {
        state.tempZone = 'green';
        updateTempZoneDisplay();
    } else if (msg.includes('HOT') || msg.includes('Hot')) {
        state.tempZone = 'hot';
        updateTempZoneDisplay();
    }

    // Battery reading
    const batteryMatch = msg.match(/Battery:\s*(\d+)\s*mV/i);
    if (batteryMatch) {
        const voltage = parseInt(batteryMatch[1]);
        state.battery.voltage = voltage;
        updateBatteryDisplay();
    }

    const percentMatch = msg.match(/Estimated:\s*(\d+)%/i);
    if (percentMatch) {
        state.battery.percent = parseInt(percentMatch[1]);
        updateBatteryDisplay();
    }

    // Charging status
    if (msg.includes('CHARGING')) {
        state.battery.charging = true;
        updateBatteryDisplay();
    } else if (msg.includes('Not charging')) {
        state.battery.charging = false;
        updateBatteryDisplay();
    }

    // Mode changes
    if (msg.includes('Calibration mode: ON') || msg.includes('Current mode: CALIBRATION')) {
        state.mode = 'CAL';
        updateModeDisplay();
    } else if (msg.includes('Regular mode: ON') || msg.includes('Current mode: REGULAR')) {
        state.mode = 'WORK';
        updateModeDisplay();
    }

    // Temp test mode
    if (msg.includes('TEMP TEST mode: ON')) {
        state.tempTestMode = true;
        elements.tempTestBtn.classList.add('btn-success');
        elements.tempTestBtn.classList.remove('btn-secondary');
    } else if (msg.includes('TEMP TEST mode: OFF')) {
        state.tempTestMode = false;
        elements.tempTestBtn.classList.remove('btn-success');
        elements.tempTestBtn.classList.add('btn-secondary');
    }

    // Calibration zone counts from STATUS
    const coldMatch = msg.match(/Cold.*?:\s*(\d+)\//i);
    if (coldMatch) {
        state.calibration.cold = parseInt(coldMatch[1]);
        updateCalibrationCounts();
    }

    const greenMatch = msg.match(/Green.*?:\s*(\d+)\//i);
    if (greenMatch) {
        state.calibration.green = parseInt(greenMatch[1]);
        updateCalibrationCounts();
    }

    const hotMatch = msg.match(/Hot.*?:\s*(\d+)\//i);
    if (hotMatch) {
        state.calibration.hot = parseInt(hotMatch[1]);
        updateCalibrationCounts();
    }
}

function updateTemperature(temp) {
    state.temperature = temp;
    elements.tempValue.textContent = temp.toFixed(1);

    // Determine zone
    if (temp < 92) {
        state.tempZone = 'cold';
    } else if (temp <= 97) {
        state.tempZone = 'green';
    } else {
        state.tempZone = 'hot';
    }
    updateTempZoneDisplay();
}

function updateTempZoneDisplay() {
    elements.tempZone.className = 'temp-zone zone-' + state.tempZone;
    const zoneNames = {
        cold: 'Cold',
        green: 'Optimal',
        hot: 'Hot',
        unknown: '--'
    };
    elements.tempZone.textContent = zoneNames[state.tempZone];
}

function updateBatteryDisplay() {
    const percent = state.battery.percent || 0;
    const voltage = state.battery.voltage;

    elements.batteryLevel.style.width = percent + '%';
    elements.batteryLevel.className = 'battery-level';
    if (percent <= 20) {
        elements.batteryLevel.classList.add('low');
    } else if (percent <= 50) {
        elements.batteryLevel.classList.add('medium');
    }

    elements.batteryPercent.textContent = percent + '%';
    elements.batteryVoltage.textContent = voltage ? `${(voltage / 1000).toFixed(2)}V` : '--';
    elements.chargingIndicator.style.display = state.battery.charging ? 'block' : 'none';
}

function updateModeDisplay() {
    elements.modeValue.textContent = state.mode;
    elements.modeValue.className = 'mode-value ' + state.mode.toLowerCase();
    elements.calModeValue.textContent = state.mode;
    elements.calModeValue.className = 'mode-value ' + state.mode.toLowerCase();
    elements.modeToggleBtn.textContent = state.mode === 'CAL' ? 'Switch to Work' : 'Switch to Cal';
}

function updateCalibrationCounts() {
    elements.coldCount.textContent = state.calibration.cold;
    elements.greenCount.textContent = state.calibration.green;
    elements.hotCount.textContent = state.calibration.hot;
}

function toggleMode() {
    const newMode = state.mode === 'CAL' ? 'WORK' : 'CAL';
    sendCommand(`MODE ${newMode}`);
}

function switchTab(tabName) {
    elements.tabs.forEach(tab => {
        tab.classList.toggle('active', tab.dataset.tab === tabName);
    });
    elements.panels.forEach(panel => {
        panel.classList.toggle('active', panel.id === tabName + 'Panel');
    });

    // Request calibration status when switching to calibration tab
    if (tabName === 'calibration' && ble.isConnected()) {
        sendCommand('STATUS');
    }
}

async function sendCommand(command) {
    if (!ble.isConnected()) {
        logTerminal('Not connected', 'error');
        return;
    }

    try {
        logTerminal(command, 'sent');
        await ble.send(command);
    } catch (error) {
        logTerminal(`Send error: ${error.message}`, 'error');
    }
}

function sendTerminalCommand() {
    const command = elements.terminalInput.value.trim();
    if (command) {
        sendCommand(command);
        elements.terminalInput.value = '';
    }
}

function logTerminal(message, type = 'received') {
    const line = document.createElement('div');
    line.className = `terminal-line ${type}`;
    line.textContent = message;
    elements.terminalOutput.appendChild(line);
    elements.terminalOutput.scrollTop = elements.terminalOutput.scrollHeight;
}

function clearTerminal() {
    elements.terminalOutput.innerHTML = '';
    logTerminal('Terminal cleared', 'system');
}
