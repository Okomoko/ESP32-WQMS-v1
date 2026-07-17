// js/core.js
// Core functionality - loaded on every page

// following ones need to be exactly the same in project_defs.h . Currently HW-316 boards are active low, therefore RELAY_STATE_ACTIVE is 0 not 1
const RELAY_STATE_ACTIVE = 0
const RELAY_STATE_IDLE = 1
const RELAY_STATE_COOLDOWN = 2

const REFRESH_INTERVAL = 1000

// ============================================================
// API Client
// ============================================================
const api = {
    async get(endpoint) {
        const r = await fetch(endpoint);
        return r.json();
    },
    async post(endpoint, data = {}) {
        const r = await fetch(endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        return r.json();
    },
    async put(endpoint, data = {}) {
        const r = await fetch(endpoint, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        return r.json();
    },
    async del(endpoint) {
        const r = await fetch(endpoint, { method: 'DELETE' });
        return r.json();
    }
};

// ============================================================
// DOM Helpers
// ============================================================
const $ = (s) => document.querySelector(s);
const $$ = (s) => document.querySelectorAll(s);

// ============================================================
// Header Update
// ============================================================
async function updateHeader() {
    try {
        const data = await api.get('/api/status');
        const el = (id) => document.getElementById(id);
        if (el('system-name')) el('system-name').textContent = data.system_name || 'WQMS-System';
        if (el('ip-address')) el('ip-address').textContent = data.ip || '0.0.0.0';
        if (el('wifi-ssid')) el('wifi-ssid').textContent = data.wifi_ssid || 'N/A';
        if (el('datetime')) el('datetime').textContent = new Date().toLocaleString();
        if (el('free-heap')) el('free-heap').textContent = (data.free_heap_kb || 0) + ' KB';
        if (el('uptime')) el('uptime').textContent = data.uptime || '0m';
        if (el('cpu0-load')) el('cpu0-load').textContent = (data.cpu0_load || 0).toFixed(0).padStart(2, '\u2007') + '%';
        if (el('cpu1-load')) el('cpu1-load').textContent = (data.cpu1_load || 0).toFixed(0).padStart(2, '\u2007') + '%';
//        if (el('cpu-temp')) el('cpu-temp').textContent = (data.cpu_temp_c || 0).toFixed(0) + ' °C';
    } catch (e) {
        console.warn('Header update failed:', e);
    }
}

// ============================================================
// Reboot System
// ============================================================
async function rebootSystem() {
    if (!confirm('⚠️ Are you sure you want to reboot the system?')) return;
    const btn = document.getElementById('reboot-btn') || document.getElementById('reboot-btn-config');
    if (btn) { btn.textContent = '⏳ Rebooting...'; btn.disabled = true; }
    try {
        await fetch('/api/system/reboot', { method: 'POST', headers: { 'Content-Type': 'application/json' } });
        alert('✅ System rebooting... Wait 15 seconds and refresh.');
    } catch (e) {
        alert('⚠️ System is rebooting... Wait 15 seconds and refresh.');
    }
    if (btn) { btn.textContent = '🔁 Reboot'; btn.disabled = false; }
}

// ============================================================
// Tab Management
// ============================================================
function initTabs() {
    const tabs = document.querySelectorAll('.tab-btn');
    const panels = document.querySelectorAll('.tab-panel');
    tabs.forEach(btn => {
        btn.addEventListener('click', () => {
            tabs.forEach(b => b.classList.remove('active'));
            panels.forEach(p => p.classList.remove('active'));
            btn.classList.add('active');
            const target = document.getElementById('tab-' + btn.dataset.tab);	
            if (target) target.classList.add('active');
        });
    });
}

// ============================================================
// Navigation
// ============================================================
function initNavigation() {
    document.querySelectorAll('#nav a').forEach(link => {
        link.addEventListener('click', () => {
            document.querySelectorAll('#nav a').forEach(l => l.classList.remove('active'));
            link.classList.add('active');
        });
    });
}
// ============================================
// CORE.JS - Shared Utilities
// ============================================

/**
 * Show a notification message (Global function)
 * @param {string} message - The message to display
 * @param {string} type - 'success', 'error', 'warning', 'info' (default: 'info')
 * @param {number} duration - Time in ms to show notification (default: 5000)
 */
function showNotification(message, type = 'info', duration = 5000) {
    // Remove existing notification if any
    const existing = document.querySelector('.notification-container');
    if (existing) {
        existing.remove();
    }
    
    // Create container
    const container = document.createElement('div');
    container.className = 'notification-container';
    container.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        z-index: 9999;
        min-width: 280px;
        max-width: 500px;
        animation: slideIn 0.3s ease;
    `;
    
    // Create notification
    const notification = document.createElement('div');
    notification.className = `notification notification-${type}`;
    
    // Set colors based on type
    const colors = {
        success: { bg: '#28a745', text: '#fff', icon: '✅' },
        error: { bg: '#dc3545', text: '#fff', icon: '❌' },
        warning: { bg: '#ffc107', text: '#000', icon: '⚠️' },
        info: { bg: '#17a2b8', text: '#fff', icon: 'ℹ️' }
    };
    
    const color = colors[type] || colors.info;
    
    notification.style.cssText = `
        background: ${color.bg};
        color: ${color.text};
        padding: 12px 20px;
        margin-bottom: 10px;
        border-radius: 8px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        font-size: 14px;
        font-weight: 500;
        display: flex;
        align-items: center;
        gap: 10px;
        border: 1px solid rgba(255,255,255,0.1);
    `;
    
    // Add icon
    const icon = document.createElement('span');
    icon.textContent = color.icon;
    icon.style.fontSize = '18px';
    notification.appendChild(icon);
    
    // Add message
    const text = document.createElement('span');
    text.textContent = message;
    text.style.flex = '1';
    notification.appendChild(text);
    
    // Add close button
    const closeBtn = document.createElement('span');
    closeBtn.textContent = '×';
    closeBtn.style.cssText = `
        cursor: pointer;
        font-size: 20px;
        line-height: 20px;
        opacity: 0.7;
        transition: opacity 0.2s;
        margin-left: 8px;
    `;
    closeBtn.onmouseover = () => closeBtn.style.opacity = '1';
    closeBtn.onmouseout = () => closeBtn.style.opacity = '0.7';
    closeBtn.onclick = () => {
        container.remove();
    };
    notification.appendChild(closeBtn);
    
    container.appendChild(notification);
    document.body.appendChild(container);
    
    // Auto-remove after duration
    if (duration > 0) {
        setTimeout(() => {
            if (container.parentNode) {
                container.style.opacity = '0';
                container.style.transition = 'opacity 0.3s';
                setTimeout(() => container.remove(), 300);
            }
        }, duration);
    }
}

// Make it globally accessible
window.showNotification = showNotification;

// Add CSS animation if not already present
(function addStyles() {
    if (document.getElementById('notification-styles')) return;
    
    const style = document.createElement('style');
    style.id = 'notification-styles';
    style.textContent = `
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
        @keyframes slideOut {
            from { transform: translateX(0); opacity: 1; }
            to { transform: translateX(100%); opacity: 0; }
        }
    `;
    document.head.appendChild(style);
})();

// If using CommonJS
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { showNotification };
}

document.getElementById('reboot-btn')?.addEventListener('click', rebootSystem);
document.getElementById('reboot-btn-config')?.addEventListener('click', rebootSystem);

// ============================================================
// Expose core functions to global scope
// ============================================================
window.rebootSystem = rebootSystem;