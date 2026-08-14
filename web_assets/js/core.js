// js/core.js
// Core functionality - loaded on every page

// ============================================================
// CONSTANTS
// ============================================================
const RELAY_STATE_ACTIVE = 0;
const RELAY_STATE_IDLE = 1;
const RELAY_STATE_COOLDOWN = 2;
const REFRESH_INTERVAL = 1000;

// ============================================================
// LOADING MANAGER - Generic, only blocks on initial load
// ============================================================
class LoadingManager {
    constructor() {
        this.count = 0;
        this.initialLoadComplete = false;
        this.overlay = document.getElementById('page-loading-overlay') || this.createOverlay();
        this.textEl = document.getElementById('loading-text');
        this.progressEl = document.getElementById('loading-progress');
    }

    createOverlay() {
        const el = document.createElement('div');
        el.id = 'page-loading-overlay';
        el.innerHTML = `
            <div class="spinner-container">
                <div class="loading-spinner"></div>
                <div class="loading-text" id="loading-text">Loading...</div>
                <div class="progress-container">
                    <div class="progress-bar" id="loading-progress"></div>
                </div>
            </div>
        `;
        document.body.appendChild(el);
        return el;
    }

    // Show loading - only works before initial load is complete
    show(msg = 'Loading...', progress = 0) {
        // DON'T show if initial load is already complete
        if (this.initialLoadComplete) {
            console.debug('Loading suppressed - initial load complete');
            return;
        }
        
        this.count++;
        if (this.textEl) this.textEl.textContent = msg;
        if (this.progressEl) this.progressEl.style.width = progress + '%';
        this.overlay.style.display = 'flex';
        document.getElementById('app')?.classList.add('loading');
        clearTimeout(this._hideTimer);
    }

    updateMessage(msg, progress = null) {
        if (this.initialLoadComplete) return;
        if (this.textEl) this.textEl.textContent = msg;
        if (progress !== null && this.progressEl) {
            this.progressEl.style.width = Math.min(100, Math.max(0, progress)) + '%';
        }
    }

    hide() {
        if (this.initialLoadComplete) return;
        
        this.count = Math.max(0, this.count - 1);
        if (this.count === 0) {
            this._hideTimer = setTimeout(() => {
                this.overlay.style.display = 'none';
                document.getElementById('app')?.classList.remove('loading');
                if (this.progressEl) this.progressEl.style.width = '0%';
                // Mark initial load as complete
                this.initialLoadComplete = true;
            }, 200);
        }
    }

    // Force complete initial load (called by router)
    completeInitialLoad() {
        this.initialLoadComplete = true;
        this.count = 0;
        this.overlay.style.display = 'none';
        document.getElementById('app')?.classList.remove('loading');
        if (this.progressEl) this.progressEl.style.width = '0%';
        clearTimeout(this._hideTimer);
    }

    // Check if we should show loading
    shouldShowLoading() {
        return !this.initialLoadComplete;
    }
}

const loadingManager = new LoadingManager();
window.loadingManager = loadingManager;

// ============================================================
// API CLIENT - Smart loading detection
// ============================================================
const api = {
    // Generic request method - automatically decides if loading should show
    async _request(endpoint, { method, body, message, forceLoading = false } = {}) {
        // Show loading ONLY if:
        // 1. It's the initial page load (loadingManager.shouldShowLoading())
        // 2. OR forceLoading is explicitly true
        const showLoading = forceLoading || loadingManager.shouldShowLoading();

        if (showLoading) {
            loadingManager.show(message || `Loading...`, 10);
        }

        try {
            if (showLoading) loadingManager.updateMessage('Processing...', 40);
            
            const options = { method };
            if (body) {
                options.headers = { 'Content-Type': 'application/json' };
                options.body = JSON.stringify(body);
            }
            
            const r = await fetch(endpoint, options);
            
            if (showLoading) loadingManager.updateMessage('Finalizing...', 80);
            const data = await r.json();
            if (showLoading) loadingManager.updateMessage('Done', 100);
            return data;
        } catch (error) {
            console.warn('API request failed:', endpoint, error);
            throw error;
        } finally {
            if (showLoading) {
                loadingManager.hide();
            }
        }
    },

    // All methods use the same _request with automatic loading detection
    async get(endpoint, options = {}) {
        return this._request(endpoint, { ...options, method: 'GET' });
    },
    
    async post(endpoint, data = {}, options = {}) {
        return this._request(endpoint, { ...options, method: 'POST', body: data });
    },
    
    async put(endpoint, data = {}, options = {}) {
        return this._request(endpoint, { ...options, method: 'PUT', body: data });
    },
    
    async del(endpoint, options = {}) {
        return this._request(endpoint, { ...options, method: 'DELETE' });
    },

    // Forced background calls - NEVER show loading (for backward compatibility)
    async getBackground(endpoint) {
        try {
            const r = await fetch(endpoint);
            return r.json();
        } catch (e) {
            console.warn('Background API call failed:', endpoint, e);
            return null;
        }
    }
};

window.api = api;

// ============================================================
// DOM HELPERS
// ============================================================
const $ = s => document.querySelector(s);
const $$ = s => document.querySelectorAll(s);

// ============================================================
// HEADER UPDATE - Always background (NO loading)
// ============================================================
async function updateHeader() {
    try {
        const d = await api.getBackground('/api/status');
        if (!d) return;
        const map = {
            'system-name': d.system_name || 'WQMS-System',
            'ip-address': d.ip || '0.0.0.0',
            'wifi-ssid': d.wifi_ssid || 'N/A',
            'datetime': new Date().toLocaleString(),
            'free-heap': (d.free_heap_kb || 0) + ' KB',
            'uptime': d.uptime || '0m',
            'cpu0-load': (d.cpu0_load || 0).toFixed(0).padStart(3, '\u2007') + '%',
            'cpu1-load': (d.cpu1_load || 0).toFixed(0).padStart(3, '\u2007') + '%'
        };
        Object.entries(map).forEach(([id, val]) => {
            const el = document.getElementById(id);
            if (el) el.textContent = val;
        });
    } catch (e) { /* silent fail */ }
}

// ============================================================
// REBOOT - User action (force loading)
// ============================================================
async function rebootSystem() {
    if (!confirm('⚠️ Reboot system?')) return;
    const btn = document.getElementById('reboot-btn') || document.getElementById('reboot-btn-config');
    if (btn) { btn.textContent = '⏳ Rebooting...'; btn.disabled = true; }
    
    // Force loading for user action
    loadingManager.show('Rebooting system...', 30);
    try {
        await fetch('/api/system/reboot', { method: 'POST' });
    } catch (e) {}
    loadingManager.updateMessage('System rebooting...', 80);
    alert('✅ System rebooting...\nPage will refresh in 30s');
    setTimeout(() => {
        loadingManager.forceHide();
        location.reload();
    }, 30000);
}

// ============================================================
// TABS
// ============================================================
function initTabs() {
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
            btn.classList.add('active');
            const target = document.getElementById('tab-' + btn.dataset.tab);
            if (target) target.classList.add('active');
        });
    });
}

// ============================================================
// NAVIGATION
// ============================================================
function initNavigation() {
    document.querySelectorAll('#nav a').forEach(link => {
        link.addEventListener('click', () => {
            document.querySelectorAll('#nav a').forEach(l => l.classList.remove('active'));
            link.classList.add('active');
        });
    });
}

// ============================================================
// NOTIFICATIONS
// ============================================================
function showNotification(msg, type = 'info', duration = 5000) {
    const existing = document.querySelector('.notification-container');
    if (existing) existing.remove();

    const colors = {
        success: { bg: '#28a745', icon: '✅' },
        error: { bg: '#dc3545', icon: '❌' },
        warning: { bg: '#ffc107', icon: '⚠️' },
        info: { bg: '#17a2b8', icon: 'ℹ️' }
    };
    const c = colors[type] || colors.info;

    const container = document.createElement('div');
    container.className = 'notification-container';
    container.style.cssText = `
        position: fixed; top: 20px; right: 20px; z-index: 9999;
        min-width: 280px; max-width: 500px;
        animation: slideIn 0.3s ease;
    `;

    const notification = document.createElement('div');
    notification.style.cssText = `
        background: ${c.bg}; color: #fff; padding: 12px 20px;
        border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        font-size: 14px; font-weight: 500;
        display: flex; align-items: center; gap: 10px;
    `;
    notification.innerHTML = `
        <span style="font-size:18px;">${c.icon}</span>
        <span style="flex:1;">${msg}</span>
        <span onclick="this.closest('.notification-container').remove()" 
              style="cursor:pointer;font-size:20px;opacity:0.7;">×</span>
    `;

    container.appendChild(notification);
    document.body.appendChild(container);
    setTimeout(() => {
        if (container.parentNode) {
            container.style.opacity = '0';
            container.style.transition = 'opacity 0.3s';
            setTimeout(() => container.remove(), 300);
        }
    }, duration);
}

// ============================================================
// INIT
// ============================================================
document.getElementById('reboot-btn')?.addEventListener('click', rebootSystem);
document.getElementById('reboot-btn-config')?.addEventListener('click', rebootSystem);
window.rebootSystem = rebootSystem;
window.showNotification = showNotification;

// Start periodic header updates (background, no loading)
setInterval(updateHeader, REFRESH_INTERVAL);