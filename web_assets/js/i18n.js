// js/i18n.js
// Multilingual engine for WQMS web UI
//  - Loads /i18n/<lang>.json on demand
//  - t("key", {vars}) for JS strings
//  - applyTranslations() walks the DOM and translates [data-i18n] elements
//  - Plurals via Intl.PluralRules with per-language suffix fallback (_one/_few/_many/_other)
//  - HTML-safe key allow-list (keys whose value may contain HTML)
//  - Sets <html dir> and <html lang> dynamically
//  - Persists language in localStorage
//  - Populates a <select id="lang-switch"> if present

(function () {
    'use strict';

    // ============================================================
    // CONFIG
    // ============================================================
    const I18N_BASE_PATH = '/i18n/';
    const META_PATH = I18N_BASE_PATH + 'meta.json';
    const STORAGE_KEY = 'wqms_lang';
    const DEFAULT_LANG = 'en';

    // Keys whose translated value may contain HTML (rendered via innerHTML).
    // Keep this list as small as possible.
    const HTML_KEYS = new Set([
        'automation.connect_hint'
    ]);

    // Cache of loaded dictionaries and plural rules
    const cache = { meta: null, dicts: {}, pluralRules: {} };
    let currentLang = DEFAULT_LANG;
    let currentDict = {};
    let currentDir = 'ltr';

    // ============================================================
    // INTERNALS
    // ============================================================

    function getStoredLang() {
        try {
            return localStorage.getItem(STORAGE_KEY) || DEFAULT_LANG;
        } catch (_) {
            return DEFAULT_LANG;
        }
    }

    function storeLang(lang) {
        try { localStorage.setItem(STORAGE_KEY, lang); } catch (_) {}
    }

    function setHtmlAttrs(lang, dir) {
        document.documentElement.setAttribute('lang', lang);
        document.documentElement.setAttribute('dir', dir || 'ltr');
    }

    function getPluralRule(lang) {
        if (cache.pluralRules[lang]) return cache.pluralRules[lang];
        let rule;
        try {
            rule = new Intl.PluralRules(lang);
        } catch (_) {
            // Fallback: treat everything as "other"
            rule = { select: () => 'other' };
        }
        cache.pluralRules[lang] = rule;
        return rule;
    }

    // Look up a key in a dict, honoring plurals and fallback to English
    function lookup(dict, key, vars) {
        // Plural case: if the key has a suffix (_one/_few/…) it's used literally.
        // If it's a bare key and vars.count is numeric, try plural variants.
        let raw = dict[key];

        if (raw === undefined && vars && typeof vars.n === 'number') {
            const rule = getPluralRule(currentLang);
            const form = rule.select(vars.n); // 'one' | 'few' | 'many' | 'other'
            const candidate = key + '_' + form;
            if (dict[candidate] !== undefined) {
                raw = dict[candidate];
            } else if (dict[key + '_other'] !== undefined) {
                raw = dict[key + '_other'];
            }
        }

        if (raw === undefined) {
            // Try English fallback
            const en = cache.dicts['en'];
            if (en && en[key] !== undefined) {
                raw = en[key];
            } else {
                // Last resort: return key so it's obvious what's missing
                return key;
            }
        }
        return raw;
    }

    function interpolate(str, vars) {
        if (!vars) return str;
        return str.replace(/\{(\w+)\}/g, function (m, name) {
            return vars[name] !== undefined ? String(vars[name]) : m;
        });
    }

    // ============================================================
    // LOADING
    // ============================================================
    async function loadMeta() {
        if (cache.meta) return cache.meta;
        const r = await fetch(META_PATH, { cache: 'no-store' });
        if (!r.ok) throw new Error('meta.json not found');
        cache.meta = await r.json();
        return cache.meta;
    }

    async function loadDict(lang) {
        if (cache.dicts[lang]) return cache.dicts[lang];
        const r = await fetch(I18N_BASE_PATH + lang + '.json', { cache: 'no-store' });
        if (!r.ok) throw new Error('Language file not found: ' + lang);
        const dict = await r.json();
        cache.dicts[lang] = dict;
        return dict;
    }

    // ============================================================
    // PUBLIC API
    // ============================================================

    /**
     * Translate a key.
     * @param {string} key - dot-separated translation key
     * @param {object} [vars] - interpolation variables. If vars.n is numeric,
     *                          tries plural suffixes (_one/_few/_many/_other).
     * @returns {string}
     */
    function t(key, vars) {
        const raw = lookup(currentDict, key, vars);
        return interpolate(raw, vars);
    }

    /**
     * Same as t() but returns HTML string for known HTML-safe keys.
     * For unknown keys, falls back to t() (safe behavior).
     */
    function th(key, vars) {
        return HTML_KEYS.has(key) ? t(key, vars) : t(key, vars);
    }

    /**
     * Apply translations to all elements with data-i18n / data-i18n-target.
     */
    function applyTranslations() {
        // Simple text content
        document.querySelectorAll('[data-i18n]').forEach(function (el) {
            const key = el.getAttribute('data-i18n');
            if (!key) return;
            const vars = readVars(el);
            const val = t(key, vars);
            if (HTML_KEYS.has(key)) {
                el.innerHTML = val;
            } else {
                el.textContent = val;
            }
        });

        // Attribute-based (placeholder, title, aria-label, alt)
        document.querySelectorAll('[data-i18n-target]').forEach(function (el) {
            const key = el.getAttribute('data-i18n-target');
            const target = el.getAttribute('data-i18n-attr') || 'placeholder';
            if (!key) return;
            const vars = readVars(el);
            el.setAttribute(target, t(key, vars));
        });

        // <option> elements sometimes need to preserve their value attribute
        // — value is not touched, only textContent is translated.

        // Update language select value if present
        const sel = document.getElementById('lang-switch');
        if (sel) sel.value = currentLang;
    }

    function readVars(el) {
        // Read data-i18n-vars='{"n":5}' — optional per-element overrides
        const s = el.getAttribute('data-i18n-vars');
        if (!s) return null;
        try { return JSON.parse(s); } catch (_) { return null; }
    }

    /**
     * Switch language, load dict if needed, re-apply translations.
     * @param {string} lang
     */
    async function setLanguage(lang) {
        if (!lang) return;
        // Ensure we know the language's dir
        let dir = 'ltr';
        try {
            const meta = await loadMeta();
            const entry = meta.languages.find(function (l) { return l.code === lang; });
            if (entry) dir = entry.dir || 'ltr';
        } catch (_) { /* meta missing: proceed */ }

        // Attempt to load the dict; on failure fall back to English
        let dict;
        try {
            dict = await loadDict(lang);
        } catch (e) {
            console.warn('[i18n] failed to load', lang, e.message, '- falling back to en');
            lang = DEFAULT_LANG;
            dir = 'ltr';
            dict = await loadDict(DEFAULT_LANG);
        }

        currentLang = lang;
        currentDict = dict;
        currentDir = dir;
        setHtmlAttrs(lang, dir);
        storeLang(lang);
        applyTranslations();
        document.dispatchEvent(new CustomEvent('i18n:changed', {
            detail: { lang: lang, dir: dir }
        }));
    }

    /**
     * Populate a <select id="lang-switch"> with available languages.
     * Safe to call multiple times.
     */
    async function populateLanguageSelector() {
        const sel = document.getElementById('lang-switch');
        if (!sel) return;
        let meta;
        try {
            meta = await loadMeta();
        } catch (e) {
            console.warn('[i18n] meta.json not available, hiding language selector');
            sel.style.display = 'none';
            return;
        }
        sel.innerHTML = '';
        meta.languages.forEach(function (l) {
            const opt = document.createElement('option');
            opt.value = l.code;
            opt.textContent = l.name;
            sel.appendChild(opt);
        });
        sel.value = currentLang;
        sel.addEventListener('change', function () {
            setLanguage(this.value);
        });
    }

    /**
     * Boot: pick initial language (stored > meta.default > en),
     * load dict, apply, populate selector, hook "DOMContentLoaded" late additions.
     */
    async function init() {
        // Load meta first so we know the default
        let meta = null;
        try { meta = await loadMeta(); } catch (_) {}
        const stored = getStoredLang();
        const initial = stored || (meta && meta.default) || DEFAULT_LANG;
        await setLanguage(initial);
        await populateLanguageSelector();

        // Observe DOM for dynamically added [data-i18n] elements
        // (renders from JS after initial load — e.g. sensor cards).
        const observer = new MutationObserver(function (muts) {
            let needsApply = false;
            for (const m of muts) {
                for (const node of m.addedNodes) {
                    if (node.nodeType === 1) {
                        if (node.hasAttribute && (node.hasAttribute('data-i18n') || node.hasAttribute('data-i18n-target'))) {
                            needsApply = true; break;
                        }
                        if (node.querySelector && node.querySelector('[data-i18n],[data-i18n-target]')) {
                            needsApply = true; break;
                        }
                    }
                }
                if (needsApply) break;
            }
            if (needsApply) applyTranslations();
        });
        observer.observe(document.body, { childList: true, subtree: true });
    }

    // ============================================================
    // PUBLIC (window) EXPORTS
    // ============================================================
    window.i18n = {
        t: t,
        th: th,
        setLanguage: setLanguage,
        applyTranslations: applyTranslations,
        populateLanguageSelector: populateLanguageSelector,
        getLang: function () { return currentLang; },
        getDir: function () { return currentDir; }
    };

    // Convenience global — t('key', {n:1})
    window.t = t;

    // Boot
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();