// js/dashboard.js
// ============================================================
// DASHBOARD MANAGER
// ============================================================

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
        9: 'µg/L'  // µg/L
    };
    return UNIT_SYMBOLS[unitId] || '';
}
class DashboardManager {
    constructor() {
        // Chart configuration
        this.maxDataPoints = 4320;
        this.updateInterval = 30000;
        this.isUpdating = false;
        this.chart = null;
        this.historyData = [];
        this.selectedSensors = new Set();
        this.chartInitialized = false;
        
        // Sensor properties
        this.sensorLabels = {};
        this.colors = {};
        this.sensorConfig = [];
        this.sensorMap = {};
        this.sensorNameMap = {};
        this.sensorColorMap = {};
        this.sensorKeyMap = {};
        this.sensorUnit = {}
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
        
        // Load sensor config first
        await this.loadSensorConfig();
        
        // Initialize chart
        this.initChart();
        this.setupSensorSelector();
        this.setupChartControls();
        this.setupChartToggle();  // <-- ADD THIS
        this.updateStatus();

        // Chart is visible by default
        this.chartVisible = false;

        const container = document.getElementById('chart-container');
        const selector = document.getElementById('sensor-selector');
        const btn = document.getElementById('chart-toggle-btn');
        
        if (container) container.classList.add('hidden');
        if (selector) selector.classList.add('hidden');
        if (btn) {
            btn.textContent = '📊 Show Chart';
            btn.classList.add('hidden');
        }

        // Load sensors and relays separately
        loadSensors();
        loadRelays();

        window.dashboardManager = this;
    }

    /**
     * Setup chart toggle button
     */
    setupChartToggle() {
        const btn = document.getElementById('chart-toggle-btn');
        if (!btn) return;
        
        btn.addEventListener('click', () => this.toggleChartVisibility());

/*
        // Restore state from localStorage if available
        const savedState = localStorage.getItem('chart_visible');
        if (savedState === 'false') {
            // Start hidden
            const container = document.getElementById('chart-container');
            const selector = document.getElementById('sensor-selector');
            if (container) container.classList.add('hidden');
            if (selector) selector.classList.add('hidden');
            btn.textContent = '📊 Show Chart';
            btn.classList.add('hidden');
            this.chartVisible = false;
            this.pauseChartUpdates();
        }
*/
    }

    /**
     * Save chart visibility state
     */
    saveChartState() {
        localStorage.setItem('chart_visible', this.chartVisible ? 'true' : 'false');
    }

    async loadSensorConfig() {
        try {
            const response = await fetch('/api/history/config');
            if (!response.ok) {
                throw new Error('Failed to load sensor config');
            }
            
            const data = await response.json();
            if (data && data.sensors) {
                this.sensorConfig = data.sensors;
                
                this.sensorMap = {};
                this.sensorNameMap = {};
                this.sensorColorMap = {};
                this.sensorKeyMap = {};
                this.sensorLabels = {};
                this.colors = {};
                this.sensorUnit = {}
                this.selectedSensors = new Set();
                
                this.sensorConfig.forEach(sensor => {
                    const id = sensor.id;
                    const name = sensor.name || 'Sensor ' + (id + 1);
                    const color = sensor.color || this.defaultColors[id % this.defaultColors.length];
                    const key = name.replace(/ /g, '_');
                    const unit = sensor.unit;
                    
                    this.sensorMap[id] = { name, color, key };
                    this.sensorNameMap[id] = name;
                    this.sensorColorMap[id] = color;
                    this.sensorKeyMap[id] = key;
                    this.sensorLabels[key] = name;
                    this.colors[key] = color;
                    this.selectedSensors.add(key);
                    this.sensorUnit[key] = unit;
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
            const unit = 0;
            
            this.sensorMap[id] = { name, color, key };
            this.sensorNameMap[id] = name;
            this.sensorColorMap[id] = color;
            this.sensorKeyMap[id] = key;
            this.sensorLabels[key] = name;
            this.colors[key] = color;
            this.selectedSensors.add(key);
            this.sensorUnit[key] = unit;
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
            const unit = sensor.unit;
            
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

    async fetchHistory() {
        if (this.isUpdating || !this.chartInitialized) return;
        this.isUpdating = true;

        try {
            const response = await fetch('/api/history?limit=' + this.maxDataPoints);
            if (!response.ok) {
                throw new Error('HTTP ' + response.status);
            }
            
            const data = await response.json();
            
            if (data && data.entries && data.entries.length > 0) {
                this.historyData = data.entries.map(entry => {
                    const mapped = {
                        timestamp: entry.timestamp,
                        sensor_mask: entry.sensor_mask
                    };
                    
                    Object.keys(entry).forEach(key => {
                        if (key !== 'timestamp' && key !== 'sensor_mask') {
                            mapped[key] = entry[key];
                            this.showNoDataMessage('Record %d', key);
                        }
                    });
                    
                   this.showNoDataMessage('');
                    return mapped;
                });
                
                // Update selected sensors to match data
                const dataKeys = Object.keys(this.historyData[0]).filter(
                    k => k !== 'timestamp' && k !== 'sensor_mask'
                );
                
                if (dataKeys.length > 0) {
                    const currentSelected = Array.from(this.selectedSensors);
                    const needsUpdate = dataKeys.some(key => !currentSelected.includes(key));
                    
                    if (needsUpdate) {
                        this.selectedSensors = new Set(dataKeys);
//                        this.updateSensorCheckboxes();
                    }
                }
                
                this.updateChart();
                this.updateChartFooter();
            } else {
                this.showNoDataMessage('No data available yet');
            }
        } catch (error) {
            console.error('Error fetching history:', error);
            this.showNoDataMessage('Error loading data');
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
        
        // Ensure chart has colors for all sensors
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

    /**
     * Toggle chart visibility
     */

    toggleChartVisibility() {
        const container = document.getElementById('chart-container');
        const selector = document.getElementById('sensor-selector');
        const btn = document.getElementById('chart-toggle-btn');
        
        if (!container) return;
        
        const isHidden = container.classList.contains('hidden');
        
        if (isHidden) {
            // Show chart
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

			// Fetch data immediately
			this.fetchHistory();
		} else {
            // Hide chart
            container.classList.add('hidden');
            if (selector) selector.classList.add('hidden');
            btn.textContent = '📊 Show Chart';
            btn.classList.add('hidden');
            
            this.chartVisible = false;
            this.pauseChartUpdates();
        }
        
        // Save state
        this.saveChartState();
    }

    /**
     * Pause all chart updates
     */
    pauseChartUpdates() {
        this.chartVisible = false;

        // Cancel any pending fetch
        this.isUpdating = true;

        // Stop the update interval if it exists
        if (this.chartUpdateInterval) {
            clearInterval(this.chartUpdateInterval);
            this.chartUpdateInterval = null;
        }
        
        console.log('[Dashboard] Chart updates paused');
    }

    /**
     * Resume chart updates
     */
    resumeChartUpdates() {
        this.chartVisible = true;
        this.isUpdating = false;
        
        // Fetch data immediately
        this.fetchHistory();
        
        // Restart interval
        if (this.chartUpdateInterval) {
            clearInterval(this.chartUpdateInterval);
        }
        this.chartUpdateInterval = setInterval(() => {
            if (this.chartVisible && !this.isUpdating && this.chartInitialized) {
                this.fetchHistory();
            }
        }, this.updateInterval);
        
        console.log('[Dashboard] Chart updates resumed');
    }

    /**
     * Check if chart should update (visibility-aware)
     */
    shouldUpdateChart() {
        return this.chartVisible && this.chartInitialized && !this.isUpdating;
    }

    /**
     * Override fetchHistory to check visibility
     */
    async fetchHistory() {
        // Skip if chart is not visible
        if (!this.chartVisible) {
            console.log('[Dashboard] Chart hidden, skipping update');
            return;
        }
        
        if (this.isUpdating || !this.chartInitialized) return;
        this.isUpdating = true;

        try {
            const response = await fetch('/api/history?limit=' + this.maxDataPoints);
            if (!response.ok) {
                throw new Error('HTTP ' + response.status);
            }
            
            const data = await response.json();
            
            if (data && data.entries && data.entries.length > 0) {
                this.historyData = data.entries.map(entry => {
                    const mapped = {
                        timestamp: entry.timestamp,
                        sensor_mask: entry.sensor_mask
                    };
                    
                    Object.keys(entry).forEach(key => {
                        if (key !== 'timestamp' && key !== 'sensor_mask') {
                            mapped[key] = entry[key];
                        }
                    });
                    
                    return mapped;
                });
                
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
            } else {
                this.showNoDataMessage('No data available yet');
            }
        } catch (error) {
            console.error('Error fetching history:', error);
            this.showNoDataMessage('Error loading data');
        } finally {
            this.isUpdating = false;
        }
    }
}

// ============================================================
// GLOBAL INITIALIZATION
// ============================================================

let dashboardManager = null;
let hiddenDatasets = [];

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


// ============================================================
// Initialize Dashboard - Updated for Merged Version
// ============================================================

async function initDashboard() {
    // First, run the existing initialization
    await updateHeader();
    await loadSensors();
    await loadRelays();
    setInterval(() => {
        loadSensors();
        loadRelays();
    }, REFRESH_INTERVAL);

}

// Note: The initDashboard() function is now called from the main page
// The DashboardManager class handles the chart functionality separately

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
                quality: reading ? reading.quality : 0,
                unit: cfg.unit || 0
            };
        });
        
        renderSensorCards(sensors);
    } catch (e) {
        console.warn('Sensor load failed:', e);
    }
}

/*
async function loadSensorSelectors() {
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
                unit: cfg.unit || 0
            };
        });
        
        renderSensorSelector(sensors);
    } catch (e) {
        console.warn('Sensor load failed:', e);
    }
}
*/

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
                <div class="value">${isDisabled ? '--' : (s.value || 0).toFixed(2)}<span class="unit">${' ' + getUnitSymbol(s.unit)}</span></div>
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

/*
function renderSensorSelector(sensors) {
    const grid = document.getElementById('sensor-selector');
    if (!grid) return;
    
    if (!sensors || sensors.length === 0) {
        grid.innerHTML = '<p style="color:#7a9bbf;">No sensors configured</p>';
        return;
    }

    grid.innerHTML = sensors.map(s => {
        return `
            <label>
                <input type="checkbox" value="${s.id}" checked>
                ${s.name || 'Sensor ' + s.id}
            </label>
        `;
    }).join('');
}
*/

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
            const stateText = r.state === RELAY_STATE_ACTIVE ? 'ON' : r.state === RELAY_STATE_COOLDOWN ? 'Cooldown' : 'OFF';
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
