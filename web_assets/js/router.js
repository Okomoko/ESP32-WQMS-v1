// js/router.js
// Dynamic page loader - loads only the JavaScript needed for each page

// ============================================================
// Get the base path of the current script
// ============================================================
function getScriptBasePath() {
    // Get the current script element
    const currentScript = document.currentScript;
    if (!currentScript) {
        // Fallback: use the first script with src containing 'router.js'
        const scripts = document.querySelectorAll('script[src]');
        for (let s of scripts) {
            if (s.src && s.src.includes('router.js')) {
                return s.src.substring(0, s.src.lastIndexOf('/') + 1);
            }
        }
        return '/';  // Fallback to root
    }
    
    // Get the full URL of the current script
    const scriptUrl = currentScript.src;
    // Extract the directory path (everything up to the last '/')
    return scriptUrl.substring(0, scriptUrl.lastIndexOf('/') + 1);
}

// ============================================================
// Page Loader
// ============================================================
function loadPageScript(page) {
    const scripts = {
        'dashboard': 'dashboard.js',
        'automation': 'automation.js',
        'config': 'config.js',
        'calibration': 'calibration.js',
        'api_docs': 'api_docs.js'
    };
    
    const scriptName = scripts[page];
    if (!scriptName) {
        console.warn('No script found for page:', page);
        return;
    }
    
    // ✅ Get the base path dynamically
    const basePath = getScriptBasePath();
    const scriptPath = basePath + scriptName;
    
    // Check if already loaded
    if (document.querySelector(`script[src="${scriptPath}"]`)) {
        console.log('Script already loaded:', scriptPath);
        return;
    }
    
    const script = document.createElement('script');
    script.src = scriptPath;
    script.async = true;
    document.head.appendChild(script);
    console.log('Loading script:', scriptPath);
    return script;
}

// ============================================================
// Script Loaded Handler - returns a Promise
// ============================================================
function loadScriptAndWait(page) {
    const scriptMap = {
        'dashboard': 'dashboard.js',
        'automation': 'automation.js',
        'config': 'config.js',
        'calibration': 'calibration.js',
        'api_docs': 'api_docs.js'
    };
    
    const scriptName = scriptMap[page];
    if (!scriptName) {
        return Promise.resolve();
    }
    
    // ✅ Get the base path dynamically
    const basePath = getScriptBasePath();
    const scriptPath = basePath + scriptName;
    
    // Check if already loaded
    const existingScript = document.querySelector(`script[src="${scriptPath}"]`);
    if (existingScript) {
        return Promise.resolve();
    }
    
    // Load the script and wait for it
    return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.src = scriptPath;
        script.async = true;
        script.onload = function() {
            console.log('Script loaded:', scriptPath);
            resolve();
        };
        script.onerror = function() {
            console.error('Failed to load script:', scriptPath);
            reject(new Error('Failed to load script: ' + scriptPath));
        };
        document.head.appendChild(script);
    });
}

// ============================================================
// Router
// ============================================================
async function initRouter() {
    const path = window.location.pathname;
    let page = 'dashboard';
    
    if (path.includes('automation')) {
        page = 'automation';
    } else if (path.includes('config')) {
        page = 'config';
    } else if (path.includes('calibration')) {
        page = 'calibration';
    } else if (path.includes('api_doc')) {
        page = 'api_docs';
    } else {
        page = 'dashboard';
    }
    
    console.log('Page detected:', page);
    console.log('Script base path:', getScriptBasePath());
    
    // Load page-specific script and wait for it
    try {
        await loadScriptAndWait(page);
        
        // Now call the initialization function
        switch (page) {
            case 'dashboard':
                if (typeof initDashboard === 'function') {
                    initDashboard();
                } else {
                    console.error('initDashboard not found after loading dashboard.js');
                }
                break;
            case 'automation':
                if (typeof initAutomation === 'function') {
                    initAutomation();
                } else {
                    console.error('initAutomation not found after loading automation.js');
                }
                break;
            case 'config':
                if (typeof initConfiguration === 'function') {
                    initConfiguration();
                } else {
                    console.error('initConfiguration not found after loading config.js');
                }
                break;
            case 'calibration':
                if (typeof initCalibration === 'function') {
                    initCalibration();
                } else {
                    console.error('initCalibration not found after loading calibration.js');
                }
                break;
            case 'api_docs':
                if (typeof initApiDocs === 'function') {
                    initApiDocs();
                } else {
                    console.error('initApiDocs not found after loading api_docs.js');
                }
                break;
            default:
                console.warn('Unknown page:', page);
        }
    } catch (e) {
        console.error('Failed to load page script:', e);
    }
}

// ============================================================
// DOM Ready - Main Entry
// ============================================================
document.addEventListener('DOMContentLoaded', function() {
    initNavigation();
    initRouter();
});