// js/config.js
// Configuration page - loaded only when needed

const RelayTestDuration = 2000
// ============================================================
// Configuration - Sensor Config
// ============================================================
const UNIT_DEFINITIONS = [
    { id: 0, label: 'None', symbol: '' },
    { id: 1, label: 'Celsius', symbol: '°C' },
    { id: 2, label: 'pH', symbol: 'pH' },
    { id: 3, label: 'Millivolt', symbol: 'mV' },
    { id: 4, label: 'mg/L', symbol: 'mg/L' },
    { id: 5, label: 'µS/cm', symbol: 'µS/cm' },
    { id: 6, label: 'Percent', symbol: '%' },
    { id: 7, label: 'NTU', symbol: 'NTU' },
    { id: 8, label: 'PPM', symbol: 'ppm' },
    { id: 9, label: 'µg/L', symbol: 'µg/L' }
];

// Get unit symbol by ID
function getUnitSymbol(unitId) {
    const unit = UNIT_DEFINITIONS.find(u => u.id === unitId);
    return unit ? unit.symbol : '';
}

function createUnitDropdown(sensorId, selectedId) {
    let html = `<select class="sensor-unit-select" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;" data-id="${sensorId}">`;
    UNIT_DEFINITIONS.forEach(unit => {
        const selected = (unit.id === selectedId) ? 'selected' : '';
        html += `<option value="${unit.id}" ${selected}>${unit.label} (${unit.symbol})</option>`;
    });
    html += `</select>`;
    return html;
}

async function loadSensorConfig() {
    try {
        const data = await api.get('/api/sensors/config');
        const list = document.getElementById('sensor-config-list');
        if (!list) return;
        const sensors = data.sensors || [];
        if (sensors.length === 0) {
            list.innerHTML = '<p style="color:#7a9bbf;">No sensors configured</p>';
            return;
        }
        list.innerHTML = sensors.map(s => {
            const adcDisplay = s.adc_channel !== undefined && s.adc_channel !== 255 ? `CH${s.adc_channel}` : 'Digital';
            return `
            <div class="sensor-card" style="background:#fff; border-radius:12px; border:1px solid #e6edf6; padding:14px 16px; margin-bottom:10px;">
                <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; border-bottom:1px solid #f0f4f9; padding-bottom:8px; margin-bottom:8px;">
                    <strong style="color:#0a2744;">${s.name || 'Sensor ' + s.id}</strong>
                    <span style="font-size:0.75rem; color:#7a9bbf;">PIN: ${s.gpio_pin || '--'} | ADC: ${adcDisplay} | MODBUS: 0x${(s.modbus_register || 0).toString(16).toUpperCase().padStart(4, '0')}</span>
                </div>
                <div style="display:grid; grid-template-columns: 1fr 1fr 1fr; gap:8px;">
                    <div>
                        <label style="font-size:0.7rem; color:#7a9bbf;">Name</label>
                        <input type="text" value="${s.name || ''}" data-id="${s.id}" class="sensor-name-input" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;">
                    </div>
                    <div class="unit-container">
                        <label style="font-size:0.7rem; color:#7a9bbf;">Unit</label>
                        ${createUnitDropdown(s.id, s.unit || 0)}
                    </div>
                    <div>
                        <label class="calibrate-sensor-btn" data-id="${s.id}" style="font-size:0.7rem; color:#7a9bbf;">Calibration (×1000)</label>
                        <input type="number" value="${s.calibration_factor || 1000}" data-id="${s.id}" class="sensor-cal-input" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;">
                    </div>
                </div>
                <div style="display:flex; justify-content:space-between; align-items:center; margin-top:8px; flex-wrap:wrap; gap:8px;">
                    <label style="font-size:0.75rem; color:#7a9bbf; display:flex; align-items:center; gap:6px;">
                        <span>Enabled</span>
                        <input type="checkbox" ${s.enabled !== false ? 'checked' : ''} data-id="${s.id}" class="sensor-enabled-check">
                    </label>
                    <div style="display:flex; gap:12px; font-size:0.75rem; color:#3d6a9b;">
                        <span>Value: ${s.current_value !== undefined && s.status !== 3 ? s.current_value.toFixed(2) : '--'}</span>
                        <span>Min: ${s.min_value || 0}</span>
                        <span>Max: ${s.max_value || 100}</span>
                    </div>
                </div>
            </div>
        `}).join('');

        document.querySelectorAll('.calibrate-sensor-btn')?.forEach(btn => {
            btn.addEventListener('click', function() {
                const id = parseInt(this.dataset.id);
                window.location.href = `/calibration.html?sensor=${id}`;
            });
        });
    } catch (e) {
        console.warn('Sensor config load failed:', e);
    }
}

async function saveSensorConfig() {
    const inputs = document.querySelectorAll('.sensor-name-input');
    const configs = [];
    inputs.forEach(inp => {
        const id = parseInt(inp.dataset.id);
        const name = inp.value;
        const enabled = document.querySelector(`.sensor-enabled-check[data-id="${id}"]`)?.checked || false;
        const cal = parseInt(document.querySelector(`.sensor-cal-input[data-id="${id}"]`)?.value) || 1000;
        const unit = parseInt(document.querySelector(`.sensor-unit-select[data-id="${id}"]`)?.value) || 0;
        configs.push({ id, name, enabled, calibration_factor: cal, unit});
    });
    try {
        const result = await api.post('/api/sensors/config', { sensors: configs });
        alert(result.message || '✅ Sensor configuration saved');
        await loadSensorConfig();
    } catch (e) {
        alert('❌ Failed to save: ' + e.message);
    }
}

// ============================================================
// Configuration - Relay Config
// ============================================================
async function loadRelayConfig() {
    try {
        const data = await api.get('/api/relays/config');
        const list = document.getElementById('relay-config-list');
        if (!list) return;
        const relays = data.relays || [];
        if (relays.length === 0) {
            list.innerHTML = '<p style="color:#7a9bbf;">No relays configured</p>';
            return;
        }
        list.innerHTML = relays.map(r => `
            <div style="background:#fff; border-radius:12px; border:1px solid #e6edf6; padding:14px 16px; margin-bottom:10px;">
                <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; border-bottom:1px solid #f0f4f9; padding-bottom:8px; margin-bottom:8px;">
                    <strong style="color:#0a2744;">${r.name || 'Relay ' + r.id}</strong>
                    <span style="font-size:0.75rem; color:#7a9bbf;">PIN: ${r.gpio_pin || '--'} | MODBUS: 0x${(r.modbus_register || 0).toString(16).toUpperCase().padStart(4, '0')}</span>
                </div>
                <div style="display:grid; grid-template-columns: 1fr 1fr 1fr; gap:8px;">
                    <div>
                        <label style="font-size:0.7rem; color:#7a9bbf;">Name</label>
                        <input type="text" value="${r.name || ''}" data-id="${r.id}" class="relay-name-input" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;">
                    </div>
                    <div>
                        <label style="font-size:0.7rem; color:#7a9bbf;">Duration (ms)</label>
                        <input type="number" value="${r.duration_ms || 100}" data-id="${r.id}" class="relay-duration-input" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;">
                    </div>
                    <div>
                        <label style="font-size:0.7rem; color:#7a9bbf;">Off-delay (ms)</label>
                        <input type="number" value="${r.off_delay_ms || 50}" data-id="${r.id}" class="relay-offdelay-input" style="width:100%; padding:4px 8px; border-radius:6px; border:1px solid #dde6ef;">
                    </div>
                </div>
                <div style="display:flex; align-items:center; margin-top:8px; gap:12px;">
                    <label style="font-size:0.75rem; color:#7a9bbf; display:flex; align-items:center; gap:6px;">
                        <span>Enabled</span>
                        <input type="checkbox" ${r.enabled !== false ? 'checked' : ''} data-id="${r.id}" class="relay-enabled-check">
                    </label>
                    <span class="test-relay-btn" data-id="${r.id}" style="font-size:0.7rem; color:#7a9bbf;">${r.enabled === true ? 'Test for '+(RelayTestDuration/1000)+' seconds' : ''}</span>
                </div>
            </div>
        `).join('');
        document.querySelectorAll('.test-relay-btn')?.forEach(btn => {
            btn.addEventListener('click', function() {
                const id = parseInt(this.dataset.id);
                testRelay(id);
            });
        });
    } catch (e) {
        console.warn('Relay config load failed:', e);
    }
}

async function saveRelayConfig() {
    const inputs = document.querySelectorAll('.relay-name-input');
    const configs = [];
    inputs.forEach(inp => {
        const id = parseInt(inp.dataset.id);
        const name = inp.value;
        const enabled = document.querySelector(`.relay-enabled-check[data-id="${id}"]`)?.checked || false;
        const duration = parseInt(document.querySelector(`.relay-duration-input[data-id="${id}"]`)?.value) || 100;
        const offDelay = parseInt(document.querySelector(`.relay-offdelay-input[data-id="${id}"]`)?.value) || 50;
        configs.push({ id, name, enabled, duration_ms: duration, off_delay_ms: offDelay });
    });
    try {
        const result = await api.post('/api/relays/config', { relays: configs });
        alert(result.message || '✅ Relay configuration saved');
        await loadRelayConfig();
    } catch (e) {
        alert('❌ Failed to save: ' + e.message);
    }
}

async function testRelay(relayId) {
    try {
        const result = await api.post('/api/relays/trigger?relay=' + relayId + '&duration=' + RelayTestDuration);
    } catch (e) {
        alert('❌ Failed to trigger: ' + e.message);
    }
}

// ============================================================
// MODBUS Map
// ============================================================
async function loadModbusMap() {
    try {
        const data = await api.get('/api/modbus/map');
        const entries = data.entries || [];
        const body = document.getElementById('modbus-map-body');
        if (!body) return;
        body.innerHTML = entries.map(e => `
            <tr>
                <td>0x${e.address.toString(16).toUpperCase().padStart(4, '0')}</td>
                <td>${e.type}</td>
                <td>${e.description}</td>
                <td>${e.access}</td>
                <td><button class="btn btn-primary edit-modbus" data-index="${e.index}" data-address="${e.address}" style="padding:2px 12px; font-size:0.7rem;">✏️ Edit</button></td>
            </tr>
        `).join('');
        document.querySelectorAll('.edit-modbus').forEach(btn => {
            btn.addEventListener('click', async function() {
                const index = parseInt(this.dataset.index);
                const currentAddr = parseInt(this.dataset.address);
                const newAddr = prompt(`Enter new MODBUS address for entry ${index}:`, `0x${currentAddr.toString(16).toUpperCase().padStart(4, '0')}`);
                if (newAddr !== null) {
                    const addr = parseInt(newAddr);
                    if (!isNaN(addr) && addr >= 0 && addr <= 0xFFFF) {
                        await updateModbusAddress(index, addr);
                    } else {
                        alert('Invalid address. Enter a value between 0 and 65535.');
                    }
                }
            });
        });
    } catch (e) {
        console.warn('MODBUS map load failed:', e);
    }
}

async function updateModbusAddress(index, address) {
    try {
        const data = await api.get('/api/modbus/map');
        const entries = data.entries || [];
        if (entries[index]) entries[index].address = address;
        const result = await api.post('/api/modbus/map', { entries });
        if (result.success) {
            alert('✅ MODBUS address updated');
            await loadModbusMap();
        } else {
            alert('❌ Update failed');
        }
    } catch (e) {
        alert('❌ Error: ' + e.message);
    }
}

async function resetModbusMap() {
    if (!confirm('Reset MODBUS map to defaults?')) return;
    const defaults = [
        { index: 0, address: 0x0000, type: 'Sensor', description: 'EC', access: 'RO' },
        { index: 1, address: 0x0001, type: 'Sensor', description: 'pH', access: 'RO' },
        { index: 2, address: 0x0002, type: 'Sensor', description: 'Potassium', access: 'RO' },
        { index: 3, address: 0x0003, type: 'Sensor', description: 'Magnesium', access: 'RO' },
        { index: 4, address: 0x0004, type: 'Sensor', description: 'Iron', access: 'RO' },
        { index: 5, address: 0x0005, type: 'Sensor', description: 'Phosphorus', access: 'RO' },
        { index: 6, address: 0x0008, type: 'Sensor', description: 'Temperature', access: 'RO' },
        { index: 7, address: 0x0009, type: 'Sensor', description: 'Humidity', access: 'RO' },
        { index: 8, address: 0x0100, type: 'Relay', description: 'Relay 0', access: 'RW' },
        { index: 9, address: 0x0101, type: 'Relay', description: 'Relay 1', access: 'RW' },
        { index: 10, address: 0x0102, type: 'Relay', description: 'Relay 2', access: 'RW' },
        { index: 11, address: 0x0103, type: 'Relay', description: 'Relay 3', access: 'RW' },
        { index: 12, address: 0x0104, type: 'Relay', description: 'Relay 4', access: 'RW' },
        { index: 13, address: 0x0105, type: 'Relay', description: 'Relay 5', access: 'RW' },
        { index: 14, address: 0x0106, type: 'Relay', description: 'Relay 6', access: 'RW' },
        { index: 15, address: 0x0107, type: 'Relay', description: 'Relay 7', access: 'RW' },
        { index: 16, address: 0x0108, type: 'Relay', description: 'Relay 8', access: 'RW' },
        { index: 17, address: 0x0109, type: 'Relay', description: 'Relay 9', access: 'RW' }
    ];
    try {
        const result = await api.post('/api/modbus/map', { entries: defaults });
        if (result.success) {
            alert('✅ MODBUS map reset');
            await loadModbusMap();
        }
    } catch (e) {
        alert('❌ Reset failed: ' + e.message);
    }
}

// ============================================================
// Load Log Files
// ============================================================
async function loadLogFiles() {
    try {
        const data = await api.get('/api/logs');
        const body = document.getElementById('log-file-body');
        if (!body) return;
        
        const logs = data.logs || [];
        if (logs.length === 0) {
            body.innerHTML = '<tr><td colspan="4" style="text-align:center; color:#7a9bbf;">No log files found</td></tr>';
            return;
        }
        
        body.innerHTML = logs.map(log => {
            const date = new Date(log.modified * 1000).toLocaleString();
            const size = formatFileSize(log.size);
            return `
                <tr>
                    <td>${log.name}</td>
                    <td>${size}</td>
                    <td>${date}</td>
                    <td>
                        <button onclick="viewLogFile('${log.name}')" class="btn btn-info" style="padding:2px 10px; font-size:0.7rem;">👁️ View</button>
                        <button class="btn btn-primary download-log" data-name="${log.name}" style="padding:2px 12px; font-size:0.7rem;">📥 Download</button>
                        <button class="btn btn-danger erase-log" data-name="${log.name}" style="padding:2px 12px; font-size:0.7rem;">🗑️ Erase</button>
                    </td>
                </tr>
            `;
        }).join('');
        
        body.addEventListener('click', function(e) {
            const downloadBtn = e.target.closest('.download-log');
            if (downloadBtn) {
                const name = downloadBtn.dataset.name;
                window.location.href = `/api/logs?name=${name}`;
                return;
            }
            
            const eraseBtn = e.target.closest('.erase-log');
            if (eraseBtn) {
                const name = eraseBtn.dataset.name;
                if (!confirm(`⚠️ Erase ${name}?`)) return;
                api.del(`/api/logs?name=${name}`).then(result => {
                    alert(result.message || '✅ Log file deleted');
                    loadLogFiles();
                }).catch(err => {
                    alert('❌ Failed to erase: ' + err.message);
                });
            }
        });
         // ============================================================
        // MODAL EVENT LISTENERS
        // ============================================================

		// Close modal with close button
		const closeBtn = document.getElementById('log-viewer-close');
		if (closeBtn) {
			closeBtn.addEventListener('click', closeLogViewer);
		}

		// Close modal with bottom close button
		const closeBtn2 = document.getElementById('log-viewer-close-btn');
		if (closeBtn2) {
			closeBtn2.addEventListener('click', closeLogViewer);
		}
		
		// Close modal by clicking outside
		const modal = document.getElementById('log-viewer-modal');
		if (modal) {
			modal.addEventListener('click', function(e) {
				if (e.target === this) {
					closeLogViewer();
				}
			});
		}
		
		// Download viewed log button
		const downloadBtn = document.getElementById('log-viewer-download');
		if (downloadBtn) {
			downloadBtn.addEventListener('click', downloadViewedLog);
		}
		
		// Keyboard shortcut: Escape to close
		document.addEventListener('keydown', function(e) {
			if (e.key === 'Escape') {
				const modal = document.getElementById('log-viewer-modal');
				if (modal && modal.style.display === 'flex') {
					closeLogViewer();
				}
			}
        });
    } catch (e) {
        console.warn('Log files load failed:', e);
        const body = document.getElementById('log-file-body');
        if (body) {
            body.innerHTML = '<tr><td colspan="4" style="color:#d32f2f;">❌ Failed to load logs</td></tr>';
        }
    }
}

async function eraseAllLogs() {
    if (!confirm('⚠️ Erase all log files?')) return;
    try {
        const result = await api.del('/api/logs/all');
        alert(result.message || '✅ All logs erased');
        await loadLogFiles();
    } catch (e) {
        alert('❌ Failed to erase: ' + e.message);
    }
}

let logViewerState = {
    filename: '',
    content: '',
    fileSize: 0,
    fileModified: ''
};

/**
 * Open log viewer modal with file content
 */
async function viewLogFile(filename) {
    const modal = document.getElementById('log-viewer-modal');
    const textElement = document.getElementById('log-viewer-text');
    const loadingElement = document.getElementById('log-viewer-loading');
    const errorElement = document.getElementById('log-viewer-error');
    const titleElement = document.getElementById('log-viewer-title');
    const infoElement = document.getElementById('log-viewer-info');
    const downloadBtn = document.getElementById('log-viewer-download');
    
    // Show modal
    modal.style.display = 'flex';
    
    // Reset states
    textElement.textContent = '';
    textElement.style.display = 'none';
    loadingElement.style.display = 'flex';
    errorElement.style.display = 'none';
    downloadBtn.disabled = true;
    
    // Update title
    titleElement.textContent = `📄 ${filename}`;
    
    try {
        // ✅ Fetch log content as plain text (not JSON)
        const response = await fetch(`/api/logs?name=${encodeURIComponent(filename)}`);
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        // ✅ Get content as text directly (not JSON)
        const content = await response.text();
        
        // Get file size from Content-Length header if available
        const contentLength = response.headers.get('Content-Length');
        const fileSize = contentLength ? parseInt(contentLength) : content.length;
        
        // Display content
        textElement.textContent = content;
        textElement.style.display = 'block';
        
        // Store state for download
        logViewerState = {
            filename: filename,
            content: content,
            fileSize: fileSize,
            fileModified: '' // We don't get this from text response
        };
        
        // Update info
        const lineCount = content.split('\n').length;
        const sizeDisplay = formatFileSize(fileSize);
        infoElement.textContent = `Size: ${sizeDisplay} | Lines: ${lineCount}`;
        
        // Enable download button
        downloadBtn.disabled = false;
        
    } catch (error) {
        console.error('Failed to view log:', error);
        errorElement.style.display = 'flex';
        const errorSpan = errorElement.querySelector('span');
        if (errorSpan) {
            errorSpan.textContent = `❌ Failed to load log: ${error.message}`;
        }
    } finally {
        loadingElement.style.display = 'none';
    }
}

/**
 * Download current viewed log file
 */
function downloadViewedLog() {
    if (!logViewerState.content) {
        showNotification('No log content to download', 'error');
        return;
    }
    
    // Create blob and download
    const blob = new Blob([logViewerState.content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = logViewerState.filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    showNotification(`Downloaded: ${logViewerState.filename}`, 'success');
}

/**
 * Close log viewer modal
 */
function closeLogViewer() {
    const modal = document.getElementById('log-viewer-modal');
    modal.style.display = 'none';
    
    // Clear content
    document.getElementById('log-viewer-text').textContent = '';
    document.getElementById('log-viewer-text').style.display = 'none';
    logViewerState.content = '';
}

/**
 * Format file size for display
 */
function formatFileSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// ============================================================
// NTP Functions
// ============================================================
async function loadNtpStatus() {
    try {
        const data = await api.get('/api/status');
        const statusElem = document.getElementById('ntp-status');
        const timeElem = document.getElementById('ntp-current-time');
        if (statusElem) {
            const synced = data.ntp_synced === true;
            statusElem.textContent = synced ? '✅ Synced' : '⏳ Waiting for sync...';
            statusElem.style.color = synced ? '#2e7d32' : '#f9a825';
        }
        if (timeElem) {
            timeElem.textContent = new Date().toLocaleString();
        }
    } catch (e) {
        console.warn('Failed to load NTP status:', e);
        const statusElem = document.getElementById('ntp-status');
        if (statusElem) {
            statusElem.textContent = '❌ Error loading status';
            statusElem.style.color = '#d32f2f';
        }
    }
}

async function setManualTime() {
    const input = document.getElementById('manual-time');
    if (!input || !input.value) {
        alert('Please select a date and time');
        return;
    }
    const timestamp = Math.floor(new Date(input.value).getTime() / 1000);
    if (isNaN(timestamp)) {
        alert('Invalid date/time');
        return;
    }
    try {
        const result = await api.post('/api/ntp/manual', { timestamp });
        if (result.success) {
            alert('✅ Manual time set');
            await loadNtpStatus();
        } else {
            alert('❌ Failed: ' + (result.message || 'Unknown error'));
        }
    } catch (e) {
        alert('❌ Error: ' + e.message);
    }
}

async function forceNtpSync() {
    try {
        const result = await api.post('/api/ntp/sync');
        alert(result.message || 'NTP sync initiated');
        setTimeout(loadNtpStatus, 3000);
    } catch (e) {
        alert('Failed to sync: ' + e.message);
    }
}

// ============================================================
// WiFi Functions (Updated with polished UI)
// ============================================================

// Load WiFi Status
async function loadWifiStatus() {
    try {
        const data = await api.get('/api/wifi/status');
//        console.log('WiFi status:', data);
        
        // Current status
        document.getElementById('wifi-current-mode').textContent = data.mode || 'Unknown';
        document.getElementById('wifi-current-ssid').textContent = data.ssid || '--';
        document.getElementById('wifi-current-ip').textContent = data.ip || '--';
        document.getElementById('wifi-current-mac').textContent = data.mac || '--';
        
        // RSSI with quality indicator
        const rssi = data.rssi || 0;
        let rssiText = rssi + ' dBm';
        if (rssi > -50) rssiText += ' (Excellent)';
        else if (rssi > -60) rssiText += ' (Good)';
        else if (rssi > -70) rssiText += ' (Fair)';
        else if (rssi > -80) rssiText += ' (Weak)';
        else rssiText += ' (Very Weak)';
        document.getElementById('wifi-current-rssi').textContent = rssiText;
        
        // Status
        const statusElem = document.getElementById('wifi-current-status');
        if (data.connected) {
            statusElem.textContent = '✅ Connected';
            statusElem.style.color = '#2e7d32';
        } else if (data.mode === 'AP') {
            statusElem.textContent = '🔵 AP Mode (No STA connection)';
            statusElem.style.color = '#2b7be4';
        } else {
            statusElem.textContent = '⏳ Disconnected';
            statusElem.style.color = '#d32f2f';
        }
        
        // Mode radio buttons
        const modeRadios = document.querySelectorAll('input[name="wifi-mode-radio"]');
        modeRadios.forEach(function(radio) {
            radio.checked = parseInt(radio.value) === data.mode_id;
        });
        
        // AP settings
        const apSettings = document.getElementById('wifi-ap-settings');
        if (data.mode_id === 0 || data.mode_id === 2) {
            apSettings.style.display = 'block';
            document.getElementById('wifi-ap-ssid').textContent = data.ap_ssid || '--';
            document.getElementById('wifi-ap-password').textContent = data.ap_password || '--';
        } else {
            apSettings.style.display = 'none';
        }
        
        // WiFi SSID input
        const ssidInput = document.getElementById('wifi-ssid-input');
        if (ssidInput && data.ssid && data.mode_id === 1) {
            ssidInput.value = data.ssid;
        }
    } catch (e) {
        console.warn('Failed to load WiFi status:', e);
        // Set fallback values
        document.getElementById('wifi-current-mode').textContent = 'Unknown';
        document.getElementById('wifi-current-status').textContent = '❌ Error loading';
        document.getElementById('wifi-current-status').style.color = '#d32f2f';
    }
}

// WiFi Scan
async function wifiScan() {
    const scanBtn = document.getElementById('wifi-scan');
    const networkList = document.getElementById('wifi-network-list');
    const placeholder = document.getElementById('wifi-scan-placeholder');
    const scanCount = document.getElementById('wifi-scan-count');
    
    // Disable button during scan
    scanBtn.disabled = true;
    scanBtn.textContent = '⏳ Scanning...';
    
    try {
        const response = await fetch('/api/wifi/scan');
        if (!response.ok) throw new Error('Scan failed');
        
        const result = await response.json();
        
        // Clear existing list
        networkList.innerHTML = '';
        
        // Handle different response formats
        let networks = [];
        
        // Check if result is an array
        if (Array.isArray(result)) {
            networks = result;
        } 
        // Check if result has a 'networks' property (common API pattern)
        else if (result.networks && Array.isArray(result.networks)) {
            networks = result.networks;
        }
        // Check if result has a 'data' property
        else if (result.data && Array.isArray(result.data)) {
            networks = result.data;
        }
        // Check if result is an object with numeric keys (like {"0": {...}, "1": {...}})
        else if (typeof result === 'object' && result !== null) {
            // Try to convert object to array
            const keys = Object.keys(result);
            if (keys.length > 0 && !isNaN(keys[0])) {
                networks = Object.values(result);
            } else {
                // Maybe it's a single network object
                if (result.ssid || result.essid) {
                    networks = [result];
                }
            }
        }
        
        // If still no networks found, try to parse differently
        if (networks.length === 0 && typeof result === 'object' && result !== null) {
            // Try to find any array-like property
            for (const key in result) {
                if (Array.isArray(result[key])) {
                    networks = result[key];
                    break;
                }
            }
        }
        
        // Check if networks is actually an array
        if (!Array.isArray(networks)) {
            console.warn('Networks is not an array:', networks);
            placeholder.style.display = 'block';
            placeholder.textContent = '⚠️ Unexpected data format from server';
            scanCount.textContent = 'Error';
            return;
        }
        
        if (networks.length === 0) {
            placeholder.style.display = 'block';
            placeholder.textContent = '🔍 No networks found';
            scanCount.textContent = '0 networks';
            return;
        }
        
        placeholder.style.display = 'none';
        scanCount.textContent = `${networks.length} networks`;
        
        // Sort by signal strength (RSSI) - only if networks are objects with rssi
        if (networks.length > 0 && networks[0] && typeof networks[0] === 'object') {
            networks.sort((a, b) => {
                const rssiA = a.rssi || a.signal || a.strength || -100;
                const rssiB = b.rssi || b.signal || b.strength || -100;
                return rssiB - rssiA;
            });
        }
        
        networks.forEach(network => {
            const li = document.createElement('li');
            
            // Get SSID - handle different property names
            const ssid = network.ssid || network.essid || network.name || 'Hidden Network';
            
            // SSID with lock icon
            const ssidSpan = document.createElement('span');
            ssidSpan.className = 'ssid';
            ssidSpan.textContent = ssid || 'Hidden Network';
            
            // Signal strength - handle different property names
            const rssi = network.rssi || network.signal || network.strength || -100;
            
            // Signal bars
            let bars = '';
            if (rssi > -50) bars = '📶 Excellent';
            else if (rssi > -60) bars = '📶 Good';
            else if (rssi > -70) bars = '📶 Fair';
            else bars = '📶 Weak';
            
            // Lock icon - check auth mode
            const isSecured = network.auth_mode !== undefined ? network.auth_mode !== 0 : true;
            const lockIcon = isSecured ? '🔒' : '🔓';
            const lockClass = isSecured ? 'locked' : 'open';
            
            const signalSpan = document.createElement('span');
            signalSpan.className = 'signal';
            signalSpan.innerHTML = `
                <span class="bars">${bars}</span>
                <span class="${lockClass}">${lockIcon}</span>
            `;
            
            li.appendChild(ssidSpan);
            li.appendChild(signalSpan);
            
            // Click to select network
            li.addEventListener('click', function() {
                // Remove selected class from all
                document.querySelectorAll('#wifi-network-list li').forEach(el => {
                    el.classList.remove('selected');
                });
                this.classList.add('selected');
                document.getElementById('wifi-ssid-input').value = ssid || '';
                
                // Auto-fill password? Only if it's a known network with saved password
                // Otherwise user needs to enter it manually
                if (network.password) {
                    document.getElementById('wifi-password').value = network.password;
                }
            });
            
            networkList.appendChild(li);
        });
        
    } catch (error) {
        console.error('WiFi scan failed:', error);
        placeholder.style.display = 'block';
        placeholder.textContent = '❌ Scan failed: ' + error.message;
        scanCount.textContent = 'Error';
    } finally {
        scanBtn.disabled = false;
        scanBtn.textContent = '🔍 Scan';
    }
}

// Select Network (global)
window.selectNetwork = function(ssid) {
    const input = document.getElementById('wifi-ssid-input');
    if (input) {
        input.value = ssid;
        const pw = document.getElementById('wifi-password');
        if (pw) pw.focus();
    }
};

// WiFi Connect
async function wifiConnect() {
    const ssid = document.getElementById('wifi-ssid-input').value.trim();
    const password = document.getElementById('wifi-password').value.trim();
    const statusMsg = document.getElementById('wifi-status-message');
    
    if (!ssid) {
        statusMsg.style.display = 'block';
        statusMsg.style.background = '#f8d7da';
        statusMsg.style.color = '#721c24';
        statusMsg.textContent = '⚠️ Please enter or select an SSID';
        return;
    }
    
    statusMsg.style.display = 'block';
    statusMsg.style.background = '#fff3cd';
    statusMsg.style.color = '#856404';
    statusMsg.textContent = '⏳ Connecting...';
    
    try {
        const response = await fetch('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password })
        });
        
        const result = await response.json();
        
        if (response.ok && result.status === 'ok') {
            statusMsg.style.background = '#d4edda';
            statusMsg.style.color = '#155724';
            statusMsg.textContent = '✅ Connected successfully!';
            
            // Update current status
            document.getElementById('wifi-current-ssid').textContent = ssid;
            document.getElementById('wifi-current-status').textContent = 'Connected';
            document.getElementById('wifi-current-status').style.color = '#2e7d32';
        } else {
            throw new Error(result.message || 'Connection failed');
        }
    } catch (error) {
        console.error('WiFi connection failed:', error);
        statusMsg.style.background = '#f8d7da';
        statusMsg.style.color = '#721c24';
        statusMsg.textContent = '❌ Connection failed: ' + error.message;
    }
}

// WiFi Apply Mode
async function wifiApplyMode() {
    const modeRadios = document.querySelectorAll('input[name="wifi-mode-radio"]');
    let mode = -1;
    modeRadios.forEach(function(radio) {
        if (radio.checked) {
            mode = parseInt(radio.value);
        }
    });
    
    if (mode === -1) {
        alert('Please select a mode (AP, STA, or AP+STA)');
        return;
    }
    
    const statusMsg = document.getElementById('wifi-status-message');
    const notice = document.getElementById('wifi-mode-notice');
    
    if (statusMsg) {
        statusMsg.style.display = 'block';
        statusMsg.textContent = '⏳ Applying mode...';
        statusMsg.style.backgroundColor = '#fff3cd';
        statusMsg.style.color = '#856404';
    }
    
    try {
        const result = await api.post('/api/wifi/connect', { mode: mode });
        if (result.success) {
            if (mode === 0) {
                alert('✅ ' + result.message + '\n\nSSID: ' + result.ssid + '\nPassword: ' + result.password + '\nIP: ' + result.ip);
            } else {
                alert('✅ ' + result.message);
            }
            if (notice) {
                notice.style.display = 'block';
                setTimeout(function() { notice.style.display = 'none'; }, 5000);
            }
            if (statusMsg) {
                statusMsg.textContent = '✅ Mode applied: ' + (mode === 0 ? 'AP' : mode === 1 ? 'STA' : 'AP+STA');
                statusMsg.style.backgroundColor = '#d4edda';
                statusMsg.style.color = '#155724';
                setTimeout(function() { statusMsg.style.display = 'none'; }, 3000);
            }
            await loadWifiStatus();
        } else {
            if (statusMsg) {
                statusMsg.textContent = '❌ ' + result.message;
                statusMsg.style.backgroundColor = '#f8d7da';
                statusMsg.style.color = '#721c24';
            }
            alert('❌ ' + result.message);
        }
    } catch (e) {
        if (statusMsg) {
            statusMsg.textContent = '❌ Failed to apply mode: ' + e.message;
            statusMsg.style.backgroundColor = '#f8d7da';
            statusMsg.style.color = '#721c24';
        }
        alert('Failed to apply mode: ' + e.message);
    }
}

// ============================================================
// Forget WiFi Network (Clear saved SSID and password)
// ============================================================
async function wifiForget() {
    // Confirm with user
    if (!confirm('⚠️ This will clear the saved WiFi credentials and switch to AP mode.\n\nThe system will restart to apply changes.\n\nContinue?')) {
        return;
    }
    
    try {
        showNotification('Forgetting WiFi network...', 'info');
        
        const result = await api.post('/api/wifi/forget');
        console.log('Forget result:', result);
        
        if (result.success) {
            showNotification('WiFi credentials cleared. System restarting...', 'success');
            
            // Update UI to show AP mode
            const dot = document.getElementById('wifi-status-dot');
            const text = document.getElementById('wifi-status-text');
            const ssidDisplay = document.getElementById('wifi-ssid-display');
            
            if (dot) dot.className = 'status-dot disconnected';
            if (text) text.textContent = 'Mode: Access Point (AP)';
            if (ssidDisplay) ssidDisplay.textContent = 'AP: WQMS-XXXX';
            
            // Clear the SSID and password fields
            document.getElementById('wifi-ssid').value = '';
            document.getElementById('wifi-password').value = '';
            
            // The ESP32 will restart automatically
            // Show countdown and reload after 5 seconds
            let countdown = 5;
            const interval = setInterval(() => {
                showNotification(`Rebooting in ${countdown} seconds...`, 'info');
                countdown--;
                if (countdown < 0) {
                    clearInterval(interval);
                    window.location.reload();
                }
            }, 1000);
            
        } else {
            showNotification(`Failed to forget network: ${result.message || 'Unknown error'}`, 'error');
        }
    } catch (e) {
        console.warn('Forget WiFi failed:', e);
        showNotification('Error forgetting network: ' + e.message, 'error');
    }
}

// ============================================================
// Download All Logs
// ============================================================
async function downloadAllLogs() {
    try {
        const data = await api.get('/api/logs');
        const logs = data.logs || [];
        if (logs.length === 0) {
            alert('No log files to download');
            return;
        }
        let downloaded = 0;
        for (const log of logs) {
            const response = await fetch(`/api/logs?name=${log.name}`);
            if (response.ok) {
                const blob = await response.blob();
                const url = window.URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = log.name;
                a.click();
                window.URL.revokeObjectURL(url);
                downloaded++;
            }
        }
        alert(`✅ Downloaded ${downloaded} log files`);
    } catch (e) {
        alert('❌ Failed to download logs: ' + e.message);
    }
}

async function loadEmailConfig() {
    try {
        const response = await fetch('/api/email/config');
        if (!response.ok) throw new Error('Failed to load email config');
        
        const config = await response.json();
        
        // Populate form fields (matching your existing pattern)
        document.getElementById('email_enabled').checked = config.enabled || false;
        document.getElementById('email_smtp_server').value = config.smtp_server || '';
        document.getElementById('email_smtp_port').value = config.smtp_port || 587;
        document.getElementById('email_username').value = config.username || '';
        document.getElementById('email_password').value = ''; // Never show stored password
        document.getElementById('email_from').value = config.from_email || '';
        document.getElementById('email_to').value = config.to_emails || '';
        document.getElementById('email_tls').checked = config.use_tls !== undefined ? config.use_tls : true;
        
        // Update UI based on enabled state
        updateEmailUIState();
        
    } catch (error) {
        console.error('Failed to load email config:', error);
        showNotification('Failed to load email configuration', 'error');
    }
}

/**
 * Save email configuration to server
 */
async function saveEmailConfig() {
    const enabled = document.getElementById('email_enabled').checked;
    
    // Gather config data
    const config = {
        enabled: enabled,
        smtp_server: document.getElementById('email_smtp_server').value.trim(),
        smtp_port: parseInt(document.getElementById('email_smtp_port').value) || 587,
        username: document.getElementById('email_username').value.trim(),
        password: document.getElementById('email_password').value.trim(),
        from_email: document.getElementById('email_from').value.trim(),
        to_emails: document.getElementById('email_to').value.trim(),
        use_tls: document.getElementById('email_tls').checked
    };
    
    // Validate if enabled
    if (enabled) {
        const required = [
            { field: 'smtp_server', label: 'SMTP Server' },
            { field: 'username', label: 'Username' },
            { field: 'password', label: 'Password' },
            { field: 'from_email', label: 'From Address' },
            { field: 'to_emails', label: 'To Address(es)' }
        ];
        
        const missing = required.filter(r => !config[r.field]);
        if (missing.length > 0) {
            showNotification(`Please fill in: ${missing.map(m => m.label).join(', ')}`, 'error');
            return;
        }
        
        // Validate email formats
        if (!isValidEmail(config.from_email)) {
            showNotification('Invalid "From" email address format', 'error');
            return;
        }
        
        const toEmails = config.to_emails.split(',').map(e => e.trim());
        const invalidEmails = toEmails.filter(e => e && !isValidEmail(e));
        if (invalidEmails.length > 0) {
            showNotification(`Invalid "To" email(s): ${invalidEmails.join(', ')}`, 'error');
            return;
        }
    }
    
    // Disable save button
    const saveBtn = document.querySelector('#email-section .btn-primary');
    if (saveBtn) {
        saveBtn.disabled = true;
        saveBtn.textContent = 'Saving...';
    }
    
    try {
        const response = await fetch('/api/email/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.message || 'Save failed');
        }
        
        showNotification('Email configuration saved successfully', 'success');
        
        // Clear password field if it was set (security)
        if (document.getElementById('email_password').value) {
            document.getElementById('email_password').value = '';
        }
        
    } catch (error) {
        console.error('Failed to save email config:', error);
        showNotification('Failed to save: ' + error.message, 'error');
    } finally {
        if (saveBtn) {
            saveBtn.disabled = false;
            saveBtn.textContent = 'Save Email Settings';
        }
    }
}

/**
 * Send test email
 */
async function testEmailConfig() {
    const enabled = document.getElementById('email_enabled').checked;
    if (!enabled) {
        showNotification('Email is disabled. Enable it first.', 'warning');
        return;
    }
    
    // Quick validation
//    const smtp_server = document.getElementById('email_smtp_server').value.trim();
//    const username = document.getElementById('email_username').value.trim();
//    const password = document.getElementById('email_password').value.trim();
//    const from_email = document.getElementById('email_from').value.trim();
//    const to_emails = document.getElementById('email_to').value.trim();
    
//    if (!smtp_server || !username || !password || !from_email || !to_emails) {
//        showNotification('Please fill in all email fields before testing', 'error');
//        return;
//    }
    
    // Disable test button
    const testBtn = document.querySelector('#email-section .btn-secondary');
    if (testBtn) {
        testBtn.disabled = true;
        testBtn.textContent = 'Sending...';
    }
    
    try {
        const response = await fetch('/api/email/test', {
            method: 'POST'
        });
        
        const result = await response.json();
        
        if (response.ok && result.status === 'ok') {
            showNotification('Test email sent! Check your inbox.', 'success');
        } else {
            showNotification('Failed: ' + (result.message || 'Unknown error'), 'error');
        }
    } catch (error) {
        console.error('Test email failed:', error);
        showNotification('Network error sending test email', 'error');
    } finally {
        if (testBtn) {
            testBtn.disabled = false;
            testBtn.textContent = 'Send Test Email';
        }
    }
}

/**
 * Update UI based on email enabled state
 */
function updateEmailUIState() {
    const enabled = document.getElementById('email_enabled').checked;
    const fields = [
        'email_smtp_server',
        'email_smtp_port',
        'email_username',
        'email_password',
        'email_from',
        'email_to',
        'email_tls'
    ];
    
    fields.forEach(id => {
        const el = document.getElementById(id);
        if (el) {
            el.disabled = !enabled;
            el.style.opacity = enabled ? '1' : '0.6';
        }
    });
    
    // Update buttons
    const saveBtn = document.querySelector('#email-section .btn-primary');
    const testBtn = document.querySelector('#email-section .btn-secondary');
    if (saveBtn) saveBtn.disabled = !enabled;
    if (testBtn) testBtn.disabled = !enabled;
}

/**
 * Validate email format
 */
function isValidEmail(email) {
    return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
}

/**
 * Handle email enable/disable toggle
 */
function setupEmailToggle() {
    const toggle = document.getElementById('email_enabled');
    if (toggle) {
        toggle.addEventListener('change', updateEmailUIState);
    }
}

/**
 * Handle email input validation
 */
function setupEmailValidation() {
    const fields = [
        'email_smtp_server',
        'email_username',
        'email_password',
        'email_from',
        'email_to'
    ];
    
    fields.forEach(id => {
        const input = document.getElementById(id);
        if (input) {
            input.addEventListener('blur', function() {
                if (document.getElementById('email_enabled').checked) {
                    validateEmailField(this);
                }
            });
        }
    });
    
    // Port validation
    const portInput = document.getElementById('email_smtp_port');
    if (portInput) {
        portInput.addEventListener('input', function() {
            this.value = this.value.replace(/[^0-9]/g, '');
            if (this.value && (parseInt(this.value) < 1 || parseInt(this.value) > 65535)) {
                this.style.borderColor = '#dc3545';
            } else {
                this.style.borderColor = '';
            }
        });
    }
}

/**
 * Validate individual email field
 */
function validateEmailField(input) {
    if (!input) return;
    
    const id = input.id;
    let isValid = true;
    
    switch(id) {
        case 'email_smtp_server':
            if (input.value.trim() && !input.value.match(/^[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/)) {
                isValid = false;
            }
            break;
        case 'email_from':
            if (input.value.trim() && !isValidEmail(input.value.trim())) {
                isValid = false;
            }
            break;
        case 'email_to':
            if (input.value.trim()) {
                const emails = input.value.split(',').map(e => e.trim());
                const invalid = emails.filter(e => e && !isValidEmail(e));
                if (invalid.length > 0) isValid = false;
            }
            break;
    }
    
    input.style.borderColor = isValid ? '' : '#dc3545';
}

/**
 * Reset email form to saved config
 */
function resetEmailForm() {
    if (confirm('Reset email form to saved configuration?')) {
        loadEmailConfig();
        showNotification('Email form reset', 'info');
    }
}

/**
 * Clear email fields
 */
function clearEmailFields() {
    if (confirm('Clear all email fields? (This will not save changes)')) {
        const fields = [
            'email_smtp_server',
            'email_smtp_port',
            'email_username',
            'email_password',
            'email_from',
            'email_to'
        ];
        fields.forEach(id => {
            const el = document.getElementById(id);
            if (el) el.value = '';
        });
        document.getElementById('email_tls').checked = true;
        document.getElementById('email_enabled').checked = false;
        updateEmailUIState();
        showNotification('Email fields cleared', 'info');
    }
}

// ============================================
// ADC GPIO MAPPING
// ============================================

/**
 * Fetch ADC to GPIO pin mapping from server
 */
async function fetchAdcPinMapping() {
    try {
        const response = await fetch('/api/adc/pins');
        if (!response.ok) throw new Error('Failed to fetch ADC mapping');
        const mapping = await response.json();
        return mapping;
    } catch (error) {
        console.error('Failed to fetch ADC mapping:', error);
        return null;
    }
}

// ============================================================
// Initialize Configuration
// ============================================================
async function initConfiguration() {
    await updateHeader();
    await loadSensorConfig();
    await loadRelayConfig();
    await loadModbusMap();
    await loadLogFiles();
    await loadWifiStatus();
    await loadNtpStatus();
    await loadEmailConfig();
    await setupEmailToggle();
    await setupEmailValidation();
    initTabs();

    try {
        const config = await api.get('/api/config');
        const fields = {
            'sys-name': config.system_name || 'WQMS-System',
            'sys-location': config.system_location || 'Unknown',
            'sys-timezone': config.timezone || 'EET-3',
            'sample-interval': config.sample_interval_ms || 1000,
            'modbus-interval': config.modbus_interval_ms || 1000,
            'integration-url': config.integration_url || '',
            'integration-interval': config.integration_interval_sec || 60,
            'automation-interval': config.automation_interval_sec || 60,
            'ntp-servers': config.ntp_servers || '',
            'ntp-timezone': config.timezone || 'EET-3',
            'wifi-ssid-input': config.wifi_ssid || ''
        };
        Object.keys(fields).forEach(id => {
            const el = document.getElementById(id);
            if (el) el.value = fields[id];
        });
    } catch (e) {
        console.warn('Failed to load config:', e);
    }
    
    // Save config event
    document.getElementById('save-config')?.addEventListener('click', async function() {
        const config = {
            system_name: document.getElementById('sys-name').value,
            system_location: document.getElementById('sys-location').value,
            timezone: document.getElementById('sys-timezone').value,
            sample_interval_ms: parseInt(document.getElementById('sample-interval').value) || 1000,
            modbus_interval_ms: parseInt(document.getElementById('modbus-interval').value) || 1000,
            integration_url: document.getElementById('integration-url').value,
            integration_interval_sec: parseInt(document.getElementById('integration-interval').value) || 60,
            automation_interval_sec: document.getElementById('automation-interval').value || 60,
            ntp_servers: document.getElementById('ntp-servers') ? document.getElementById('ntp-servers').value : '',
            modbus_interval_ms: parseInt(document.getElementById('modbus-interval-2').value) || 1000
        };
        const result = await api.post('/api/config', config);
        alert(result.message || 'Configuration saved');
        await updateHeader();
    });
    
    document.getElementById('reset-config')?.addEventListener('click', function() {
        if (confirm('Reset to factory defaults?')) {
            api.post('/api/config/reset').then(() => {
                alert('Factory reset complete, system will restart');
            });
        }
    });
    
    // WiFi buttons
    document.getElementById('wifi-apply-mode')?.addEventListener('click', wifiApplyMode);
    document.getElementById('wifi-scan')?.addEventListener('click', wifiScan);
    document.getElementById('wifi-connect')?.addEventListener('click', wifiConnect);
    document.getElementById('wifi-forget')?.addEventListener('click', wifiForget);
    //NTP buttons
    document.getElementById('ntp-sync-now')?.addEventListener('click', forceNtpSync);
    document.getElementById('set-manual-time')?.addEventListener('click', setManualTime);
    //MODBUS buttom
    document.getElementById('modbus-reset-map')?.addEventListener('click', resetModbusMap);
    // LOG buttons
    document.getElementById('download-all-logs')?.addEventListener('click', downloadAllLogs);
    document.getElementById('erase-all-logs')?.addEventListener('click', eraseAllLogs);
    //Sensor & Relay buttons
    document.getElementById('save-sensor-config')?.addEventListener('click', saveSensorConfig);
    document.getElementById('save-relay-config')?.addEventListener('click', saveRelayConfig);

    setInterval(updateHeader, REFRESH_INTERVAL);
}