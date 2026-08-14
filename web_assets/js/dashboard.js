// ============================================================
// DASHBOARD MANAGER
// ============================================================

// Constants from core.js
// RELAY_STATE_ACTIVE = 0, RELAY_STATE_IDLE = 1, RELAY_STATE_COOLDOWN = 2

function getUnitSymbol(unitId) {
    const UNIT_SYMBOLS = {
        0: '',      // None
        1: '°C',    // Celsius
        2: 'pH',    // pH
        3: 'mV',    // Millivolt
        4: 'mg/L',  // mg/L
        5: 'µS/cm', // µS/cm
        6: '%',     // Percent
        7: 'NTU',   // NTU
        8: 'ppm',   // PPM
        9: 'µg/L'   // µg/L
    };
    return UNIT_SYMBOLS[unitId] || '';
}

const CONTROLDEVICE_DEFINITIONS = [
    { id: 0, label: 'Pump'},
    { id: 1, label: 'Valve'}
];

// ============================================================
// SENSOR CARD RENDERER WITH SCADA GAUGE
// ============================================================
function renderSCADAGauge(value, unit, safeMin, safeMax, isDisabled, label) {
    const startAngle = 140;
    const endAngle = 340;
    const sweepAngle = 200;
    
    let displayValue = isDisabled ? '--' : (value || 0).toFixed(1);
    let gaugeColor = '#4a4a5a';
    let pct = 50;
    
    if (!isDisabled && value !== undefined && value !== null && safeMin !== undefined && safeMax !== undefined) {
        if (safeMax > safeMin) {
            pct = ((value - safeMin) / (safeMax - safeMin)) * 100;
        }
        pct = Math.min(100, Math.max(0, pct));
        
        if (value < safeMin || value > safeMax) {
            gaugeColor = '#ff1744';
        } else {
            const range = safeMax - safeMin;
            const lowWarning = safeMin + (range * 0.1);
            const highWarning = safeMax - (range * 0.1);
            
            if (value < lowWarning || value > highWarning) {
                gaugeColor = '#ffd740';
            } else {
                gaugeColor = '#00e676';
            }
        }
    } else if (isDisabled) {
        gaugeColor = '#4a4a5a';
    } else {
        gaugeColor = '#2b7be4';
    }
    
    const angleDeg = startAngle - (pct / 100) * sweepAngle;
    const angleRad = (angleDeg * Math.PI) / 180;
    const radius = 45;
    const circumference = (sweepAngle / 360) * 2 * Math.PI * radius;
    const offset = circumference * (1 - pct/100);
    
    let ticks = '';
    const numTicks = 11;
    for (let i = 0; i <= numTicks; i++) {
        const pctPos = i / numTicks;
        const tickAngleDeg = startAngle - pctPos * sweepAngle;
        const tickAngleRad = (tickAngleDeg * Math.PI) / 180;
        const innerR = 52;
        const outerR = i % 5 === 0 ? 58 : 55;
        const x1 = 60 + innerR * Math.cos(tickAngleRad);
        const y1 = 60 + innerR * Math.sin(tickAngleRad);
        const x2 = 60 + outerR * Math.cos(tickAngleRad);
        const y2 = 60 + outerR * Math.sin(tickAngleRad);
        const isMajor = i % 5 === 0;
        ticks += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${isDisabled ? '#4a4a5a' : '#8899bb'}" stroke-width="${isMajor ? 2 : 1}"/>`;
    }
    
    const startAngleRad = (startAngle * Math.PI) / 180;
    const endAngleRad = (endAngle * Math.PI) / 180;
    const startX = 60 + radius * Math.cos(startAngleRad);
    const startY = 60 + radius * Math.sin(startAngleRad);
    const endX = 60 + radius * Math.cos(endAngleRad);
    const endY = 60 + radius * Math.sin(endAngleRad);
    const largeArcFlag = sweepAngle > 180 ? 1 : 0;
    const arcPath = `M ${startX} ${startY} A ${radius} ${radius} 0 ${largeArcFlag} 1 ${endX} ${endY}`;
    
    return `
        <div class="gauge-wrapper scada-gauge">
            <svg viewBox="0 0 120 120">
                <path d="${arcPath}" fill="none" stroke="#1a1a2e" stroke-width="10" stroke-linecap="round"/>
                <path d="${arcPath}" fill="none" stroke="${gaugeColor}" stroke-width="10" 
                      stroke-dasharray="${circumference}" stroke-dashoffset="${offset}" stroke-linecap="round"
                      style="transition: stroke-dashoffset 0.5s ease, stroke 0.3s ease;"/>
                ${ticks}
                <text x="60" y="65" text-anchor="middle" fill="#e0e8f0" font-size="25" font-weight="750">${displayValue}</text>
                <text x="60" y="84" text-anchor="middle" fill="#8899bb" font-size="16">${unit}</text>
            </svg>
        </div>
    `;
}

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
                quality: reading ? reading.quality : 0,
                unit: cfg.unit || 0,
                safe_min: cfg.safe_min !== undefined ? cfg.safe_min : 0,
                safe_max: cfg.safe_max !== undefined ? cfg.safe_max : 100
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
        
        const unitSymbol = getUnitSymbol(s.unit);
        const gaugeHTML = renderSCADAGauge(s.value, unitSymbol, s.safe_min, s.safe_max, isDisabled, s.name);
        
        return `
            <div class="card sensor-card" style="display:flex; align-items: center; justify-content: center;">
                <div class="card-name">${s.name || 'Sensor ' + s.id}</div>
                ${gaugeHTML}
                <div class="sensor-status">
                    <span class="${statusClass}">${statusLabel}</span>
                    <span class="sensor-id">ID: ${s.id}</span>
                    <span class="sensor-pin">PIN: ${s.gpio_pin}</span>
                    ${isDisabled ? '<span class="sensor-disabled">Disabled</span>' : ''}
                </div>
            </div>
        `;
    }).join('');
}

// ============================================================
// RELAY CARD RENDERER WITH ANIMATED PUMP/VALVE
// ============================================================

function renderPumpAnimation(isActive) {
    const spinClass = isActive ? 'spin' : '';
    return `
        <svg class="relay-icon-svg pump-svg ${isActive ? 'active' : ''}" viewBox="0 0 80 80">
            <defs>
                <style>
                    .pump-body { fill: none; stroke: currentColor; stroke-width: 2; }
                    .pump-impeller { fill: currentColor; transform-origin: 40px 40px; }
                    .pump-impeller.spinning { animation: spinPump 0.8s linear infinite; }
                    @keyframes spinPump {
                        from { transform: rotate(0deg); }
                        to { transform: rotate(360deg); }
                    }
                </style>
            </defs>
            <circle cx="40" cy="40" r="28" class="pump-body"/>
            <rect x="12" y="35" width="10" height="10" rx="2" class="pump-body"/>
            <rect x="58" y="35" width="10" height="10" rx="2" class="pump-body"/>
            <rect x="35" y="12" width="10" height="10" rx="2" class="pump-body"/>
            <rect x="35" y="58" width="10" height="10" rx="2" class="pump-body"/>
            <g class="pump-impeller ${isActive ? 'spinning' : ''}">
                <path d="M40 20 L44 36 L58 28 L48 40 L58 52 L44 44 L40 60 L36 44 L22 52 L32 40 L22 28 L36 36 Z" opacity="0.8"/>
                <circle cx="40" cy="40" r="6" fill="currentColor"/>
            </g>
            <circle cx="40" cy="40" r="3" fill="#fff" opacity="0.5"/>
        </svg>
    `;
}

function renderValveAnimation(isActive) {
    const turnClass = isActive ? 'turning' : '';
    return `
        <svg class="relay-icon-svg valve-svg ${isActive ? 'active' : ''}" viewBox="0 0 80 80">
            <defs>
                <style>
                    .valve-body { fill: none; stroke: currentColor; stroke-width: 2; }
                    .valve-handle { fill: currentColor; transform-origin: 40px 40px; }
                    .valve-handle.turning { animation: turnValve 1s ease-in-out infinite; }
                    @keyframes turnValve {
                        0%, 100% { transform: rotate(0deg); }
                        50% { transform: rotate(90deg); }
                    }
                </style>
            </defs>
            <rect x="10" y="30" width="60" height="20" rx="4" class="valve-body"/>
            <rect x="0" y="35" width="12" height="10" rx="2" class="valve-body"/>
            <rect x="68" y="35" width="12" height="10" rx="2" class="valve-body"/>
            <g class="valve-handle ${isActive ? 'turning' : ''}">
                <rect x="18" y="10" width="44" height="6" rx="3" fill="currentColor"/>
                <rect x="18" y="30" width="6" height="20" rx="2" fill="currentColor"/>
                <rect x="56" y="30" width="6" height="20" rx="2" fill="currentColor"/>
                <circle cx="40" cy="40" r="12" fill="none" stroke="currentColor" stroke-width="2"/>
                <circle cx="40" cy="40" r="4" fill="currentColor"/>
            </g>
            ${isActive ? `<circle cx="30" cy="40" r="3" fill="#4fc3f7" opacity="0.6"><animate attributeName="opacity" values="0.3;1;0.3" dur="1s" repeatCount="indefinite"/></circle>
            <circle cx="50" cy="40" r="3" fill="#4fc3f7" opacity="0.6"><animate attributeName="opacity" values="0.3;1;0.3" dur="1s" repeatCount="indefinite" begin="0.5s"/></circle>` : ''}
        </svg>
    `;
}

function renderRelayVisual(relayId, deviceType, isActive) {
    const type = deviceType || 0;
    const isPump = type === 0;
    const isValve = type === 1;
    
    let visualHTML = '';
    if (isPump) {
        visualHTML = `
            <div class="relay-visual-item pump ${isActive ? 'active' : ''}">
                ${renderPumpAnimation(isActive)}
                <span class="relay-icon-label">Pump</span>
                <span class="relay-status-dot ${isActive ? 'active' : ''}"></span>
            </div>
        `;
    } else if (isValve) {
        visualHTML = `
            <div class="relay-visual-item valve ${isActive ? 'active' : ''}">
                ${renderValveAnimation(isActive)}
                <span class="relay-icon-label">Valve</span>
                <span class="relay-status-dot ${isActive ? 'active' : ''}"></span>
            </div>
        `;
    } else {
        visualHTML = `
            <div class="relay-visual-item pump ${isActive ? 'active' : ''}">
                ${renderPumpAnimation(isActive)}
                <span class="relay-icon-label">${type}</span>
                <span class="relay-status-dot ${isActive ? 'active' : ''}"></span>
            </div>
        `;
    }
    
    return visualHTML;
}

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
        renderRelayCards(relays);
    } catch (e) {
        console.warn('Relay load failed:', e);
    }
}

function renderRelayCards(relays) {
    const grid = document.getElementById('relay-grid');
    if (!grid) return;
    
    grid.innerHTML = relays.map(r => {
        const isActive = r.state === RELAY_STATE_ACTIVE;
        const isCooldown = r.state === RELAY_STATE_COOLDOWN;
        
        const stateLabel = isActive ? '🔴' : isCooldown ? '🟡' : '⚪';
        const stateText = isActive ? 'ACTIVE' : isCooldown ? 'COOLDOWN' : 'IDLE';
        const stateClass = isActive ? 'relay-on' : isCooldown ? 'relay-cooldown' : 'relay-off';
        
        const deviceType = r.control_device || 0;
        const deviceLabel = CONTROLDEVICE_DEFINITIONS.find(device => device.id === deviceType).label;
        
        return `
            <div class="card relay-card">
                <div class="card-name">${r.name || 'Relay ' + r.id}</div>
                <div class="relay-visual-container">
                    ${renderRelayVisual(r.id, deviceType, isActive)}
                </div>
                <div class="relay-state">
                    <span class="relay-state-badge ${stateClass}">${stateLabel} ${stateText}</span>
                    ${isCooldown ? `<span class="cooldown-timer">⏱ ${r.remaining || 0}s</span>` : ''}
                </div>
                <div class="relay-meta">
                    <span>${deviceLabel}</span>
                    <span>${r.active ? 'Active' : 'Idle'}</span>
                    ${r.remaining && isCooldown ? `<span>${r.remaining}s</span>` : ''}
                    <span>PIN: ${r.gpio_pin || '--'}</span>
                </div>
            </div>
        `;
    }).join('');
}

// ============================================================
// GLOBAL INITIALIZATION
// ============================================================

async function initDashboard() {
    await updateHeader();
    await loadSensors();
    await loadRelays();
    setInterval(() => {
        loadSensors();
        loadRelays();
    }, REFRESH_INTERVAL || 5000);
}

// ============================================================
// DASHBOARD MANAGER (Chart functionality)
// ============================================================

class DashboardManager {
    constructor() {
        this.maxDataPoints = 4320;
        this.updateInterval = 30000;
        this.isUpdating = false;
        this.chart = null;
        this.historyData = [];
        this.selectedSensors = new Set();
        this.chartInitialized = false;
        this.chartVisible = false;
        this.chartUpdateInterval = null;
        
        this.sensorLabels = {};
        this.colors = {};
        this.sensorConfig = [];
        this.sensorMap = {};
        this.sensorNameMap = {};
        this.sensorColorMap = {};
        this.sensorKeyMap = {};
        this.sensorUnit = {};
        this.defaultColors = [
            '#FF6384', '#36A2EB', '#FFCE56', '#4BC0C0', '#9966FF',
            '#FF9F40', '#FF6384', '#36A2EB', '#FFCE56', '#4BC0C0'
        ];
        
        this.init();
    }

    init() {
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => this.setup());
        } else {
            this.setup();
        }
    }

    async setup() {
        if (typeof LightChart === 'undefined') {
            console.error('LightChart not defined - check chart.js load order');
            return;
        }
        
        await this.loadSensorConfig();
        this.initChart();
        this.setupSensorSelector();
        this.setupChartControls();
        this.setupChartToggle();
        this.updateStatus();

        const container = document.getElementById('chart-container');
        const selector = document.getElementById('sensor-selector');
        const btn = document.getElementById('chart-toggle-btn');
        
        if (container) container.classList.add('hidden');
        if (selector) selector.classList.add('hidden');
        if (btn) {
            btn.textContent = '📊 Show Chart';
            btn.classList.add('hidden');
        }

        window.dashboardManager = this;
    }

    setupChartToggle() {
        const btn = document.getElementById('chart-toggle-btn');
        if (!btn) return;
        btn.addEventListener('click', () => this.toggleChartVisibility());
    }

    async loadSensorConfig() {
        try {
            const response = await fetch('/api/history/config');
            if (!response.ok) throw new Error('Failed to load sensor config');
            
            const data = await response.json();
            if (data && data.sensors) {
                this.sensorConfig = data.sensors;
                this.sensorMap = {};
                this.sensorNameMap = {};
                this.sensorColorMap = {};
                this.sensorKeyMap = {};
                this.sensorLabels = {};
                this.colors = {};
                this.sensorUnit = {};
                this.selectedSensors = new Set();
                
                this.sensorConfig.forEach(sensor => {
                    const id = sensor.id;
                    const name = sensor.name || 'Sensor ' + (id + 1);
                    const color = sensor.color || this.defaultColors[id % this.defaultColors.length];
                    const key = name.replace(/ /g, '_');
                    
                    this.sensorMap[id] = { name, color, key };
                    this.sensorNameMap[id] = name;
                    this.sensorColorMap[id] = color;
                    this.sensorKeyMap[id] = key;
                    this.sensorLabels[key] = name;
                    this.colors[key] = color;
                    this.selectedSensors.add(key);
                    this.sensorUnit[key] = sensor.unit;
                });
            }
        } catch (error) {
            console.warn('Error loading sensor config:', error);
            this.loadDefaultConfig();
        }
    }

    loadDefaultConfig() {
        const defaultNames = ['Sensor 1', 'Sensor 2', 'Sensor 3', 'Sensor 4', 
                             'Sensor 5', 'Sensor 6', 'Sensor 7', 'Sensor 8'];
        
        this.sensorMap = {};
        this.sensorNameMap = {};
        this.sensorColorMap = {};
        this.sensorKeyMap = {};
        this.sensorLabels = {};
        this.colors = {};
        this.sensorUnit = {};
        this.selectedSensors = new Set();
        
        for (let i = 0; i < 8; i++) {
            const id = i;
            const name = defaultNames[i];
            const color = this.defaultColors[i % this.defaultColors.length];
            const key = name.replace(/ /g, '_');
            
            this.sensorMap[id] = { name, color, key };
            this.sensorNameMap[id] = name;
            this.sensorColorMap[id] = color;
            this.sensorKeyMap[id] = key;
            this.sensorLabels[key] = name;
            this.colors[key] = color;
            this.selectedSensors.add(key);
            this.sensorUnit[key] = 0;
        }
    }

    initChart() {
        const canvas = document.getElementById('sensor-history-chart');
        if (!canvas) return;

        try {
            const initialVisible = Array.from(this.selectedSensors);
            if (initialVisible.length === 0) {
                const defaultKey = 'Sensor_1';
                this.selectedSensors.add(defaultKey);
                initialVisible.push(defaultKey);
                this.sensorLabels[defaultKey] = 'Sensor 1';
                this.colors[defaultKey] = '#FF6384';
            }
            
            this.chart = new LightChart('sensor-history-chart', {
                maxDataPoints: this.maxDataPoints,
                initialVisible: initialVisible,
                colors: this.colors,
                labels: this.sensorLabels
            });
            
            this.chartInitialized = true;
            this.fetchHistory();
        } catch (e) {
            console.warn('Chart initialization failed:', e);
            this.chart = null;
            this.chartInitialized = false;
        }
    }

    setupSensorSelector() {
        const container = document.getElementById('sensor-selector');
        if (!container) return;
        container.innerHTML = '';
        
        if (!this.sensorConfig || this.sensorConfig.length === 0) return;
        
        this.sensorConfig.forEach(sensor => {
            const id = sensor.id;
            const name = sensor.name;
            const color = sensor.color || this.defaultColors[id % this.defaultColors.length];
            const key = name.replace(/ /g, '_');
            
            const label = document.createElement('label');
            label.style.cssText = `
                display: inline-flex;
                align-items: center;
                gap: 6px;
                padding: 4px 8px;
                cursor: pointer;
                font-size: 0.85rem;
                color: #c0d0e0;
                border-radius: 4px;
            `;
            
            const checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            checkbox.id = 'cb_' + key;
            checkbox.value = key;
            checkbox.checked = this.selectedSensors.has(key);
            
            checkbox.addEventListener('change', (e) => {
                const isChecked = e.target.checked;
                const sensorKey = e.target.value;
                
                if (isChecked) {
                    this.selectedSensors.add(sensorKey);
                } else {
                    this.selectedSensors.delete(sensorKey);
                }
                
                if (this.chart && this.chartInitialized) {
                    this.chart.setVisible(sensorKey, isChecked);
                }
            });
            
            const colorSpan = document.createElement('span');
            colorSpan.style.cssText = `
                display: inline-block;
                width: 12px;
                height: 12px;
                background: ${color};
                border-radius: 3px;
                flex-shrink: 0;
                border: 1px solid rgba(255,255,255,0.1);
            `;
            
            const nameSpan = document.createElement('span');
            nameSpan.textContent = name;
            
            label.appendChild(checkbox);
            label.appendChild(colorSpan);
            label.appendChild(nameSpan);
            container.appendChild(label);
        });
    }

    setupChartControls() {
        const downloadBtn = document.getElementById('btn-download-csv');
        if (downloadBtn) {
            downloadBtn.addEventListener('click', () => this.downloadCSV());
        }
    }

    // ============================================================
    // MODIFIED: fetchHistory with loading indicator
    // ============================================================
    async fetchHistory() {
        if (!this.chartVisible || this.isUpdating || !this.chartInitialized) return;
        this.isUpdating = true;
        
        const c = this.chart;
        
        // Start loading indicator
        if (c) c.setLoading(true, 'Fetching sensor history...', 0);
        
        try {
            // Progress: 10% - Request sent
            if (c) c.setLoadingProgress(10, 'Requesting data from server...');
            
            const response = await fetch('/api/history?limit=' + this.maxDataPoints);
            if (!response.ok) throw new Error('HTTP ' + response.status);
            
            // Progress: 30% - Data received, parsing
            if (c) c.setLoadingProgress(30, 'Parsing data...');
            
            const data = await response.json();
            
            // Progress: 50% - Data parsed, processing
            if (c) c.setLoadingProgress(50, 'Processing data...');
            
            if (data && data.entries && data.entries.length > 0) {
                const total = data.entries.length;
                const batchSize = Math.max(1, Math.floor(total / 15));
                this.historyData = [];
                
                // Process in batches to show progress
                for (let i = 0; i < total; i += batchSize) {
                    const batch = data.entries.slice(i, Math.min(i + batchSize, total));
                    batch.forEach(entry => {
                        const mapped = { timestamp: entry.timestamp, sensor_mask: entry.sensor_mask };
                        Object.keys(entry).forEach(key => {
                            if (key !== 'timestamp' && key !== 'sensor_mask') {
                                mapped[key] = entry[key];
                            }
                        });
                        this.historyData.push(mapped);
                    });
                    
                    const processed = Math.min(i + batchSize, total);
                    const progress = 50 + (processed / total) * 40;
                    if (c) c.setLoadingProgress(progress, 'Processing ' + processed + '/' + total + ' entries...');
                    
                    // Allow UI to update
                    await new Promise(resolve => setTimeout(resolve, 0));
                }
                
                // Progress: 90% - Almost done
                if (c) c.setLoadingProgress(90, 'Finalizing chart...');
                
                const dataKeys = Object.keys(this.historyData[0]).filter(
                    k => k !== 'timestamp' && k !== 'sensor_mask'
                );
                
                if (dataKeys.length > 0) {
                    const currentSelected = Array.from(this.selectedSensors);
                    const needsUpdate = dataKeys.some(key => !currentSelected.includes(key));
                    
                    if (needsUpdate) {
                        this.selectedSensors = new Set(dataKeys);
                    }
                }
                
                this.updateChart();
                this.updateChartFooter();
                
                // Done - hide loading
                if (c) c.setLoading(false);
            } else {
                if (c) c.setLoading(false);
                this.showNoDataMessage('No data available yet');
            }
        } catch (error) {
            console.error('Error fetching history:', error);
            if (c) c.setLoading(false);
            this.showNoDataMessage('Error loading data: ' + error.message);
        } finally {
            this.isUpdating = false;
        }
    }

    updateChart() {
        if (!this.chart || !this.historyData.length) return;

        const labels = this.historyData.map(entry => {
            const d = new Date(entry.timestamp * 1000);
            return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        });
        
        const data = {};
        const sensorKeys = Array.from(this.selectedSensors);
        
        if (sensorKeys.length === 0) {
            const availableKeys = Object.keys(this.historyData[0]).filter(
                k => k !== 'timestamp' && k !== 'sensor_mask'
            );
            if (availableKeys.length > 0) {
                this.selectedSensors = new Set(availableKeys);
                this.updateSensorCheckboxes();
                setTimeout(() => this.updateChart(), 10);
                return;
            }
        }
        
        sensorKeys.forEach(key => {
            data[key] = this.historyData.map(entry => {
                const value = entry[key];
                return (value !== undefined && value !== null && !isNaN(value)) ? value : null;
            });
        });
        
        Object.keys(data).forEach(key => {
            if (!this.chart.colors[key]) {
                const index = Object.keys(this.chart.colors).length;
                const defaultColors = ['#FF6384', '#36A2EB', '#FFCE56', '#4BC0C0', '#9966FF', '#FF9F40'];
                this.chart.colors[key] = defaultColors[index % defaultColors.length];
            }
            if (!this.chart.labels[key]) {
                this.chart.labels[key] = this.sensorLabels[key] || key;
            }
        });
        
        this.chart.setData(labels, data);
    }

    updateSensorCheckboxes() {
        const checkboxes = document.querySelectorAll('#sensor-selector input[type="checkbox"]');
        checkboxes.forEach(cb => {
            cb.checked = this.selectedSensors.has(cb.value);
        });
    }

    updateChartFooter() {
        if (!this.historyData || !this.historyData.length) return;
        
        const first = this.historyData[0];
        const last = this.historyData[this.historyData.length - 1];
        
        if (first && first.timestamp && last && last.timestamp) {
            const startDate = new Date(first.timestamp * 1000);
            const endDate = new Date(last.timestamp * 1000);
            const el = document.getElementById('chart-update-time');
            if (el) {
                el.textContent = 'Range: ' + startDate.toLocaleString() + ' → ' + endDate.toLocaleString();
            }
        }
        
        const rangeEl = document.getElementById('chart-data-range');
        if (rangeEl) {
            rangeEl.textContent = 'Showing ' + this.historyData.length + ' entries';
        }
    }

    showNoDataMessage(message) {
        const canvas = document.getElementById('sensor-history-chart');
        if (!canvas) return;
        
        const ctx = canvas.getContext('2d');
        const rect = canvas.getBoundingClientRect();
        
        ctx.clearRect(0, 0, rect.width, rect.height);
        ctx.fillStyle = '#0f0f1a';
        ctx.fillRect(0, 0, rect.width, rect.height);
        ctx.fillStyle = '#6a7a9e';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(message, rect.width/2, rect.height/2);
    }

    async updateStatus() {
        try {
            const response = await fetch('/api/status');
            if (!response.ok) return;
            const status = await response.json();
            
            const wifiEl = document.getElementById('wifi-status');
            if (wifiEl) {
                wifiEl.textContent = status.wifi ? 'Connected' : 'Disconnected';
                wifiEl.className = 'status-value ' + (status.wifi ? 'connected' : 'disconnected');
            }
            
            const ntpEl = document.getElementById('ntp-status');
            if (ntpEl) {
                ntpEl.textContent = status.ntp ? 'Synchronized' : 'Unsynchronized';
                ntpEl.className = 'status-value ' + (status.ntp ? 'synced' : 'unsynced');
            }
            
            const storageEl = document.getElementById('storage-status');
            if (storageEl && status.storage_used !== undefined) {
                storageEl.textContent = status.storage_used + '%';
            }
        } catch (error) {
            // Silently fail
        }
    }

    toggleChartVisibility() {
        const container = document.getElementById('chart-container');
        const selector = document.getElementById('sensor-selector');
        const btn = document.getElementById('chart-toggle-btn');
        
        if (!container) return;
        
        const isHidden = container.classList.contains('hidden');
        
        if (isHidden) {
            container.classList.remove('hidden');
            if (selector) selector.classList.remove('hidden');
            btn.textContent = '📊 Hide Chart';
            btn.classList.remove('hidden');
            this.chartVisible = true;

            if (!this.chartUpdateInterval) {
                this.chartUpdateInterval = setInterval(() => {
                    if (this.chartVisible && !this.isUpdating && this.chartInitialized) {
                        this.fetchHistory();
                    }
                    this.updateStatus();
                }, this.updateInterval);
            }
            this.fetchHistory();
        } else {
            container.classList.add('hidden');
            if (selector) selector.classList.add('hidden');
            btn.textContent = '📊 Show Chart';
            btn.classList.add('hidden');
            this.chartVisible = false;
            this.pauseChartUpdates();
        }
        
        localStorage.setItem('chart_visible', this.chartVisible ? 'true' : 'false');
    }

    pauseChartUpdates() {
        this.chartVisible = false;
        this.isUpdating = true;

        if (this.chartUpdateInterval) {
            clearInterval(this.chartUpdateInterval);
            this.chartUpdateInterval = null;
        }
    }

    resumeChartUpdates() {
        this.chartVisible = true;
        this.isUpdating = false;
        this.fetchHistory();
        
        if (this.chartUpdateInterval) {
            clearInterval(this.chartUpdateInterval);
        }
        this.chartUpdateInterval = setInterval(() => {
            if (this.chartVisible && !this.isUpdating && this.chartInitialized) {
                this.fetchHistory();
            }
        }, this.updateInterval);
    }
}

// ============================================================
// GLOBAL INITIALIZATION
// ============================================================

let dashboardManager = null;

function initDashboardManager() {
    if (!dashboardManager) {
        dashboardManager = new DashboardManager();
        window.dashboardManager = dashboardManager;
    }
    return dashboardManager;
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initDashboardManager);
} else {
    initDashboardManager();
}

// Initialize main dashboard
initDashboard();