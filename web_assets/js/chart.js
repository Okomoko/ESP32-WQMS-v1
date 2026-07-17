/**
 * Lightweight Chart - ESP32 Optimized
 */
class LightChart {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) return;
        
        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width || this.canvas.parentElement.clientWidth || 800;
        this.height = this.canvas.height || 400;
        this.padding = { top: 20, right: 20, bottom: 40, left: 50 };
        
        this.chartArea = {
            x: this.padding.left,
            y: this.padding.top,
            width: this.width - this.padding.left - this.padding.right,
            height: this.height - this.padding.top - this.padding.bottom
        };
        
        this.colors = options.colors || {};
        this.labels = options.labels || {};
        this.datasets = {};
        this.labelsArray = [];
        this.maxDataPoints = options.maxDataPoints || 60;
        this.visibleDatasets = new Set(options.initialVisible || []);
        
        this.setupDPI();
        this.resizeHandler = this.resize.bind(this);
        window.addEventListener('resize', this.resizeHandler);
    }

    setupDPI() {
        const rect = this.canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        
        this.canvas.width = rect.width * dpr;
        this.canvas.height = rect.height * dpr;
        this.canvas.style.width = rect.width + 'px';
        this.canvas.style.height = rect.height + 'px';
        
        this.ctx.scale(dpr, dpr);
        this.width = rect.width;
        this.height = rect.height;
        this.chartArea = {
            x: this.padding.left,
            y: this.padding.top,
            width: this.width - this.padding.left - this.padding.right,
            height: this.height - this.padding.top - this.padding.bottom
        };
    }

    resize() {
        this.setupDPI();
        this.render();
    }

    setData(labels, data) {
        this.labelsArray = labels.slice(-this.maxDataPoints);
        
        Object.keys(data).forEach(key => {
            this.datasets[key] = data[key].slice(-this.maxDataPoints);
        });
        
        this.render();
    }

    setVisible(datasetKey, visible) {
        if (visible) {
            this.visibleDatasets.add(datasetKey);
        } else {
            this.visibleDatasets.delete(datasetKey);
        }
        this.render();
    }

    render() {
        const ctx = this.ctx;
        const w = this.width;
        const h = this.height;
        
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#0f0f1a';
        ctx.fillRect(0, 0, w, h);
        
        if (this.labelsArray.length === 0) {
            ctx.fillStyle = '#6a7a9e';
            ctx.font = '14px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('No data available', w/2, h/2);
            return;
        }
        
        const allValues = [];
        this.visibleDatasets.forEach(key => {
            if (this.datasets[key]) {
                const valid = this.datasets[key].filter(v => v !== null && !isNaN(v));
                allValues.push(...valid);
            }
        });
        
        if (allValues.length === 0) {
            ctx.fillStyle = '#6a7a9e';
            ctx.font = '14px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('No data for selected sensors', w/2, h/2);
            return;
        }
        
        const minVal = Math.min(0, ...allValues);
        const maxVal = Math.max(...allValues);
        const range = maxVal - minVal || 1;
        const padding = range * 0.1;
        const yMin = minVal - padding;
        const yMax = maxVal + padding;
        
        const chartX = this.chartArea.x;
        const chartY = this.chartArea.y;
        const chartW = this.chartArea.width;
        const chartH = this.chartArea.height;
        
        const mapX = (index) => chartX + (index / (this.labelsArray.length - 1 || 1)) * chartW;
        const mapY = (value) => chartY + chartH - ((value - yMin) / (yMax - yMin)) * chartH;
        
        // Grid
        ctx.strokeStyle = 'rgba(255,255,255,0.05)';
        ctx.lineWidth = 0.5;
        
        for (let i = 0; i <= 5; i++) {
            const y = chartY + (i / 5) * chartH;
            ctx.beginPath();
            ctx.moveTo(chartX, y);
            ctx.lineTo(chartX + chartW, y);
            ctx.stroke();
            
            const value = yMax - (i / 5) * (yMax - yMin);
            ctx.fillStyle = '#6a7a9e';
            ctx.font = '9px sans-serif';
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.fillText(value.toFixed(1), chartX - 8, y);
        }
        
        // X-axis labels
        const labelStep = Math.max(1, Math.floor(this.labelsArray.length / 12));
        for (let i = 0; i < this.labelsArray.length; i += labelStep) {
            const x = mapX(i);
            ctx.fillStyle = '#6a7a9e';
            ctx.font = '9px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';
            ctx.fillText(this.labelsArray[i], x, chartY + chartH + 6);
        }
        
        // Draw datasets
        let legendIndex = 0;
        this.visibleDatasets.forEach((key) => {
            const data = this.datasets[key];
            if (!data || data.length === 0) return;
            
            const color = this.colors[key] || '#FF6384';
            const label = this.labels[key] || key;
            
            ctx.beginPath();
            let started = false;
            
            for (let i = 0; i < data.length; i++) {
                const value = data[i];
                if (value === null || isNaN(value)) {
                    started = false;
                    continue;
                }
                
                const x = mapX(i);
                const y = mapY(value);
                
                if (!started) {
                    ctx.moveTo(x, y);
                    started = true;
                } else {
                    ctx.lineTo(x, y);
                }
            }
            
            ctx.strokeStyle = color;
            ctx.lineWidth = 1.5;
            ctx.stroke();
            
            // Points
            const pointStep = Math.max(1, Math.floor(data.length / 30));
            for (let i = 0; i < data.length; i += pointStep) {
                const value = data[i];
                if (value === null || isNaN(value)) continue;
                
                const x = mapX(i);
                const y = mapY(value);
                
                ctx.beginPath();
                ctx.arc(x, y, 2, 0, Math.PI * 2);
                ctx.fillStyle = color;
                ctx.fill();
            }
            
            // Legend
            const legendX = chartX + 12;
            const legendY = chartY + 12 + (legendIndex * 18);
            
            ctx.fillStyle = color;
            ctx.fillRect(legendX, legendY + 4, 14, 2);
            
            ctx.fillStyle = '#c0d0e0';
            ctx.font = '10px sans-serif';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'middle';
            ctx.fillText(label, legendX + 20, legendY + 5);
            
            legendIndex++;
        });
        
        ctx.strokeStyle = 'rgba(255,255,255,0.1)';
        ctx.lineWidth = 1;
        ctx.strokeRect(chartX, chartY, chartW, chartH);
    }

    destroy() {
        window.removeEventListener('resize', this.resizeHandler);
    }
}