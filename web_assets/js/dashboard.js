// js/dashboard.js
// Dashboard page - loaded only when needed


// ============================================================
// Dashboard - Sensor Cards
// ============================================================
async function loadSensors() {
    try {
        const [readingsData, configData] = await Promise.all([
            api.get('/api/sensors'),
            api.get('/api/sensors/config')
        ]);
        
        const readings = readingsData.sensors || [];
        const configs = configData.sensors || [];
        
        const sensors = configs.map(cfg => {
            const reading = readings.find(r => r.id === cfg.id);
            return {
                id: cfg.id,
                name: cfg.name || 'Sensor ' + cfg.id,
                enabled: cfg.enabled !== false,
                gpio_pin: cfg.gpio_pin || '--',
                value: reading ? reading.value : 0,
                raw_adc: reading ? reading.raw_adc : 0,
                status: reading ? reading.status : 0,
                quality: reading ? reading.quality : 0
            };
        });
        
        renderSensorCards(sensors);
    } catch (e) {
        console.warn('Sensor load failed:', e);
    }
}

function renderSensorCards(sensors) {
    const grid = document.getElementById('sensor-grid');
    if (!grid) return;
    
    if (!sensors || sensors.length === 0) {
        grid.innerHTML = '<p style="color:#7a9bbf;">No sensors configured</p>';
        return;
    }
    
    grid.innerHTML = sensors.map(s => {
        const isEnabled = s.enabled;
        const isOk = isEnabled && s.status === 0;
        const isError = isEnabled && s.status !== 0 && s.status !== 3;
        const isDisabled = !isEnabled;
        
        let statusLabel = '⊘';
        let statusClass = 'status-disabled';
        if (isDisabled) {
            statusLabel = '⊘';
            statusClass = 'status-disabled';
        } else if (isOk) {
            statusLabel = '✅';
            statusClass = 'status-ok';
        } else if (isError) {
            statusLabel = '⚠️';
            statusClass = 'status-error';
        }
        
        return `
            <div class="card">
                <div class="name">${s.name || 'Sensor ' + s.id}</div>
                <div class="value">${isDisabled ? '--' : (s.value || 0).toFixed(2)}</div>
                <div class="meta">
                    <span class="${statusClass}">${statusLabel}</span>
                    <span>ID: ${s.id}</span>
                    <span>PIN: ${s.gpio_pin}</span>
                    ${isDisabled ? '<span style="color:#9e9e9e;">Disabled</span>' : ''}
                </div>
            </div>
        `;
    }).join('');
}

// ============================================================
// Dashboard - Relay Cards
// ============================================================
async function loadRelays() {
    try {
        const data = await api.get('/api/relays');
        const grid = document.getElementById('relay-grid');
        if (!grid) return;
        const relays = data.relays || [];
        if (relays.length === 0) {
            grid.innerHTML = '<p style="color:#7a9bbf;">No relays configured</p>';
            return;
        }
        grid.innerHTML = relays.map(r => {
            const stateLabel = r.state === RELAY_STATE_ACTIVE ? '🔴' : r.state === RELAY_STATE_COOLDOWN ? '🟡' : '⚪';
            const stateText = r.state === RELAY_STATE_ACTIVE ? 'ON' : r.state === RELAY_STATE_COOLDOWN ? 'COOLDOWN' : 'OFF';
            return `
                <div class="card">
                    <div class="name">${r.name || 'Relay ' + r.id}</div>
                    <div class="value">${stateLabel} ${stateText}</div>
                    <div class="meta">
                        <span>${r.active ? 'Active' : 'Idle'}</span>
                        <span>${r.remaining ? r.remaining + 's' : ''}</span>
                        <span>PIN: ${r.gpio_pin || '--'}</span>
                    </div>
                </div>
            `;
        }).join('');
    } catch (e) {
        console.warn('Relay load failed:', e);
    }
}

// ============================================================
// Initialize Dashboard
// ============================================================
async function initDashboard() {
    await updateHeader();
    await loadSensors();
    await loadRelays();
    setInterval(async () => {
        await updateHeader();
        await loadSensors();
        await loadRelays();
    }, REFRESH_INTERVAL);
}