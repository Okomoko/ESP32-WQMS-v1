// js/calibration.js
// Calibration page - loaded only when needed

// ============================================================
// Calibration Functions
// ============================================================
async function loadCalibrationSensors() {
    try {
        const data = await api.get('/api/sensors/config');
        const select = document.getElementById('cal-sensor-select');
        if (!select) return;
        
        select.innerHTML = (data.sensors || []).map(s =>
            `<option value="${s.id}">${s.name || 'Sensor ' + s.id}</option>`
        ).join('');
        
        const urlParams = new URLSearchParams(window.location.search);
        const sensorId = urlParams.get('sensor');
        if (sensorId !== null) select.value = sensorId;
        
        // Update current sensor value when selection changes
        select.addEventListener('change', updateCurrentSensorValue);
        await updateCurrentSensorValue();
    } catch (e) {
        console.warn('Calibration sensor load failed:', e);
    }
}

async function updateCurrentSensorValue() {
    try {
        const select = document.getElementById('cal-sensor-select');
        const sensorId = parseInt(select.value);
        if (isNaN(sensorId)) {
            document.getElementById('cal-current1').textContent = '--';
            document.getElementById('cal-current2').textContent = '--';
            return;
        }
        
        const data = await api.get('/api/sensors');
        const sensor = (data.sensors || []).find(s => s.id === sensorId);
        if (sensor) {
            document.getElementById('cal-current1').textContent = sensor.value !== undefined ? sensor.value.toFixed(2) : '--';
            document.getElementById('cal-current2').textContent = sensor.value !== undefined ? sensor.value.toFixed(2) : '--';
        }
    } catch (e) {
        console.warn('Failed to update sensor value:', e);
    }
}

// ============================================================
// Initialize Calibration
// ============================================================
async function initCalibration() {
    await updateHeader();
    await loadCalibrationSensors();
    
    // ============================================================
    // Start Calibration
    // ============================================================
    document.getElementById('cal-start')?.addEventListener('click', async function() {
        const sensorId = parseInt(document.getElementById('cal-sensor-select').value);
        const sensorName = document.getElementById('cal-sensor-select').options[document.getElementById('cal-sensor-select').selectedIndex].text;
        if (isNaN(sensorId)) {
            alert('Please select a sensor');
            return;
        }
        
        try {
            const result = await api.post('/api/calibrate/start', { sensor_id: sensorId });
            if (result.success) {
                // Clear previous samples
                document.getElementById('cal-sample-body').innerHTML = '';
                document.getElementById('cal-result').style.display = 'none';
                document.getElementById('cal-factor').textContent = '--';
                
                // Switch steps
                document.getElementById('cal-step-1').classList.remove('active');
                document.getElementById('cal-step-2').classList.add('active');
                
                // Enable sample input
                document.getElementById('cal-known-value').disabled = false;
                document.getElementById('cal-add-sample').disabled = false;
                
                alert('Calibration started for sensor ' + sensorName);
                document.getElementById('sensor-name').innerText = sensorName;
            } else {
                alert('Failed to start calibration: ' + (result.message || 'Unknown error'));
            }
        } catch (e) {
            alert('Error: ' + e.message);
        }
    });
    
    // ============================================================
    // Add Sample
    // ============================================================
    document.getElementById('cal-add-sample')?.addEventListener('click', async function() {
        const value = parseFloat(document.getElementById('cal-known-value').value);
        if (isNaN(value) || value <= 0) {
            alert('Please enter a valid reference value (positive number)');
            return;
        }
        
        try {
            const result = await api.post('/api/calibrate/sample', { known_value: value });
            if (result.success) {
                const body = document.getElementById('cal-sample-body');
                const row = document.createElement('tr');
                const sampleNum = body.children.length + 1;
                
                row.innerHTML = `
                    <td>${sampleNum}</td>
                    <td>${result.known_value || value}</td>
                    <td>${result.raw_adc !== undefined ? result.raw_adc : '--'}</td>
                    <td>${result.voltage !== undefined ? result.voltage.toFixed(3) : '--'}</td>
                `;
                body.appendChild(row);
                
                // Clear input
                document.getElementById('cal-known-value').value = '';
                
                // Update factor display
                const factorResult = await api.get('/api/calibrate/factor');
                if (factorResult && factorResult.factor) {
                    document.getElementById('cal-factor').textContent = factorResult.factor.toFixed(4);
                    document.getElementById('cal-result').style.display = 'block';
                }
                
                // Enable apply button if we have at least 2 samples
                if (body.children.length >= 2) {
                    document.getElementById('cal-apply').disabled = false;
                }
            } else {
                alert('Failed to add sample: ' + (result.message || 'Unknown error'));
            }
        } catch (e) {
            alert('Error: ' + e.message);
        }
    });
    
    // ============================================================
    // Apply Calibration
    // ============================================================
    document.getElementById('cal-apply')?.addEventListener('click', async function() {
        const body = document.getElementById('cal-sample-body');
        if (body.children.length < 2) {
            alert('Need at least 2 samples to apply calibration');
            return;
        }
        
        try {
            const result = await api.post('/api/calibrate/apply');
            if (result.success) {
                alert('Calibration applied!\nFactor: ' + (result.factor || 'N/A'));
                
                // Reset UI
                document.getElementById('cal-step-2').classList.remove('active');
                document.getElementById('cal-step-1').classList.add('active');
                document.getElementById('cal-result').style.display = 'none';
                document.getElementById('cal-factor').textContent = '--';
                document.getElementById('cal-apply').disabled = true;
                document.getElementById('cal-known-value').disabled = true;
                document.getElementById('cal-add-sample').disabled = true;
                
                // Refresh sensor value
                await updateCurrentSensorValue();
            } else {
                alert('Failed to apply calibration: ' + (result.message || 'Unknown error'));
            }
        } catch (e) {
            alert('Error: ' + e.message);
        }
    });
    
    // ============================================================
    // Cancel Calibration
    // ============================================================
    document.getElementById('cal-cancel')?.addEventListener('click', async function() {
        try {
            await api.post('/api/calibrate/cancel');
            
            // Reset UI
            document.getElementById('cal-step-2').classList.remove('active');
            document.getElementById('cal-step-1').classList.add('active');
            document.getElementById('cal-result').style.display = 'none';
            document.getElementById('cal-factor').textContent = '--';
            document.getElementById('cal-sample-body').innerHTML = '';
            document.getElementById('cal-apply').disabled = true;
            document.getElementById('cal-known-value').disabled = true;
            document.getElementById('cal-add-sample').disabled = true;
            
            alert('Calibration cancelled');
        } catch (e) {
            alert('Error: ' + e.message);
        }
    });

    // ============================================================
    // Auto-refresh
    // ============================================================
    setInterval(() => {
        updateHeader();
        updateCurrentSensorValue();
    }, REFRESH_INTERVAL);
}