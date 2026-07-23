// web_assets/js/console.js

// --- ANSI to HTML Converter ---
class AnsiParser {
    constructor() {
        this.codes = {
            '0': '</span>',
            '1': '<span class="bold">',
            '31': '<span class="error">',
            '32': '<span class="info">',
            '33': '<span class="warn">',
            '34': '<span class="debug">',
            '36': '<span class="debug">',
            '37': '<span class="verbose">',
            '90': '<span class="verbose">',
        };
    }
    
    parse(text) {
        if (!text) return '';
        
        let html = '';
        let i = 0;
        let inSpan = false;
        
        while (i < text.length) {
            if (text[i] === '\u001b') {
                let seq = '';
                i++;
                while (i < text.length && text[i] !== 'm') {
                    seq += text[i];
                    i++;
                }
                i++;
                
                const parts = seq.split(';');
                let hasReset = false;
                
                for (const code of parts) {
                    if (code === '0') {
                        hasReset = true;
                        if (inSpan) {
                            html += '</span>';
                            inSpan = false;
                        }
                    } else if (this.codes[code]) {
                        if (inSpan) {
                            html += '</span>';
                        }
                        html += this.codes[code];
                        inSpan = true;
                    }
                }
                
                if (hasReset && !parts.some(c => c !== '0' && this.codes[c])) {
                    if (inSpan) {
                        html += '</span>';
                        inSpan = false;
                    }
                }
            } else {
                const char = text[i];
                if (char === '<') {
                    html += '&lt;';
                } else if (char === '>') {
                    html += '&gt;';
                } else if (char === '&') {
                    html += '&amp;';
                } else if (char === '"') {
                    html += '&quot;';
                } else {
                    html += char;
                }
                i++;
            }
        }
        
        if (inSpan) {
            html += '</span>';
        }
        
        return html;
    }
}

// --- Console Viewer Application ---
class ConsoleViewer {
    constructor() {
        this.parser = new AnsiParser();
        this.logElement = document.getElementById('log');
        this.lineCountElement = document.getElementById('lineCount');
        this.updateTimeElement = document.getElementById('updateTime');
        this.bufferInfoElement = document.getElementById('bufferInfo');
        this.statusElement = document.getElementById('console-status');
        
        this.isPaused = false;
        this.refreshInterval = 1000; // 1 second
        this.isFirstFetch = true;
        this.lastContent = '';
        this.fetchInProgress = false;
        this.retryCount = 0;
        this.maxRetries = 3;
        
        // Setup controls
        document.getElementById('pauseBtn').addEventListener('click', () => this.togglePause());
        document.getElementById('clearBtn').addEventListener('click', () => this.clearLog());
        document.getElementById('downloadBtn').addEventListener('click', () => this.downloadLog());
        
        // Start fetching immediately and then every second
        this.fetchLogs();
        this.intervalId = setInterval(() => this.fetchLogs(), this.refreshInterval);
        
        // Also fetch on visibility change (when tab becomes active again)
        document.addEventListener('visibilitychange', () => {
            if (!document.hidden && !this.isPaused) {
                this.fetchLogs();
            }
        });
    }
    
    togglePause() {
        this.isPaused = !this.isPaused;
        const btn = document.getElementById('pauseBtn');
        if (this.isPaused) {
            btn.textContent = '▶ Resume';
            btn.classList.remove('active');
            this.statusElement.innerHTML = '⏸ <span class="offline">Paused</span>';
        } else {
            btn.textContent = '⏸ Pause';
            btn.classList.add('active');
            this.statusElement.innerHTML = '🟢 <span class="online">Live</span>';
            // Fetch immediately when resuming
            this.fetchLogs();
        }
    }
    
    clearLog() {
        this.logElement.innerHTML = '';
        this.lastContent = '';
        this.lineCountElement.textContent = 'Lines: 0';
        this.bufferInfoElement.textContent = 'Buffer: 0%';
    }
    
    downloadLog() {
        const rawText = this.logElement.textContent || '';
        const blob = new Blob([rawText], { type: 'text/plain;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `console_log_${new Date().toISOString().slice(0,19).replace(/[:-]/g, '')}.txt`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }
    
    async fetchLogs() {
        // Prevent concurrent fetches
        if (this.fetchInProgress) return;
        if (this.isPaused) return;
        
        this.fetchInProgress = true;
        
        try {
            const response = await fetch('/console', {
                cache: 'no-store',
                headers: {
                    'Cache-Control': 'no-cache',
                    'Pragma': 'no-cache'
                }
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const text = await response.text();
            this.retryCount = 0; // Reset retry count on success
            
            // Only update if content changed
            if (text !== this.lastContent) {
                this.lastContent = text;
                
                // Update display
                if (text.length > 0) {
                    const html = this.parser.parse(text);
                    this.logElement.innerHTML = html;
                    
                    // Update stats
                    const lines = html.split('\n').length;
                    this.lineCountElement.textContent = `Lines: ${lines}`;
                    
                    // Buffer usage (approximate)
                    const bufferPercent = Math.min(100, Math.round((text.length / 16384) * 100));
                    this.bufferInfoElement.textContent = `Buffer: ${bufferPercent}%`;
                } else {
                    // If empty, show a placeholder
                    if (this.isFirstFetch) {
                        this.logElement.innerHTML = '<span style="color: #7a9bbf;">Waiting for logs...</span>';
                    }
                }
                
                const now = new Date();
                this.updateTimeElement.textContent = `Updated: ${now.toTimeString().slice(0, 8)}`;
                
                // Auto-scroll to bottom if we're near the bottom
                const content = document.getElementById('console-content');
                if (content) {
                    const isNearBottom = content.scrollHeight - content.scrollTop - content.clientHeight < 50;
                    if (isNearBottom || this.isFirstFetch) {
                        content.scrollTop = content.scrollHeight;
                    }
                }
            }
            
            // Update status
            this.statusElement.innerHTML = '🟢 <span class="online">Live</span>';
            this.isFirstFetch = false;
            
        } catch (error) {
            console.error('Fetch error:', error);
            this.retryCount++;
            
            // Show error after multiple failures
            if (this.retryCount >= this.maxRetries) {
                this.statusElement.innerHTML = '🔴 <span class="offline">Disconnected</span>';
                if (this.isFirstFetch) {
                    this.logElement.innerHTML = '<span style="color: #d32f2f;">⚠️ Unable to connect to console. Retrying...</span>';
                }
            }
        } finally {
            this.fetchInProgress = false;
        }
    }
}

// Initialize when page loads
document.addEventListener('DOMContentLoaded', () => {
    new ConsoleViewer();
});

// Also initialize if DOM is already loaded
if (document.readyState === 'complete' || document.readyState === 'interactive') {
    // DOM already loaded, but ensure we only create one instance
    if (!window.consoleViewer) {
        window.consoleViewer = new ConsoleViewer();
    }
}