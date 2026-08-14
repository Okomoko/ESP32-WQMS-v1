// js/router.js
// Dynamic page loader

// ============================================================
// HELPERS
// ============================================================
function getScriptBasePath() {
    const scripts = document.querySelectorAll('script[src]');
    for (let s of scripts) {
        if (s.src && s.src.includes('router.js')) {
            return s.src.substring(0, s.src.lastIndexOf('/') + 1);
        }
    }
    return '/';
}

// ============================================================
// PAGE CONFIG
// ============================================================
const PAGES = {
    dashboard: { script: 'dashboard.js', init: 'initDashboard', label: 'Dashboard' },
    automation: { script: 'automation.js', init: 'initAutomation', label: 'Automation' },
    config: { script: 'config.js', init: 'initConfiguration', label: 'Configuration' },
    calibration: { script: 'calibration.js', init: 'initCalibration', label: 'Calibration' },
    api_docs: { script: 'api_docs.js', init: 'initApiDocs', label: 'API Docs' }
};

function detectPage() {
    const path = window.location.pathname;
    if (path.includes('automation')) return 'automation';
    if (path.includes('config')) return 'config';
    if (path.includes('calibration')) return 'calibration';
    if (path.includes('api_doc')) return 'api_docs';
    return 'dashboard';
}

// ============================================================
// LOAD SCRIPT
// ============================================================
function loadScript(src) {
    return new Promise((resolve, reject) => {
        const existing = document.querySelector(`script[src="${src}"]`);
        if (existing) return resolve();

        const script = document.createElement('script');
        script.src = src;
        script.async = true;
        script.onload = resolve;
        script.onerror = () => reject(new Error(`Failed to load: ${src}`));
        document.head.appendChild(script);
    });
}

// ============================================================
// ROUTER
// ============================================================
async function initRouter() {
    const page = detectPage();
    const config = PAGES[page];
    if (!config) return;

    console.log(`📄 Page: ${page}`);

    try {
        // Show loading only for initial page load
        if (window.loadingManager && !window.loadingManager.initialLoadComplete) {
            window.loadingManager.show(`Loading ${config.label}...`, 10);
        }

        // Load script
        const basePath = getScriptBasePath();
        await loadScript(basePath + config.script);

        // Update progress
        if (window.loadingManager && !window.loadingManager.initialLoadComplete) {
            window.loadingManager.updateMessage(`Initializing ${config.label}...`, 60);
        }

        // Call init function (it may contain API calls)
        const initFn = window[config.init];
        if (typeof initFn === 'function') {
            const result = initFn();
            // If it returns a promise, wait for it
            if (result && typeof result.then === 'function') {
                await result;
            }
        }

        // Small delay for UI updates
        await new Promise(r => setTimeout(r, 300));

    } catch (error) {
        console.error('Router error:', error);
    } finally {
        // Complete initial load - this unlocks all subsequent API calls
        if (window.loadingManager) {
            window.loadingManager.completeInitialLoad();
            console.log('✅ Initial load complete - UI unlocked');
        }
    }
}

// ============================================================
// START
// ============================================================
document.addEventListener('DOMContentLoaded', () => {
    initNavigation();
    initRouter();
});

// Handle if DOM already loaded
if (document.readyState === 'complete' || document.readyState === 'interactive') {
    if (!window._routerStarted) {
        window._routerStarted = true;
        initNavigation();
        initRouter();
    }
}