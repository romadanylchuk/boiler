'use strict';

// ─── Auth ──────────────────────────────────────────────────────────────────────
let userRole = null; // 'admin' | 'operator' | null
let loginFailCount = 0;
let apMode = false;

function getToken() { return localStorage.getItem('apiToken') || ''; }
function saveToken(t) { localStorage.setItem('apiToken', t); }

// ─── API helpers ──────────────────────────────────────────────────────────────
async function apiGet(path) {
    const token = getToken();
    const headers = token ? { 'Authorization': 'Bearer ' + token } : {};
    return fetch(path, { headers });
}

async function apiPost(path, body) {
    const token = getToken();
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers['Authorization'] = 'Bearer ' + token;
    return fetch(path, { method: 'POST', headers, body: JSON.stringify(body) });
}

async function apiPostForm(path, data) {
    return fetch(path, { method: 'POST', body: new URLSearchParams(data) });
}

// ─── Router ───────────────────────────────────────────────────────────────────
let currentView = null;
let refreshTimer = null;

function stopRefresh() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
}

function navigate(view) {
    stopRefresh();
    currentView = view;
    renderApp();
}

// ─── Cached data ──────────────────────────────────────────────────────────────
let cachedState = null;
let cachedLog = null;
let settingsCfg = null;
let settingsTab = 'control';

async function fetchState() {
    try {
        const res = await apiGet('/api/state');
        if (res.ok) { cachedState = await res.json(); return true; }
        if (res.status === 401) { userRole = null; navigate('login'); }
    } catch (_) {}
    return false;
}

async function fetchLog() {
    try {
        const res = await apiGet('/api/log');
        if (res.ok) { cachedLog = await res.json(); return true; }
    } catch (_) {}
    return false;
}

// ─── Root render ──────────────────────────────────────────────────────────────
function renderApp() {
    const root = document.getElementById('root');
    if (!root) return;

    if (currentView === 'setup')   { root.innerHTML = tplSetup();   bindSetup();   return; }
    if (currentView === 'login')   { root.innerHTML = tplLogin();   bindLogin();   return; }
    if (currentView === 'resetpw') { root.innerHTML = tplResetPw(); bindResetPw(); return; }

    const navLinks = [
        ['dashboard', 'Dashboard'],
        ['log', 'Log'],
        ...(userRole === 'admin' ? [['settings', 'Settings'], ['test', 'Test \u2697']] : []),
    ].map(([v, l]) => `<a onclick="navigate('${v}')" class="${currentView===v?'active':''}">${l}</a>`).join('');

    root.innerHTML = `
        <nav>
            <span class="brand">&#9889; Boiler</span>
            ${navLinks}
            <span class="spacer"></span>
            <span class="role-badge">${userRole || ''}</span>
            <button class="logout-btn" onclick="doLogout()">Logout</button>
        </nav>
        <main id="view"></main>`;

    if (currentView === 'dashboard') loadDashboard();
    else if (currentView === 'log') loadLog();
    else if (currentView === 'settings') loadSettings();
    else if (currentView === 'test') loadTest();
}

function view() { return document.getElementById('view'); }

// ─── Dashboard ────────────────────────────────────────────────────────────────
async function loadDashboard() {
    const v = view(); if (v) v.innerHTML = '<div class="spinner">Loading&hellip;</div>';
    await fetchState();
    renderDashboard();
    refreshTimer = setInterval(async () => {
        if (currentView !== 'dashboard') return;
        await fetchState();
        renderDashboard();
    }, 3000);
}

function renderDashboard() {
    const v = view(); if (!v) return;
    if (!cachedState) { v.innerHTML = '<div class="spinner">No data</div>'; return; }

    const { sensors: s, relays: r, status: st, alarms: al } = cachedState;

    const alarmList = [
        [al.overheat,             'Overheat (&gt;80&deg;C) &mdash; heater stopped'],
        [al.flowSensorFault,      'Flow sensor fault &mdash; heater stopped'],
        [al.returnSensorFault,    'Return sensor fault'],
        [al.roomSensorLost,       'Room sensor lost'],
        [al.outsideSensorLost,    'Outside sensor lost'],
        [al.bleRoomLowBattery,    'Room BLE sensor: low battery'],
        [al.bleOutsideLowBattery, 'Outside BLE sensor: low battery'],
    ].filter(([f]) => f).map(([, m]) => `<div class="alarm-item">${m}</div>`).join('');

    const phaseLabels = {
        idle: 'Idle', pump_pre: 'Pump Pre-heat',
        heating: 'Heating', pump_post: 'Pump Cooldown', pump_standby: 'Pump Standby',
    };
    const phaseLabel = phaseLabels[st.phase] || st.phase;

    v.innerHTML = `
        ${alarmList ? `<div class="alarm-banner"><div class="alarm-icon">&#9888;</div><div>${alarmList}</div></div>` : ''}

        <div class="status-bar">
            <div class="status-pill"><div class="status-dot ${st.wifiConnected?'ok':'err'}"></div>WiFi</div>
            <div class="status-pill"><div class="status-dot ${st.mqttConnected?'ok':''}"></div>MQTT</div>
            <div class="status-pill"><div class="status-dot ${st.ntpSynced?'ok':''}"></div>NTP</div>
            ${st.haRemoteDisable ? '<div class="status-pill"><div class="status-dot warn"></div>HA Disabled</div>' : ''}
            ${st.otaInProgress ? '<div class="status-pill"><div class="status-dot warn"></div>OTA&hellip;</div>' : ''}
        </div>

        <div class="card">
            <h3>Temperatures</h3>
            <div class="grid-4">
                ${tempCard('Flow',    s.flowTemp,    s.flowFault,   st.activeFlowSetpoint,   al.flowSensorFault)}
                ${tempCard('Return',  s.returnTemp,  s.returnFault, st.activeReturnSetpoint, al.returnSensorFault)}
                ${tempCard('Room',    s.roomTemp,    false,         null,                    al.roomSensorLost)}
                ${tempCard('Outside', s.outsideTemp, false,         null,                    al.outsideSensorLost)}
            </div>
        </div>

        <div class="grid-2">
            <div class="card">
                <h3>Relays &amp; Phase</h3>
                <div class="relay-row">
                    <div class="relay-dot ${r.heater?'on heater':''}"></div>
                    <div class="relay-label">Heater</div>
                    <div class="relay-state ${r.heater?'on heater':''}">${r.heater?'ON':'OFF'}</div>
                </div>
                <div class="relay-row">
                    <div class="relay-dot ${r.pump?'on':''}"></div>
                    <div class="relay-label">Pump</div>
                    <div class="relay-state ${r.pump?'on':''}">${r.pump?'ON':'OFF'}</div>
                </div>
                <div style="margin-top:12px">
                    <span class="phase-badge ${st.phase}">${phaseLabel}</span>
                </div>
            </div>

            <div class="card">
                <h3>Mode</h3>
                <div class="mode-buttons">
                    <button class="mode-btn ${st.mode==='off'?'active-off':''}" onclick="setMode('off')">OFF</button>
                    <button class="mode-btn ${st.mode==='on'?'active-on':''}" onclick="setMode('on')">ON</button>
                    <button class="mode-btn ${st.mode==='antifreeze'?'active-antifreeze':''}" onclick="setMode('antifreeze')">Antifreeze</button>
                </div>
                <div style="margin-top:14px;font-size:12px;color:#475569">
                    Setpoints: flow <strong>${st.activeFlowSetpoint}&deg;C</strong>,
                    return <strong>${st.activeReturnSetpoint}&deg;C</strong>
                </div>
            </div>
        </div>
        <div class="card">
            <h3>Display Buttons</h3>
            <div class="dsp-btn-panel">
                <button class="dsp-btn" data-btn="up"       onclick="pressButton('up')"       title="Up">&#9650;</button>
                <button class="dsp-btn" data-btn="down"     onclick="pressButton('down')"     title="Down">&#9660;</button>
                <button class="dsp-btn" data-btn="enter"    onclick="pressButton('enter')"    title="Enter / Next screen">&#9166;</button>
                <button class="dsp-btn" data-btn="back"     onclick="pressButton('back')"     title="Back">&#9100;</button>
                <button class="dsp-btn" data-btn="settings" onclick="pressButton('settings')" title="Settings">&#9881;</button>
            </div>
        </div>
        <div class="refresh-hint">Auto-refresh every 3s</div>`;
}

function tempCard(label, value, fault, setpoint, alarmFlag) {
    let valHtml;
    if (fault) {
        valHtml = '<div class="t-value hot">ERR</div>';
    } else if (value === null || value === undefined) {
        valHtml = '<div class="t-value na">&mdash;</div>';
    } else {
        const n = parseFloat(value);
        const cls = n >= 70 ? 'hot' : n >= 50 ? 'warm' : n < 5 ? 'cool' : 'normal';
        valHtml = `<div class="t-value ${cls}">${n.toFixed(1)}&deg;</div>`;
    }
    const sub = setpoint ? `<div class="t-sub">SP: ${setpoint}&deg;C</div>` : '';
    const flt = alarmFlag ? '<div class="t-fault">&#9888; Lost</div>' : '';
    return `<div class="temp-card"><div class="t-label">${label}</div>${valHtml}${sub}${flt}</div>`;
}

async function setMode(mode) {
    const res = await apiPost('/api/command', { cmd: 'set_mode', mode });
    if (res.ok) { await fetchState(); renderDashboard(); }
}

async function pressButton(btn) {
    const el = document.querySelector(`.dsp-btn[data-btn="${btn}"]`);
    if (el) { el.classList.add('pressed'); setTimeout(() => el.classList.remove('pressed'), 200); }
    await apiPost('/api/command', { cmd: 'button', button: btn });
}

// ─── Log ──────────────────────────────────────────────────────────────────────
async function loadLog() {
    const v = view(); if (v) v.innerHTML = '<div class="spinner">Loading&hellip;</div>';
    await fetchLog();
    renderLog();
    refreshTimer = setInterval(async () => {
        if (currentView !== 'log') return;
        await fetchLog();
        renderLog();
    }, 5000);
}

const REASONS = [
    'Start conditions met',       // 0
    'Antifreeze: return cold',    // 1
    'Flow setpoint reached',      // 2
    'Room setpoint reached',      // 3
    'External thermostat',        // 4
    'HA remote disable',          // 5
    'Overheat',                   // 6
    'Mode turned off',            // 7
    'Antifreeze: return warm',    // 8
    'Antifreeze: room warm',      // 9
    'Pre-delay: flow warmed',     // 10
    'Pre-delay: return blocked',  // 11
    'Pre-delay: room warm',       // 12
    'Pre-delay: thermostat',      // 13
    'Pre-delay: HA disable',      // 14
    'Pump pre-delay start',       // 15
    'Pump standby run',           // 16
    'Pump antifreeze mode',       // 17
    'Pump post-delay complete',   // 18
    'Pump standby complete',      // 19
    'Pump antifreeze off',        // 20
    'Mode turned off',            // 21
    'Pump min time wait',         // 22
];

function fmtTs(ts) {
    if (ts < 1700000000) {
        const h = Math.floor(ts / 3600), m = Math.floor((ts % 3600) / 60), s = ts % 60;
        return `+${h}h${String(m).padStart(2,'0')}m${String(s).padStart(2,'0')}s`;
    }
    return new Date(ts * 1000).toLocaleString();
}

function renderLog() {
    const v = view(); if (!v) return;
    if (!cachedLog) { v.innerHTML = '<div class="spinner">No data</div>'; return; }

    const events = cachedLog.events || [];
    if (!events.length) {
        v.innerHTML = '<div class="card"><div class="spinner" style="padding:20px">No events recorded yet</div></div>';
        return;
    }

    const fT = val => (val !== null && val !== undefined) ? parseFloat(val).toFixed(1) + '&deg;' : '&mdash;';

    const rows = events.map(ev => {
        const t = ev.temps;
        return `<tr>
            <td style="font-size:12px;color:#64748b;white-space:nowrap">${fmtTs(ev.ts)}</td>
            <td><span class="relay-chip ${ev.relay}">${ev.relay.replace('_', ' ')}</span></td>
            <td style="color:#94a3b8;font-size:13px">${REASONS[ev.reason] !== undefined ? REASONS[ev.reason] : '#' + ev.reason}</td>
            <td style="font-size:12px;color:#64748b;white-space:nowrap">${fT(t.flow)} / ${fT(t.return)}</td>
            <td style="font-size:12px;color:#475569;white-space:nowrap">${ev.flowSp}&deg; / ${ev.returnSp}&deg;</td>
        </tr>`;
    }).join('');

    v.innerHTML = `
        <div class="card">
            <h3>Event Log &mdash; ${cachedLog.count} event${cachedLog.count===1?'':'s'}, newest first</h3>
            <div class="log-wrap">
                <table class="log-table">
                    <thead><tr>
                        <th>Time</th><th>Event</th><th>Reason</th>
                        <th>Flow / Return</th><th>Setpoint</th>
                    </tr></thead>
                    <tbody>${rows}</tbody>
                </table>
            </div>
        </div>
        <div class="refresh-hint">Auto-refresh every 5s</div>`;
}

// ─── Settings ─────────────────────────────────────────────────────────────────
async function loadSettings() {
    if (userRole !== 'admin') { navigate('dashboard'); return; }
    const v = view(); if (v) v.innerHTML = '<div class="spinner">Loading&hellip;</div>';
    try {
        const res = await apiGet('/api/config');
        if (res.ok) settingsCfg = await res.json();
        else { navigate('dashboard'); return; }
    } catch (_) { navigate('dashboard'); return; }
    renderSettings();
}

function renderSettings() {
    const v = view(); if (!v || !settingsCfg) return;
    const tabs = [
        ['control', 'Control'],
        ['sensors', 'Sensors'],
        ['network', 'Network'],
        ['security', 'Security'],
        ['system', 'System'],
    ];
    const tabHtml = tabs.map(([id, label]) =>
        `<button class="tab ${settingsTab===id?'active':''}" onclick="switchTab('${id}')">${label}</button>`
    ).join('');

    const content = settingsTab === 'control'  ? tplControl(settingsCfg)
                  : settingsTab === 'sensors'  ? tplSensors(settingsCfg)
                  : settingsTab === 'network'  ? tplNetwork(settingsCfg)
                  : settingsTab === 'security' ? tplSecurity(settingsCfg)
                  : tplSystem();

    v.innerHTML = `
        <div class="card">
            <div class="tabs">${tabHtml}</div>
            <form id="sf" onsubmit="return false">
                ${content}
                ${settingsTab !== 'system' ? '<button class="btn btn-primary" onclick="saveSettings()">Save</button>' : ''}
            </form>
            <div id="sfmsg" style="margin-top:12px"></div>
        </div>`;
}

function switchTab(tab) { settingsTab = tab; renderSettings(); }

function tplControl(c) {
    const tOpts = ['NO','NC'].map(v =>
        `<option value="${v}" ${c.thermostatMode===v?'selected':''}>${v==='NO'?'Normally Open (default)':'Normally Closed'}</option>`
    ).join('');
    return `
        <div class="form-section">
            <h4>Flow &amp; Return Setpoints</h4>
            <div class="form-grid">
                <div class="form-group"><label>Flow Setpoint &deg;C (20&ndash;65)</label>
                    <input type="number" name="flowSetpoint" min="20" max="65" value="${c.flowSetpoint}"></div>
                <div class="form-group"><label>Flow Hysteresis &deg;C (1&ndash;10)</label>
                    <input type="number" name="flowHysteresis" min="1" max="10" value="${c.flowHysteresis}"></div>
                <div class="form-group"><label>Return Setpoint &deg;C (20&ndash;65)</label>
                    <input type="number" name="returnSetpoint" min="20" max="65" value="${c.returnSetpoint}"></div>
                <div class="form-group"><label>Return Hysteresis &deg;C (1&ndash;10)</label>
                    <input type="number" name="returnHysteresis" min="1" max="10" value="${c.returnHysteresis}"></div>
            </div>
        </div>
        <div class="form-section">
            <h4>Room Setpoint</h4>
            <div class="form-grid">
                <div class="form-group"><label>Room Setpoint &deg;C (5&ndash;30)</label>
                    <input type="number" name="roomSetpoint" min="5" max="30" value="${c.roomSetpoint}"></div>
                <div class="form-group"><label>Room Hysteresis &deg;C (1&ndash;5)</label>
                    <input type="number" name="roomHysteresis" min="1" max="5" value="${c.roomHysteresis}"></div>
            </div>
        </div>
        <div class="form-section">
            <h4>Pump Timing</h4>
            <div class="form-grid">
                <div class="form-group"><label>Pre / Post Delay s (30&ndash;120)</label>
                    <input type="number" name="pumpPrePostDelaySec" min="30" max="120" value="${c.pumpPrePostDelaySec}"></div>
                <div class="form-group"><label>Standby Period min (30&ndash;180)</label>
                    <input type="number" name="standbyPumpPeriodMin" min="30" max="180" value="${c.standbyPumpPeriodMin}"></div>
                <div class="form-group"><label>Standby Duration min (1&ndash;5)</label>
                    <input type="number" name="standbyPumpDurationMin" min="1" max="5" value="${c.standbyPumpDurationMin}"></div>
                <div class="form-group"><label>Min Heater Off s (60&ndash;180)</label>
                    <input type="number" name="minHeaterOffSec" min="60" max="180" value="${c.minHeaterOffSec}"></div>
            </div>
        </div>
        <div class="form-section">
            <h4>External Thermostat Contact</h4>
            <div class="form-group"><label>Contact Mode</label>
                <select name="thermostatMode">${tOpts}</select></div>
        </div>`;
}

function tplSensorSource(prefix, cfg) {
    const modeOpts = ['none','ble','api'].map(v =>
        `<option value="${v}" ${cfg.mode===v?'selected':''}>${v.toUpperCase()}</option>`
    ).join('');
    const bleFields = cfg.mode === 'ble' ? `
        <div class="form-group"><label>BLE MAC Address (AA:BB:CC:DD:EE:FF)</label>
            <input type="text" name="${prefix}BleMac" value="${escHtml(cfg.bleMac)}" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17"></div>` : '';
    const apiFields = cfg.mode === 'api' ? `
        <div class="form-group"><label>HTTP URL</label>
            <input type="text" name="${prefix}ApiUrl" value="${escHtml(cfg.apiUrl)}" placeholder="http://192.168.1.x/temp"></div>
        <div class="form-group"><label>JSON field name (e.g. <code>temp</code>)</label>
            <input type="text" name="${prefix}ApiJsonPath" value="${escHtml(cfg.apiJsonPath)}" placeholder="temperature"></div>
        <div class="form-group"><label>Poll interval s</label>
            <input type="number" name="${prefix}ApiPollSec" min="10" max="3600" value="${cfg.apiPollSec}"></div>` : '';
    return `
        <div class="form-group"><label>Source</label>
            <select name="${prefix}Mode" onchange="changeSensorMode('${prefix}', this.value)">${modeOpts}</select></div>
        ${bleFields}${apiFields}`;
}

function tplSensors(c) {
    const curveRows = (c.curve || []).map((pt, i) => `
        <div class="curve-row">
            <input type="number" class="cr-out" value="${pt.outsideTemp}" placeholder="0">
            <input type="number" class="cr-flow" min="20" max="90" value="${pt.flowSetpoint}" placeholder="55">
            <input type="number" class="cr-ret" min="20" max="90" value="${pt.returnSetpoint}" placeholder="45">
            <button type="button" class="rm-btn" onclick="removeCurve(${i})" title="Remove">&#215;</button>
        </div>`).join('');
    const curveEnabled = (c.curveCount || 0) >= 3;
    return `
        <div class="form-section">
            <h4>Room Sensor</h4>
            ${tplSensorSource('roomSensor', c.roomSensor)}
        </div>
        <div class="form-section">
            <h4>Outside Sensor</h4>
            ${tplSensorSource('outsideSensor', c.outsideSensor)}
        </div>
        <div class="form-section">
            <h4>Weather Compensation Curve ${curveEnabled ? '&#10003; active' : '(disabled &mdash; need 3&ndash;5 points)'}</h4>
            <div class="curve-header">
                <span>Outside &deg;C</span><span>Flow SP &deg;C</span><span>Return SP &deg;C</span><span></span>
            </div>
            <div id="curve-rows">${curveRows}</div>
            <button type="button" class="add-curve-btn" onclick="addCurve()">+ Add point</button>
            <div class="info-text">Sorted by outside temp automatically. 0 points = disabled. Active with 3&ndash;5 points.</div>
        </div>`;
}

function tplNetwork(c) {
    return `
        <div class="form-section">
            <h4>WiFi</h4>
            <div class="form-group"><label>SSID</label>
                <input type="text" name="wifiSsid" value="${escHtml(c.wifiSsid||'')}" autocomplete="off"></div>
            <div class="form-group"><label>Password (leave blank to keep current)</label>
                <input type="password" name="wifiPass" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;" autocomplete="new-password"></div>
        </div>
        <div class="form-section">
            <h4>MQTT</h4>
            <div class="form-group"><label>Broker host / IP</label>
                <input type="text" name="mqttBroker" value="${escHtml(c.mqttBroker||'')}" placeholder="192.168.1.x"></div>
            <div class="form-grid">
                <div class="form-group"><label>Port</label>
                    <input type="number" name="mqttPort" min="1" max="65535" value="${c.mqttPort||1883}"></div>
                <div class="form-group"><label>Client ID</label>
                    <input type="text" name="mqttClientId" value="${escHtml(c.mqttClientId||'boiler')}"></div>
            </div>
            <div class="form-group"><label>Username</label>
                <input type="text" name="mqttUser" value="${escHtml(c.mqttUser||'')}" autocomplete="off"></div>
            <div class="form-group"><label>Password (leave blank to keep current)</label>
                <input type="password" name="mqttPass" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;" autocomplete="new-password"></div>
        </div>
        <div class="form-section">
            <h4>NTP</h4>
            <div class="form-group"><label>Server 1</label>
                <input type="text" name="ntpServer1" value="${escHtml(c.ntpServer1||'pool.ntp.org')}"></div>
            <div class="form-group"><label>Server 2</label>
                <input type="text" name="ntpServer2" value="${escHtml(c.ntpServer2||'time.google.com')}"></div>
            <div class="form-group"><label>Timezone (POSIX TZ string)</label>
                <input type="text" name="timezone" value="${escHtml(c.timezone||'')}" placeholder="EET-2EEST,M3.5.0,M10.5.0/3"></div>
        </div>`;
}

function tplSecurity(c) {
    return `
        <div class="form-section">
            <h4>Admin Account &mdash; <em>${escHtml(c.adminUser)}</em></h4>
            <div class="form-group"><label>New password (min 8 chars, leave blank to keep)</label>
                <div class="pw-wrap">
                    <input id="sec-ap" type="password" name="adminPassword" placeholder="New password" autocomplete="new-password">
                    <button type="button" class="pw-show" onclick="togglePw('sec-ap',this)">Show</button>
                </div></div>
        </div>
        <div class="form-section">
            <h4>Operator Account &mdash; <em>${escHtml(c.operatorUser)}</em></h4>
            <div class="form-group"><label>New password (min 8 chars, leave blank to keep)</label>
                <div class="pw-wrap">
                    <input id="sec-op" type="password" name="operatorPassword" placeholder="New password" autocomplete="new-password">
                    <button type="button" class="pw-show" onclick="togglePw('sec-op',this)">Show</button>
                </div></div>
        </div>
        <div class="form-section">
            <h4>Password Reset Phrase</h4>
            <div class="form-group"><label>Secret phrase (used to reset admin password, min 4 chars)</label>
                <input type="text" name="resetPhrase" value="${escHtml(c.resetPhrase||'secret')}" autocomplete="off"></div>
        </div>
        <div class="form-section">
            <h4>API Token (Bearer authentication)</h4>
            <div class="token-box">${escHtml(c.apiToken||'')}</div>
            <div style="display:flex;gap:8px;margin-top:8px;flex-wrap:wrap">
                <button type="button" class="btn btn-secondary" onclick="copyToken()">Copy token</button>
                <button type="button" class="btn btn-secondary" onclick="storeToken()">Store in browser</button>
            </div>
            <div class="info-text">Stored token is used automatically for API requests (Authorization: Bearer).</div>
        </div>`;
}

// Sensors tab: sync current DOM values into settingsCfg before re-render
function syncSensorsForm() {
    if (settingsTab !== 'sensors') return;
    const g = name => { const e = document.querySelector(`[name="${name}"]`); return e ? e.value : null; };
    const gi = name => { const v = g(name); return v !== null ? parseInt(v) || 0 : null; };
    const m = g('roomSensorMode');
    if (m !== null) settingsCfg.roomSensor.mode = m;
    const rMac = g('roomSensorBleMac');    if (rMac !== null) settingsCfg.roomSensor.bleMac = rMac;
    const rUrl = g('roomSensorApiUrl');    if (rUrl !== null) settingsCfg.roomSensor.apiUrl = rUrl;
    const rPath = g('roomSensorApiJsonPath'); if (rPath !== null) settingsCfg.roomSensor.apiJsonPath = rPath;
    const rPoll = gi('roomSensorApiPollSec'); if (rPoll !== null) settingsCfg.roomSensor.apiPollSec = rPoll;
    const om = g('outsideSensorMode');
    if (om !== null) settingsCfg.outsideSensor.mode = om;
    const oMac = g('outsideSensorBleMac');    if (oMac !== null) settingsCfg.outsideSensor.bleMac = oMac;
    const oUrl = g('outsideSensorApiUrl');    if (oUrl !== null) settingsCfg.outsideSensor.apiUrl = oUrl;
    const oPath = g('outsideSensorApiJsonPath'); if (oPath !== null) settingsCfg.outsideSensor.apiJsonPath = oPath;
    const oPoll = gi('outsideSensorApiPollSec'); if (oPoll !== null) settingsCfg.outsideSensor.apiPollSec = oPoll;
    // Sync curve rows
    const rows = document.querySelectorAll('#curve-rows .curve-row');
    const pts = [];
    rows.forEach(row => {
        const out = parseInt(row.querySelector('.cr-out').value);
        const fl  = parseInt(row.querySelector('.cr-flow').value);
        const ret = parseInt(row.querySelector('.cr-ret').value);
        if (!isNaN(out) && !isNaN(fl) && !isNaN(ret)) pts.push({ outsideTemp: out, flowSetpoint: fl, returnSetpoint: ret });
    });
    settingsCfg.curve = pts.sort((a, b) => a.outsideTemp - b.outsideTemp);
    settingsCfg.curveCount = pts.length >= 3 ? pts.length : 0;
}

function changeSensorMode(prefix, mode) {
    syncSensorsForm();
    if (prefix === 'roomSensor') settingsCfg.roomSensor.mode = mode;
    else settingsCfg.outsideSensor.mode = mode;
    renderSettings();
}

function addCurve() {
    syncSensorsForm();
    if ((settingsCfg.curve || []).length >= 5) return;
    settingsCfg.curve = settingsCfg.curve || [];
    settingsCfg.curve.push({ outsideTemp: 0, flowSetpoint: 55, returnSetpoint: 45 });
    renderSettings();
}

function removeCurve(idx) {
    syncSensorsForm();
    settingsCfg.curve.splice(idx, 1);
    settingsCfg.curveCount = settingsCfg.curve.length >= 3 ? settingsCfg.curve.length : 0;
    renderSettings();
}

async function saveSettings() {
    const f = document.getElementById('sf'); if (!f) return;
    const g  = name => { const e = f.querySelector(`[name="${name}"]`); return e ? e.value : null; };
    const gi = name => { const v = g(name); return v !== null ? parseInt(v) : null; };

    const payload = {};

    if (settingsTab === 'control') {
        const fields = ['flowSetpoint','returnSetpoint','flowHysteresis','returnHysteresis',
                        'roomSetpoint','roomHysteresis','pumpPrePostDelaySec',
                        'standbyPumpPeriodMin','standbyPumpDurationMin','minHeaterOffSec'];
        fields.forEach(k => { const v = gi(k); if (v !== null) payload[k] = v; });
        const tm = g('thermostatMode'); if (tm) payload.thermostatMode = tm;
    }

    if (settingsTab === 'sensors') {
        syncSensorsForm();
        payload.roomSensor    = settingsCfg.roomSensor;
        payload.outsideSensor = settingsCfg.outsideSensor;
        payload.curve         = settingsCfg.curve || [];
    }

    if (settingsTab === 'network') {
        if (g('wifiSsid') !== null)   payload.wifiSsid   = g('wifiSsid');
        const wp = g('wifiPass'); if (wp) payload.wifiPass = wp;
        if (g('mqttBroker') !== null) payload.mqttBroker  = g('mqttBroker');
        const mp = g('mqttPort'); if (mp) payload.mqttPort = parseInt(mp);
        if (g('mqttUser') !== null)   payload.mqttUser    = g('mqttUser');
        const mpass = g('mqttPass'); if (mpass) payload.mqttPass = mpass;
        if (g('mqttClientId') !== null) payload.mqttClientId = g('mqttClientId');
        if (g('ntpServer1') !== null) payload.ntpServer1  = g('ntpServer1');
        if (g('ntpServer2') !== null) payload.ntpServer2  = g('ntpServer2');
        if (g('timezone') !== null)   payload.timezone    = g('timezone');
    }

    if (settingsTab === 'security') {
        const ap = g('adminPassword');    if (ap && ap.length >= 8) payload.adminPassword    = ap;
        const op = g('operatorPassword'); if (op && op.length >= 8) payload.operatorPassword = op;
        const rp = g('resetPhrase');      if (rp && rp.length >= 4) payload.resetPhrase       = rp;
    }

    const msgEl = document.getElementById('sfmsg');
    try {
        const res = await apiPost('/api/config', payload);
        if (res.ok) {
            const cfgRes = await apiGet('/api/config');
            if (cfgRes.ok) settingsCfg = await cfgRes.json();
            if (msgEl) msgEl.innerHTML = '<div class="msg-ok">&#10003; Settings saved</div>';
        } else {
            const d = await res.json().catch(() => ({}));
            if (msgEl) msgEl.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Save failed')}</div>`;
        }
    } catch (_) {
        if (msgEl) msgEl.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

function copyToken() {
    const t = settingsCfg && settingsCfg.apiToken;
    if (t) navigator.clipboard.writeText(t).catch(() => {});
}

function storeToken() {
    const t = settingsCfg && settingsCfg.apiToken;
    if (t) {
        saveToken(t);
        const m = document.getElementById('sfmsg');
        if (m) m.innerHTML = '<div class="msg-ok">&#10003; Token stored in browser</div>';
    }
}

// ─── System tab (OTA + factory reset) ────────────────────────────────────────
function tplSystem() {
    return `
        <div class="form-section">
            <h4>OTA Firmware Update</h4>
            <div class="info-text" style="margin-bottom:10px">Confirm your admin password to open the firmware updater. The heater stops automatically before flashing.</div>
            <div class="form-group"><label>Admin password</label>
                <div class="pw-wrap">
                    <input id="sys-ota-pw" type="password" autocomplete="current-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                    <button type="button" class="pw-show" onclick="togglePw('sys-ota-pw',this)">Show</button>
                </div></div>
            <button type="button" class="btn btn-secondary" onclick="doOtaOpen()">Open OTA Updater</button>
            <div id="sys-ota-msg" style="margin-top:8px"></div>
        </div>
        <div class="form-section">
            <h4>Factory Reset</h4>
            <div class="msg-error" style="margin-bottom:12px">&#9888; This will erase ALL settings and credentials from flash. The device will reboot into first-boot setup mode.</div>
            <div class="form-group"><label>Admin password</label>
                <div class="pw-wrap">
                    <input id="sys-rst-pw" type="password" autocomplete="current-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                    <button type="button" class="pw-show" onclick="togglePw('sys-rst-pw',this)">Show</button>
                </div></div>
            <button type="button" class="btn btn-danger" onclick="doFactoryReset()">Reset to Factory Defaults</button>
            <div id="sys-rst-msg" style="margin-top:8px"></div>
        </div>`;
}

async function doOtaOpen() {
    const pass = (document.getElementById('sys-ota-pw') || {}).value || '';
    const msg = document.getElementById('sys-ota-msg');
    if (!pass) {
        if (msg) msg.innerHTML = '<div class="msg-error">Enter your admin password</div>';
        return;
    }
    try {
        const res = await apiPost('/api/admin-action', { action: 'verify', password: pass });
        if (res.ok) {
            window.location.href = '/update';
        } else {
            const d = await res.json().catch(() => ({}));
            if (msg) msg.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Wrong password')}</div>`;
        }
    } catch (_) {
        if (msg) msg.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

async function doFactoryReset() {
    const pass = (document.getElementById('sys-rst-pw') || {}).value || '';
    const msg = document.getElementById('sys-rst-msg');
    if (!pass) {
        if (msg) msg.innerHTML = '<div class="msg-error">Enter your admin password</div>';
        return;
    }
    if (!confirm('Factory reset will erase ALL settings and reboot to first-boot setup. Continue?')) return;
    try {
        const res = await apiPost('/api/admin-action', { action: 'factory-reset', password: pass });
        if (res.ok) {
            if (msg) msg.innerHTML = '<div class="msg-ok">&#10003; Reset complete. Device is rebooting&hellip; Redirecting in 8s.</div>';
            setTimeout(() => { userRole = null; navigate('setup'); }, 8000);
        } else {
            const d = await res.json().catch(() => ({}));
            if (msg) msg.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Reset failed')}</div>`;
        }
    } catch (_) {
        if (msg) msg.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
function togglePw(id, btn) {
    const el = document.getElementById(id);
    if (!el) return;
    const showing = el.type === 'text';
    el.type = showing ? 'password' : 'text';
    btn.textContent = showing ? 'Show' : 'Hide';
}

// ─── Login ────────────────────────────────────────────────────────────────────
function tplLogin() {
    return `
        <div class="auth-page">
            <div class="auth-card">
                <h2>&#9889; Boiler Controller</h2>
                <p class="subtitle">Sign in to continue</p>
                <div id="lmsg"></div>
                <div class="form-group"><label>Username</label>
                    <input id="lu" type="text" autocomplete="username" placeholder="name"></div>
                <div class="form-group"><label>Password</label>
                    <div class="pw-wrap">
                        <input id="lp" type="password" autocomplete="current-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                        <button type="button" class="pw-show" onclick="togglePw('lp',this)">Show</button>
                    </div></div>
                <button class="btn btn-primary" style="width:100%;margin-top:10px" onclick="doLogin()">Sign In</button>
                <div style="margin-top:16px;text-align:center;display:flex;flex-direction:column;gap:6px">
                    <a id="forgot-link" onclick="navigate('resetpw')" style="display:none;font-size:12px;color:#dc2626;cursor:pointer">Forgot admin password? Reset with secret phrase &rarr;</a>
                    ${apMode ? '<a onclick="navigate(\'setup\')" style="font-size:12px;color:#475569;cursor:pointer">First boot setup &rarr;</a>' : ''}
                </div>
            </div>
        </div>`;
}

function bindLogin() {
    ['lu','lp'].forEach(id => {
        const e = document.getElementById(id);
        if (e) e.addEventListener('keydown', ev => { if (ev.key === 'Enter') doLogin(); });
    });
    const u = document.getElementById('lu'); if (u) u.focus();
}

async function doLogin() {
    const user = (document.getElementById('lu') || {}).value || '';
    const pass = (document.getElementById('lp') || {}).value || '';
    const msg = document.getElementById('lmsg');
    if (!user || !pass) {
        if (msg) msg.innerHTML = '<div class="msg-error">Enter username and password</div>';
        return;
    }
    try {
        const res = await apiPostForm('/api/login', { username: user, password: pass });
        if (res.ok) {
            const d = await res.json();
            userRole = d.role;
            loginFailCount = 0;
            navigate('dashboard');
        } else {
            loginFailCount++;
            const d = await res.json().catch(() => ({}));
            if (msg) msg.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Invalid credentials')}</div>`;
            if (loginFailCount >= 5) {
                const fl = document.getElementById('forgot-link');
                if (fl) fl.style.display = 'block';
            }
        }
    } catch (_) {
        if (msg) msg.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

async function doLogout() {
    try { await apiPostForm('/api/logout', {}); } catch (_) {}
    userRole = null;
    navigate('login');
}

// ─── Reset Admin Password ─────────────────────────────────────────────────────
function tplResetPw() {
    return `
        <div class="auth-page">
            <div class="auth-card">
                <h2>&#9889; Reset Admin Password</h2>
                <p class="subtitle">Enter the secret phrase and a new password</p>
                <div id="rpmsg"></div>
                <div class="form-group"><label>Secret phrase</label>
                    <div class="pw-wrap">
                        <input id="rphrase" type="password" autocomplete="off" placeholder="secret">
                        <button type="button" class="pw-show" onclick="togglePw('rphrase',this)">Show</button>
                    </div></div>
                <div class="form-group"><label>New password (min 8 chars)</label>
                    <div class="pw-wrap">
                        <input id="rpnew" type="password" autocomplete="new-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                        <button type="button" class="pw-show" onclick="togglePw('rpnew',this)">Show</button>
                    </div></div>
                <button class="btn btn-primary" style="width:100%;margin-top:10px" onclick="doResetPw()">Reset Password</button>
                <div style="margin-top:12px;text-align:center">
                    <a onclick="navigate('login')" style="font-size:12px;color:#475569;cursor:pointer">&larr; Back to login</a>
                </div>
            </div>
        </div>`;
}

function bindResetPw() {
    ['rphrase','rpnew'].forEach(id => {
        const e = document.getElementById(id);
        if (e) e.addEventListener('keydown', ev => { if (ev.key === 'Enter') doResetPw(); });
    });
    const p = document.getElementById('rphrase'); if (p) p.focus();
}

async function doResetPw() {
    const phrase  = (document.getElementById('rphrase') || {}).value || '';
    const newPass = (document.getElementById('rpnew')   || {}).value || '';
    const msg = document.getElementById('rpmsg');
    if (!phrase) {
        if (msg) msg.innerHTML = '<div class="msg-error">Enter the secret phrase</div>';
        return;
    }
    if (newPass.length < 8) {
        if (msg) msg.innerHTML = '<div class="msg-error">Password must be at least 8 characters</div>';
        return;
    }
    try {
        const res = await apiPost('/api/reset-admin-password', { phrase, newPassword: newPass });
        if (res.ok) {
            if (msg) msg.innerHTML = '<div class="msg-ok">Password reset! You can now sign in.</div>';
            setTimeout(() => navigate('login'), 2000);
        } else {
            const d = await res.json().catch(() => ({}));
            if (msg) msg.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Reset failed')}</div>`;
        }
    } catch (_) {
        if (msg) msg.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

// ─── First-boot Setup ─────────────────────────────────────────────────────────
function tplSetup() {
    return `
        <div class="auth-page">
            <div class="auth-card">
                <h2>&#9889; First Boot Setup</h2>
                <p class="subtitle">Set admin password and WiFi credentials to get started</p>
                <div id="smsg"></div>
                <div class="form-group"><label>Admin Password (min 8 chars)</label>
                    <div class="pw-wrap">
                        <input id="sa" type="password" autocomplete="new-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                        <button type="button" class="pw-show" onclick="togglePw('sa',this)">Show</button>
                    </div></div>
                <div class="form-group"><label>Operator Password (optional)</label>
                    <div class="pw-wrap">
                        <input id="so" type="password" autocomplete="new-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                        <button type="button" class="pw-show" onclick="togglePw('so',this)">Show</button>
                    </div></div>
                <div class="form-group"><label>WiFi SSID</label>
                    <input id="ss" type="text" autocomplete="off" placeholder="MyNetwork"></div>
                <div class="form-group"><label>WiFi Password</label>
                    <div class="pw-wrap">
                        <input id="sw" type="password" autocomplete="new-password" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;">
                        <button type="button" class="pw-show" onclick="togglePw('sw',this)">Show</button>
                    </div></div>
                <button class="btn btn-primary" style="width:100%;margin-top:10px" onclick="doSetup()">Configure &amp; Start</button>
                <div style="margin-top:12px;text-align:center">
                    <a onclick="navigate('login')" style="font-size:12px;color:#475569;cursor:pointer">&larr; Back to login</a>
                </div>
            </div>
        </div>`;
}

function bindSetup() {
    const a = document.getElementById('sa'); if (a) a.focus();
}

async function doSetup() {
    const ap = (document.getElementById('sa') || {}).value || '';
    const op = (document.getElementById('so') || {}).value || '';
    const ss = (document.getElementById('ss') || {}).value || '';
    const sw = (document.getElementById('sw') || {}).value || '';
    const msg = document.getElementById('smsg');

    if (ap.length < 8) {
        if (msg) msg.innerHTML = '<div class="msg-error">Admin password must be at least 8 characters</div>';
        return;
    }

    const payload = { adminPassword: ap, wifiSsid: ss, wifiPass: sw };
    if (op.length >= 8) payload.operatorPassword = op;

    try {
        const res = await fetch('/api/setup', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (res.ok) {
            const d = await res.json();
            if (d.apiToken) saveToken(d.apiToken);
            if (msg) msg.innerHTML = '<div class="msg-ok">&#10003; Setup complete! Redirecting to login&hellip;</div>';
            setTimeout(() => navigate('login'), 1500);
        } else {
            const d = await res.json().catch(() => ({}));
            if (msg) msg.innerHTML = `<div class="msg-error">${escHtml(d.error || 'Setup failed')}</div>`;
        }
    } catch (_) {
        if (msg) msg.innerHTML = '<div class="msg-error">Network error</div>';
    }
}

// ─── Test Mode Tab ────────────────────────────────────────────────────────────
let testState = null;

async function loadTest() {
    const v = view(); if (v) v.innerHTML = '<div class="spinner">Loading&hellip;</div>';
    await fetchState();
    try {
        const res = await fetch('/api/test', { headers: { 'Cookie': '' } });
        // fetch with session — browser sends cookie automatically from same origin
        const r2 = await apiGet('/api/test'); // uses Bearer which won't work, but we have session
        if (r2.ok) testState = await r2.json();
    } catch (_) {}
    // Re-fetch properly via session (apiGet uses token, test needs session).
    // For same-origin web UI this works via browser cookie.
    renderTest();
    refreshTimer = setInterval(async () => {
        if (currentView !== 'test') return;
        await fetchState();
        renderTest();
    }, 2000);
}

async function fetchTestState() {
    try {
        const res = await fetch('/api/test', { credentials: 'same-origin' });
        if (res.ok) { testState = await res.json(); return true; }
    } catch (_) {}
    return false;
}

async function postTest(body) {
    try {
        const res = await fetch('/api/test', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        if (res.ok) { testState = await res.json(); return true; }
    } catch (_) {}
    return false;
}

function renderTest() {
    const v = view(); if (!v) return;
    const s = cachedState;
    const t = testState;
    const active = t && t.active;

    const tempField = (id, label, val) => `
        <div class="form-group" style="margin-bottom:8px">
            <label style="font-size:11px;color:#94a3b8">${label}</label>
            <div style="display:flex;gap:6px;align-items:center">
                <input id="${id}" type="number" step="0.5" value="${val !== null && val !== undefined ? val : ''}"
                    placeholder="&mdash;" style="width:90px;padding:4px 6px;background:#1e293b;border:1px solid #334155;border-radius:4px;color:#f1f5f9;font-size:13px">
                <button class="btn" style="padding:3px 8px;font-size:12px" onclick="testSetTemp('${id}')">Set</button>
                <button class="btn" style="padding:3px 8px;font-size:12px;color:#94a3b8" onclick="testClearTemp('${id}')">Clear</button>
            </div>
        </div>`;

    const faultCheck = (id, label, checked) => `
        <label style="display:flex;align-items:center;gap:6px;font-size:12px;cursor:pointer;margin-bottom:6px">
            <input type="checkbox" id="${id}" ${checked ? 'checked' : ''} onchange="testSetFault('${id}',this.checked)">
            ${label}
        </label>`;

    const relayDot = (on, label) => `
        <div style="display:flex;align-items:center;gap:8px;margin-bottom:4px">
            <div style="width:10px;height:10px;border-radius:50%;background:${on?'#22c55e':'#475569'}"></div>
            <span style="font-size:13px">${label}: <strong>${on?'ON':'OFF'}</strong></span>
        </div>`;

    v.innerHTML = `
        <div style="display:flex;gap:16px;flex-wrap:wrap;align-items:flex-start">

            <div class="card" style="flex:1;min-width:280px">
                <h3 style="display:flex;align-items:center;gap:10px">
                    Test Mode
                    <span style="font-size:12px;padding:2px 8px;border-radius:10px;background:${active?'#166534':'#374151'};color:${active?'#86efac':'#9ca3af'}">
                        ${active ? 'ACTIVE' : 'INACTIVE'}
                    </span>
                </h3>
                ${active
                    ? `<button class="btn btn-danger" style="width:100%;margin-bottom:12px" onclick="testToggle(false)">Deactivate Test Mode</button>`
                    : `<button class="btn btn-primary" style="width:100%;margin-bottom:12px" onclick="testToggle(true)">Activate Test Mode</button>`
                }
                <p style="font-size:12px;color:#64748b;margin-bottom:12px">
                    When active, DS18B20 and API sensor readings are replaced by the injected values below.
                    All other hardware (relays, buzzer, display) works normally.
                </p>
                <a href="../tools/test-runner/test-runner.html" target="_blank"
                   style="display:block;text-align:center;padding:8px;background:#1e40af;color:#bfdbfe;border-radius:6px;font-size:13px;text-decoration:none">
                    &#128640; Open Automated Test Runner &rarr;
                </a>
            </div>

            <div class="card" style="flex:1;min-width:280px">
                <h3>Temperature Injection</h3>
                <p style="font-size:11px;color:#64748b;margin-bottom:10px">Empty = sensor unavailable (NaN)</p>
                ${tempField('ti-flow',    'Flow temp (°C)',    t ? t.flowTemp    : null)}
                ${tempField('ti-return',  'Return temp (°C)',  t ? t.returnTemp  : null)}
                ${tempField('ti-room',    'Room temp (°C)',    t ? t.roomTemp    : null)}
                ${tempField('ti-outside', 'Outside temp (°C)', t ? t.outsideTemp : null)}
                <div style="margin-top:10px">
                    ${faultCheck('ti-flowfault',   'Flow sensor fault',   t && t.flowFault)}
                    ${faultCheck('ti-returnfault', 'Return sensor fault', t && t.returnFault)}
                    ${faultCheck('ti-roomlost',    'Room sensor lost',    t && t.roomSensorLost)}
                    ${faultCheck('ti-outsidelost', 'Outside sensor lost', t && t.outsideSensorLost)}
                </div>
                <div style="margin-top:10px;padding-top:10px;border-top:1px solid #1e293b">
                    <label style="font-size:11px;color:#94a3b8">Thermostat override</label>
                    <div style="display:flex;gap:8px;margin-top:6px;flex-wrap:wrap">
                        <button class="btn ${t&&t.thermostatOverride&&t.thermostatAllow?'btn-primary':''}" style="font-size:12px;padding:4px 10px"
                            onclick="testSetThermostat(true,true)">Override: ALLOW</button>
                        <button class="btn ${t&&t.thermostatOverride&&!t.thermostatAllow?'btn-danger':''}" style="font-size:12px;padding:4px 10px"
                            onclick="testSetThermostat(true,false)">Override: BLOCK</button>
                        <button class="btn ${t&&!t.thermostatOverride?'active-off':''}" style="font-size:12px;padding:4px 10px"
                            onclick="testSetThermostat(false,true)">Use GPIO</button>
                    </div>
                </div>
            </div>

            <div class="card" style="flex:1;min-width:220px">
                <h3>Live State</h3>
                ${s ? `
                    ${relayDot(s.relays.heater, 'Heater')}
                    ${relayDot(s.relays.pump,   'Pump')}
                    <div style="margin-top:8px;font-size:12px;color:#94a3b8">
                        Mode: <strong style="color:#f1f5f9">${s.status.mode.toUpperCase()}</strong><br>
                        Phase: <strong style="color:#f1f5f9">${s.status.phase}</strong><br>
                        Flow&nbsp;SP: <strong style="color:#f1f5f9">${s.status.activeFlowSetpoint}&deg;C</strong><br>
                        Return SP: <strong style="color:#f1f5f9">${s.status.activeReturnSetpoint}&deg;C</strong>
                    </div>
                    ${s.alarms.active ? `
                        <div style="margin-top:8px;padding:6px 8px;background:#450a0a;border-radius:4px;font-size:11px;color:#fca5a5">
                            &#9888; Alarms active (${s.alarms.active})
                        </div>` : `
                        <div style="margin-top:8px;font-size:11px;color:#22c55e">&#10003; No alarms</div>`}
                    <div style="margin-top:8px;font-size:11px;color:#64748b">
                        Sensors: flow=${s.sensors.flowTemp??'—'}&deg;  return=${s.sensors.returnTemp??'—'}&deg;
                        ${s.sensors.roomTemp!=null?`  room=${s.sensors.roomTemp}&deg;`:''}
                    </div>
                    ${s.testMode ? '<div style="margin-top:6px;font-size:11px;color:#f59e0b">&#9888; Test mode active</div>' : ''}
                ` : '<div style="color:#64748b">No state available</div>'}
                <div style="margin-top:8px;font-size:11px;color:#334155">Auto-refresh 2s</div>
            </div>

        </div>`;
}

async function testToggle(activate) {
    await postTest({ active: activate });
    await fetchState();
    renderTest();
}

async function testSetTemp(id) {
    const el = document.getElementById(id);
    if (!el) return;
    const val = el.value.trim();
    const key = { 'ti-flow': 'flowTemp', 'ti-return': 'returnTemp',
                  'ti-room': 'roomTemp',  'ti-outside': 'outsideTemp' }[id];
    if (!key) return;
    const body = {};
    body[key] = val === '' ? null : parseFloat(val);
    await postTest(body);
    renderTest();
}

async function testClearTemp(id) {
    const el = document.getElementById(id);
    if (el) el.value = '';
    const key = { 'ti-flow': 'flowTemp', 'ti-return': 'returnTemp',
                  'ti-room': 'roomTemp',  'ti-outside': 'outsideTemp' }[id];
    if (!key) return;
    const body = {}; body[key] = null;
    await postTest(body);
    renderTest();
}

async function testSetFault(id, checked) {
    const key = { 'ti-flowfault': 'flowFault', 'ti-returnfault': 'returnFault',
                  'ti-roomlost': 'roomSensorLost', 'ti-outsidelost': 'outsideSensorLost' }[id];
    if (!key) return;
    const body = {}; body[key] = checked;
    await postTest(body);
    renderTest();
}

async function testSetThermostat(override, allow) {
    await postTest({ thermostatOverride: override, thermostatAllow: allow });
    renderTest();
}

// ─── Utilities ────────────────────────────────────────────────────────────────
function escHtml(s) {
    return String(s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// ─── Init ─────────────────────────────────────────────────────────────────────
async function init() {
    // Check setup status first (no auth required)
    try {
        const st = await fetch('/api/status');
        if (st.ok) {
            const d = await st.json();
            apMode = !!d.apMode;
            if (!d.setupComplete) { navigate('setup'); return; }
        }
    } catch (_) {}

    // Try state with stored token — if it works we're already authenticated
    try {
        const res = await apiGet('/api/state');
        if (res.ok) {
            cachedState = await res.json();
            const cfgRes = await fetch('/api/config');
            userRole = cfgRes.ok ? 'admin' : 'operator';
            navigate('dashboard');
            return;
        }
    } catch (_) {}

    navigate('login');
}

document.addEventListener('DOMContentLoaded', init);
