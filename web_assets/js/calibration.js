// js/calibration.js
// Calibration page - loaded only when needed

// ============================================================
// Calibration Functions
// ============================================================
async function loadCalibrationSensors() {
    try {
        const data = await api.get('/api/sensors');
        const select = document.getElementById('cal-sensor-select');
        if (!select) return;
        select.innerHTML = (data.sensors || []).map(s =>
            `<option value="${s.id}">${s.name || 'Sensor ' + s.id}</option>`
        ).join('');
        const urlParams = new URLSearchParams(window.location.search);
        const sensorId = urlParams.get('sensor');
        if (sensorId !== null) select.value = sensorId;
    } catch (e) {
        console.warn('Calibration sensor load failed:', e);
    }
}

// ============================================================
// Initialize Calibration
// ============================================================
async function initCalibration() {
    await updateHeader();
    await loadCalibrationSensors();
    
    document.getElementById('cal-start')?.addEventListener('click', async function() {
        const sensorId = parseInt(document.getElementById('cal-sensor-select').value);
        const result = await api.post('/api/calibrate/start', { sensor_id: sensorId });
        if (result.success) {
            document.getElementById('cal-step-1').classList.remove('active');
            document.getElementById('cal-step-2').classList.add('active');
            document.getElementById('cal-result').style.display = 'none';
            document.getElementById('cal-sample-body').innerHTML = '';
            alert('Calibration started for sensor ' + sensorId);
        }
    });
    
    document.getElementById('cal-add-sample')?.addEventListener('click', async function() {
        const value = parseFloat(document.getElementById('cal-known-value').value);
        if (isNaN(value)) {
            alert('Please enter a valid reference value');
            return;
        }
        const result = await api.post('/api/calibrate/sample', { known_value: value });
        if (result.success) {
            const body = document.getElementById('cal-sample-body');
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${result.sample_number}</td>
                <td>${result.known_value}</td>
                <td>${result.raw_adc}</td>
                <td>${result.voltage.toFixed(3)}</td>
            `;
            body.appendChild(row);
            document.getElementById('cal-known-value').value = '';
            const factor = await api.get('/api/calibrate/factor');
            if (factor && factor.factor) {
                document.getElementById('cal-factor').textContent = factor.factor.toFixed(3);
                document.getElementById('cal-result').style.display = 'block';
            }
        }
    });
    
    document.getElementById('cal-apply')?.addEventListener('click', async function() {
        const result = await api.post('/api/calibrate/apply');
        if (result.success) {
            alert('Calibration applied: factor = ' + result.factor);
            document.getElementById('cal-step-2').classList.remove('active');
            document.getElementById('cal-step-1').classList.add('active');
            document.getElementById('cal-result').style.display = 'none';
        }
    });
    
    document.getElementById('cal-cancel')?.addEventListener('click', async function() {
        await api.post('/api/calibrate/cancel');
        document.getElementById('cal-step-2').classList.remove('active');
        document.getElementById('cal-step-1').classList.add('active');
        document.getElementById('cal-result').style.display = 'none';
        alert('Calibration cancelled');
    });

    // Auto-refresh header
    setInterval(updateHeader, REFRESH_INTERVAL);
}