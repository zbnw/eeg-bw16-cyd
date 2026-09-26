'use strict';

const PROTOCOL_VERSION = 1;
const RING_CAPACITY = 4096;
const EXPORT_CAPACITY = 100000;
const STALE_AFTER_MS = 3000;
const MAX_BATCH_SAMPLES = 1024;

function byId(id) {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Missing required element: ${id}`);
  return element;
}

const ui = {
  canvas: byId('eegChart'),
  menuButton: byId('menuButton'),
  sidebar: byId('sidebar'),
  sidebarOverlay: byId('sidebarOverlay'),
  sessionButton: byId('sessionButton'),
  exportButton: byId('exportButton'),
  protocolButton: byId('protocolButton'),
  toast: byId('toast'),
  clock: byId('clock'),
  duration: byId('durationValue'),
  durationProgress: byId('durationProgress'),
  attention: byId('attentionValue'),
  meditation: byId('meditationValue'),
  signalQuality: byId('signalQualityValue'),
  attentionProgress: byId('attentionProgress'),
  meditationProgress: byId('meditationProgress'),
  signalProgress: byId('signalProgress'),
  signalState: byId('signalState'),
  signalHelp: byId('signalHelp'),
  sampleRate: byId('sampleRate'),
  amplitude: byId('amplitude'),
  streamMode: byId('streamMode'),
  streamStatus: byId('streamStatus'),
  chartSummary: byId('chartSummary'),
  modeLabel: byId('modeLabel'),
  pageStatus: byId('pageStatus'),
  deviceName: byId('deviceName'),
  deviceMiniStatus: byId('deviceMiniStatus'),
  connectionDot: byId('connectionDot'),
  connectionTitle: byId('connectionTitle'),
  connectionDetail: byId('connectionDetail'),
  latency: byId('latencyValue'),
  packetLoss: byId('packetLossValue'),
  battery: byId('batteryValue'),
  dominantBand: byId('dominantBand'),
  insightTitle: byId('insightTitle'),
  insightText: byId('insightText')
};

const ctx = ui.canvas.getContext('2d');
if (!ctx) throw new Error('Canvas 2D context is unavailable');

const samples = new Float32Array(RING_CAPACITY);
const state = {
  mode: 'boot',
  displayPaused: false,
  writeIndex: 0,
  sampleCount: 0,
  sampleRateHz: null,
  attention: null,
  meditation: null,
  poorSignal: null,
  bands: null,
  deviceId: 'unknown',
  firmware: 'unknown',
  lastSeq: null,
  receivedMessages: 0,
  missedMessages: 0,
  rejectedMessages: 0,
  lastMessageAt: 0,
  lastSampleAt: 0,
  lastMetricsAt: 0,
  connectedAt: 0,
  sessionId: '',
  source: 'unknown',
  transport: {},
  sequencedMessages: 0,
  latencyMs: null,
  batteryPercent: null,
  exportRows: [],
  exportWrite: 0,
  exportDropped: 0,
  startedAt: Date.now(),
  demoPhase: 0,
  demoTimer: null,
  demoMetricTimer: null,
  socket: null,
  reconnectTimer: null,
  reconnectAttempt: 0,
  wsUrl: null,
  drawScheduled: false,
  lastSummaryAt: 0
};

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function finiteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function boundedNumber(value, min, max) {
  const number = finiteNumber(value);
  return number === null || number < min || number > max ? null : number;
}

function showToast(message) {
  ui.toast.textContent = message;
  ui.toast.classList.add('show');
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => ui.toast.classList.remove('show'), 2400);
}

function setProgress(element, value) {
  const normalized = value === null ? 0 : clamp(Math.round(value), 0, 100);
  element.setAttribute('aria-valuenow', String(normalized));
  const bar = element.querySelector('i');
  if (bar) bar.style.width = `${normalized}%`;
}

function updateMetricUi() {
  const attention = state.attention === null ? null : Math.round(state.attention);
  const meditation = state.meditation === null ? null : Math.round(state.meditation);
  const quality = state.poorSignal === null ? null : Math.round(100 - state.poorSignal / 2);

  ui.attention.textContent = attention === null ? '--' : String(attention);
  ui.meditation.textContent = meditation === null ? '--' : String(meditation);
  ui.signalQuality.textContent = quality === null ? '--' : String(quality);
  setProgress(ui.attentionProgress, attention);
  setProgress(ui.meditationProgress, meditation);
  setProgress(ui.signalProgress, quality);

  if (quality === null) {
    ui.signalState.textContent = '等待数据';
    ui.signalHelp.textContent = '数值由 poor_signal 换算';
  } else if (quality >= 80) {
    ui.signalState.textContent = '接触良好';
    ui.signalHelp.textContent = '当前数据质量较高';
  } else if (quality >= 50) {
    ui.signalState.textContent = '存在干扰';
    ui.signalHelp.textContent = '检查电极接触和线缆';
  } else {
    ui.signalState.textContent = '信号较差';
    ui.signalHelp.textContent = '当前指标不应作为有效结果';
  }

  updateBands();
}

function normalizeBands(input) {
  if (!input || typeof input !== 'object') return null;
  const keys = ['delta', 'theta', 'alpha', 'beta', 'gamma'];
  const values = {};
  let total = 0;
  for (const key of keys) {
    const value = finiteNumber(input[key]);
    if (value === null || value < 0) return null;
    values[key] = value;
    total += value;
  }
  if (!Number.isFinite(total) || total <= 0) return null;
  for (const key of keys) values[key] = values[key] * 100 / total;
  return values;
}

function updateBands() {
  const rows = document.querySelectorAll('.band-row[data-band]');
  let dominantKey = null;
  let dominantValue = -1;
  rows.forEach(row => {
    const key = row.dataset.band;
    const value = state.bands && finiteNumber(state.bands[key]);
    const percent = value === null ? null : clamp(Math.round(value), 0, 100);
    const bar = row.querySelector('.band-bar i');
    const label = row.querySelector('b');
    if (bar) bar.style.width = `${percent === null ? 0 : percent}%`;
    if (label) label.textContent = percent === null ? '--' : `${percent}%`;
    if (percent !== null && percent > dominantValue) {
      dominantValue = percent;
      dominantKey = key;
    }
  });
  const names = { delta: 'Delta 波', theta: 'Theta 波', alpha: 'Alpha 波', beta: 'Beta 波', gamma: 'Gamma 波' };
  ui.dominantBand.textContent = dominantKey ? names[dominantKey] : '等待指标';
}

function setMode(mode, detail) {
  state.mode = mode;
  ui.connectionDot.className = `status-dot ${mode}`;
  const modes = {
    demo: {
      label: 'DEMO MODE', title: '未连接真实设备', stream: 'DEMO',
      page: '正在运行本地演示数据，尚未连接真实设备。', device: '演示数据源', mini: 'DEMO · 未连接设备'
    },
    connecting: {
      label: 'CONNECTING', title: '正在连接设备', stream: 'WAIT',
      page: '正在建立 WebSocket 数据连接。', device: '等待设备', mini: '正在连接'
    },
    live: {
      label: 'LIVE MONITORING', title: '设备数据流正常', stream: 'LIVE',
      page: '正在显示设备发送的实时数据。', device: state.deviceId, mini: 'LIVE · 数据接收中'
    },
    replay: {
      label: 'HISTORICAL REPLAY', title: '历史记录回放', stream: 'REPLAY',
      page: '正在按指定的近似采样率回放历史记录，并非当前设备测量。', device: state.deviceId, mini: 'REPLAY · 历史数据'
    },
    stale: {
      label: 'DATA STALE', title: '数据流已超时', stream: 'STALE',
      page: '连接存在，但最近没有收到新数据。', device: state.deviceId, mini: '数据超时'
    },
    disconnected: {
      label: 'DISCONNECTED', title: '设备未连接', stream: 'OFFLINE',
      page: 'WebSocket 已断开，系统将自动重连。', device: state.deviceId, mini: '连接断开'
    },
    error: {
      label: 'CONFIG ERROR', title: '数据源配置错误', stream: 'ERROR',
      page: '请检查 WebSocket URL 或设备协议。', device: '配置错误', mini: '无法连接'
    }
  };
  const copy = modes[mode] || modes.error;
  ui.modeLabel.textContent = copy.label;
  ui.connectionTitle.textContent = copy.title;
  ui.streamMode.textContent = copy.stream;
  ui.pageStatus.textContent = detail || copy.page;
  ui.deviceName.textContent = copy.device;
  ui.deviceMiniStatus.innerHTML = `<i></i> ${copy.mini}`;
  ui.connectionDetail.textContent = mode === 'live' ? `${state.deviceId} · ${state.firmware}` : (detail || copy.page);
  ui.streamStatus.innerHTML = `<i></i> ${copy.stream} 数据流`;

  const isLive = mode === 'live';
  ui.insightTitle.textContent = isLive ? '正在显示设备原始输出' : (mode === 'replay' ? '正在回放历史记录' : (mode === 'demo' ? '当前为演示模式' : '当前没有可确认的实时数据'));
  ui.insightText.textContent = isLive
    ? '指标来自传感器算法，仅用于原型验证，不能用于医疗判断。'
    : (mode === 'replay' ? '数据来自已保存的记录，时间轴按指定采样率估算，不代表当前测量。' : '页面数据不代表真实脑电状态，也不能用于医疗判断。');
}

function pushRingSample(value) {
  samples[state.writeIndex] = value;
  state.writeIndex = (state.writeIndex + 1) % RING_CAPACITY;
  state.sampleCount = Math.min(RING_CAPACITY, state.sampleCount + 1);
}

function appendExportRows(batch, metadata) {
  const rate = state.sampleRateHz || 0;
  const stepUs = rate > 0 ? 1000000 / rate : 0;
  const supplied = finiteNumber(metadata.timestamp_us);
  const received = finiteNumber(metadata.host_received_us) ?? Date.now() * 1000;
  const baseTimestampUs = supplied ?? (received - Math.max(0, batch.length - 1) * stepUs);
  for (let index = 0; index < batch.length; index++) {
    const row = {
      seq: metadata.seq === undefined ? '' : metadata.seq,
      timestampUs: Math.round(baseTimestampUs + index * stepUs),
      raw: batch[index],
      source: state.source, deviceId: state.deviceId, firmware: state.firmware,
      sessionId: state.sessionId, sampleRate: state.sampleRateHz,
      timestampKind: supplied === null ? 'host-estimate' : 'source-supplied',
      sampleIndex: Number.isSafeInteger(metadata.sample_index) ? metadata.sample_index + index : '',
      attention: state.attention === null ? '' : state.attention,
      meditation: state.meditation === null ? '' : state.meditation,
      poorSignal: state.poorSignal === null ? '' : state.poorSignal
    };
    if (state.exportRows.length < EXPORT_CAPACITY) state.exportRows.push(row);
    else {
      state.exportRows[state.exportWrite] = row;
      state.exportWrite = (state.exportWrite + 1) % EXPORT_CAPACITY;
      state.exportDropped++;
    }
  }
}

function orderedExportRows() {
  return state.exportRows.slice(state.exportWrite).concat(state.exportRows.slice(0, state.exportWrite));
}

function acceptSequence(seq) {
  if (!Number.isSafeInteger(seq) || seq < 0) return;
  state.sequencedMessages++;
  if (state.lastSeq !== null && seq > state.lastSeq + 1) state.missedMessages += seq - state.lastSeq - 1;
  if (state.lastSeq === null || seq > state.lastSeq) state.lastSeq = seq;
}

function acceptSamples(batch, metadata = {}) {
  if (!Array.isArray(batch) || batch.length === 0 || batch.length > MAX_BATCH_SAMPLES) return false;
  const clean = [];
  for (const item of batch) {
    const value = boundedNumber(item, -32768, 32767);
    if (value === null || !Number.isInteger(value)) return false;
    clean.push(value);
  }
  const reportedRate = boundedNumber(metadata.sample_rate_hz, 1, 4096);
  if (reportedRate !== null) state.sampleRateHz = reportedRate;
  acceptSequence(metadata.seq);
  clean.forEach(pushRingSample);
  appendExportRows(clean, metadata);
  state.lastMessageAt = Date.now();
  state.lastSampleAt = state.lastMessageAt;
  state.receivedMessages++;
  ui.sampleRate.textContent = state.sampleRateHz === null ? '--' : String(Math.round(state.sampleRateHz));
  updateTransportUi();
  scheduleDraw();
  return true;
}

function acceptMetrics(message) {
  const attention = boundedNumber(message.attention, 0, 100);
  const meditation = boundedNumber(message.meditation, 0, 100);
  const poorSignal = boundedNumber(message.poor_signal, 0, 200);
  const bands = normalizeBands(message.bands);
  if (attention === null && meditation === null && poorSignal === null && bands === null) return false;
  state.attention = attention;
  state.meditation = meditation;
  state.poorSignal = poorSignal;
  state.bands = bands;
  acceptSequence(message.seq);
  state.lastMessageAt = Date.now();
  state.lastMetricsAt = state.lastMessageAt;
  state.receivedMessages++;
  updateMetricUi();
  updateTransportUi();
  return attention !== null || meditation !== null || poorSignal !== null || bands !== null;
}

function acceptStatus(message) {
  const battery = boundedNumber(message.battery_percent, 0, 100);
  const latency = boundedNumber(message.latency_ms, 0, 60000);
  const fields = ['ble_gap_events', 'ble_queue_drops', 'ble_malformed', 'checksum_errors',
    'frame_errors', 'reconnects', 'bw16_checksum_errors', 'bw16_tx_drops', 'free_heap', 'received_rate_hz'];
  const transport = {};
  for (const key of fields) {
    const value = boundedNumber(message[key], 0, Number.MAX_SAFE_INTEGER);
    if (value !== null) transport[key] = value;
  }
  if (battery === null && latency === null && typeof message.connected !== 'boolean' && !Object.keys(transport).length) return false;
  state.transport = transport;
  if (transport.received_rate_hz > 0 && transport.received_rate_hz <= 4096) {
    state.sampleRateHz = transport.received_rate_hz;
    ui.sampleRate.textContent = `~${Math.round(state.sampleRateHz)}`;
  }
  if (message.connected === false) {
    state.lastSampleAt = 0;
    clearMetrics();
  }
  if (battery !== null) state.batteryPercent = battery;
  if (latency !== null) state.latencyMs = latency;
  state.lastMessageAt = Date.now();
  state.receivedMessages++;
  acceptSequence(message.seq);
  updateTransportUi();
  return true;
}

function clearMetrics() {
  state.attention = state.meditation = state.poorSignal = state.bands = null;
  state.lastMetricsAt = 0;
  updateMetricUi();
}

function resetSession(message) {
  state.sessionId = String(message.session_id || 'unknown').slice(0, 100);
  state.source = message.source === 'replay' ? 'replay' : 'device';
  state.lastSeq = null;
  state.sequencedMessages = state.receivedMessages = state.missedMessages = 0;
  state.lastSampleAt = state.lastMessageAt = 0;
  state.sampleCount = state.writeIndex = 0;
  state.sampleRateHz = null;
  ui.sampleRate.textContent = '--';
  ui.amplitude.textContent = '--';
  state.batteryPercent = state.latencyMs = null;
  state.transport = {};
  clearMetrics();
  updateTransportUi();
  scheduleDraw();
}

function handleDeviceMessage(event) {
  let message;
  try {
    message = JSON.parse(event.data);
  } catch (error) {
    state.rejectedMessages++;
    return;
  }
  if (!message || message.v !== PROTOCOL_VERSION || typeof message.type !== 'string') {
    state.rejectedMessages++;
    return;
  }

  let accepted = false;
  if (message.type === 'hello') {
    resetSession(message);
    state.deviceId = String(message.device_id || 'EEG device').slice(0, 80);
    state.firmware = String(message.firmware || 'firmware unknown').slice(0, 80);
    const rate = boundedNumber(message.sample_rate_hz, 1, 4096);
    if (rate !== null) state.sampleRateHz = rate;
    state.lastMessageAt = Date.now();
    state.receivedMessages++;
    accepted = true;
  } else if (message.type === 'samples') {
    if (Number.isSafeInteger(message.seq) && state.lastSeq !== null && message.seq <= state.lastSeq) return;
    accepted = acceptSamples(message.samples, message);
  } else if (message.type === 'metrics') {
    if (Number.isSafeInteger(message.seq) && state.lastSeq !== null && message.seq <= state.lastSeq) return;
    accepted = acceptMetrics(message);
  } else if (message.type === 'status') {
    if (Number.isSafeInteger(message.seq) && state.lastSeq !== null && message.seq <= state.lastSeq) return;
    accepted = acceptStatus(message);
  }

  if (!accepted) {
    state.rejectedMessages++;
    return;
  }
  state.reconnectAttempt = 0;
  setMode(state.lastSampleAt && Date.now() - state.lastSampleAt <= STALE_AFTER_MS
    ? (state.source === 'replay' ? 'replay' : 'live') : 'stale');
}

function updateTransportUi() {
  const total = state.sequencedMessages + state.missedMessages;
  const loss = total > 0 ? state.missedMessages * 100 / total : null;
  ui.packetLoss.textContent = loss === null ? '--' : `${loss.toFixed(loss < 1 ? 2 : 1)}%`;
  ui.latency.textContent = state.latencyMs === null ? '--' : `${Math.round(state.latencyMs)} ms`;
  ui.battery.textContent = state.batteryPercent === null ? '--' : `${Math.round(state.batteryPercent)}%`;
  const health = document.getElementById('transportHealth');
  if (health) {
    const t = state.transport;
    const value = key => t[key] === undefined ? '--' : t[key];
    health.textContent = `BLE 间断 ${value('ble_gap_events')} · 接收队列丢弃 ${value('ble_queue_drops')} · BW16 丢弃 ${value('bw16_tx_drops')} · UART 校验错误 ${value('bw16_checksum_errors')}`;
  }
}

function validateWebSocketUrl(value) {
  try {
    const url = new URL(value);
    return url.protocol === 'ws:' || url.protocol === 'wss:' ? url.href : null;
  } catch (error) {
    return null;
  }
}

function scheduleReconnect() {
  if (!state.wsUrl || state.reconnectTimer) return;
  const delay = Math.min(30000, 1000 * 2 ** Math.min(state.reconnectAttempt, 5));
  state.reconnectAttempt++;
  state.reconnectTimer = setTimeout(() => {
    state.reconnectTimer = null;
    connectWebSocket();
  }, delay);
}

function connectWebSocket() {
  if (!state.wsUrl) return;
  if (state.socket) {
    const previous = state.socket;
    state.socket = null;
    previous.close();
  }
  resetSession({});
  state.connectedAt = Date.now();
  setMode('connecting', `正在连接 ${state.wsUrl}`);
  let socket;
  try {
    socket = new WebSocket(state.wsUrl);
  } catch (error) {
    setMode('error', 'WebSocket URL 无法建立连接。');
    scheduleReconnect();
    return;
  }
  state.socket = socket;
  socket.addEventListener('open', () => { if (state.socket === socket) setMode('connecting', '连接已建立，等待设备握手。'); });
  socket.addEventListener('message', event => { if (state.socket === socket) handleDeviceMessage(event); });
  socket.addEventListener('error', () => { if (state.socket === socket) setMode('disconnected', 'WebSocket 发生错误。'); });
  socket.addEventListener('close', () => {
    if (state.socket !== socket) return;
    state.socket = null;
    clearMetrics();
    state.lastSampleAt = 0;
    setMode('disconnected');
    scheduleReconnect();
  });
}

function generateDemoSamples() {
  const batch = [];
  for (let index = 0; index < 10; index++) {
    const t = state.demoPhase;
    const alpha = Math.sin(t * 0.31) * 260;
    const beta = Math.sin(t * 0.83) * 85;
    const drift = Math.sin(t * 0.021) * 60;
    const deterministicNoise = Math.sin(t * 2.17) * 18;
    batch.push(Math.round(alpha + beta + drift + deterministicNoise));
    state.demoPhase += 0.12;
  }
  acceptSamples(batch, {
    seq: state.lastSeq === null ? 0 : state.lastSeq + 1,
    timestamp_us: Date.now() * 1000,
    sample_rate_hz: 500
  });
}

function updateDemoMetrics() {
  const t = state.demoPhase;
  state.attention = Math.round(62 + Math.sin(t * 0.015) * 12);
  state.meditation = Math.round(55 + Math.cos(t * 0.012) * 10);
  state.poorSignal = Math.round(18 + (Math.sin(t * 0.009) + 1) * 6);
  state.bands = normalizeBands({
    delta: 18 + Math.sin(t * 0.01) * 3,
    theta: 24 + Math.cos(t * 0.013) * 4,
    alpha: 43 + Math.sin(t * 0.008) * 7,
    beta: 29 + Math.cos(t * 0.017) * 5,
    gamma: 12 + Math.sin(t * 0.019) * 2
  });
  updateMetricUi();
}

function startDemo() {
  state.source = 'demo';
  state.sessionId = `demo-${Date.now()}`;
  state.deviceId = 'demo-source';
  state.firmware = 'browser-generator';
  state.sampleRateHz = 500;
  setMode('demo');
  updateDemoMetrics();
  generateDemoSamples();
  state.demoTimer = setInterval(generateDemoSamples, 20);
  state.demoMetricTimer = setInterval(updateDemoMetrics, 1000);
}

function resizeCanvas() {
  const rect = ui.canvas.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));
  if (ui.canvas.width !== width || ui.canvas.height !== height) {
    ui.canvas.width = width;
    ui.canvas.height = height;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  scheduleDraw();
}

function orderedSamples() {
  const result = new Float32Array(state.sampleCount);
  const start = (state.writeIndex - state.sampleCount + RING_CAPACITY) % RING_CAPACITY;
  for (let index = 0; index < state.sampleCount; index++) {
    result[index] = samples[(start + index) % RING_CAPACITY];
  }
  return result;
}

function drawChart() {
  state.drawScheduled = false;
  if (state.displayPaused || document.hidden) return;
  const width = ui.canvas.clientWidth;
  const height = ui.canvas.clientHeight;
  if (!width || !height) return;

  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = '#e2e9ec';
  ctx.lineWidth = 1;
  for (let row = 0; row <= 4; row++) {
    const y = Math.round(height * row / 4) + 0.5;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  for (let column = 0; column <= 5; column++) {
    const x = Math.round(width * column / 5) + 0.5;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }

  const data = orderedSamples();
  if (data.length < 2) return;
  let peak = 1;
  for (const value of data) peak = Math.max(peak, Math.abs(value));
  const scale = (height * 0.42) / peak;
  // Preserve every sample's extrema in each screen bucket.
  const columns = Math.min(Math.floor(width), data.length);
  ctx.strokeStyle = '#0ca89f';
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let col = 0; col < columns; col++) {
    const begin = Math.floor(col * data.length / columns);
    const end = Math.max(begin + 1, Math.floor((col + 1) * data.length / columns));
    let low = Infinity, high = -Infinity;
    for (let i = begin; i < end; i++) {
      low = Math.min(low, data[i]);
      high = Math.max(high, data[i]);
    }
    const x = col * width / Math.max(1, columns - 1);
    const top = height / 2 - high * scale;
    const bottom = height / 2 - low * scale;
    ctx.moveTo(x, top);
    ctx.lineTo(x, Math.max(top + 1, bottom));
  }
  ctx.stroke();
  document.querySelectorAll('.y-axis span').forEach((element, index) => {
    element.textContent = Math.round((height / 2 - index * height / 4) / scale).toString();
  });
  document.querySelectorAll('.x-axis span').forEach((element, index) => {
    const remaining = (data.length - 1) * (5 - index) / 5;
    element.textContent = index === 5 ? '最新' : (state.sampleRateHz
      ? `~-${(remaining / state.sampleRateHz).toFixed(1)}s` : `-${Math.round(remaining)}点`);
  });

  ui.amplitude.textContent = peak.toFixed(0);
  const now = Date.now();
  if (now - state.lastSummaryAt > 1000) {
    state.lastSummaryAt = now;
    ui.chartSummary.textContent = `当前缓存 ${data.length} 个样本，显示峰值 ${peak.toFixed(0)} raw units。`;
  }
}

function scheduleDraw() {
  if (state.drawScheduled || state.displayPaused) return;
  state.drawScheduled = true;
  requestAnimationFrame(drawChart);
}

function updateClockAndDuration() {
  const now = new Date();
  ui.clock.textContent = now.toLocaleTimeString('zh-CN', { hour12: false });
  const elapsed = Math.max(0, Math.floor((Date.now() - state.startedAt) / 1000));
  const hours = String(Math.floor(elapsed / 3600)).padStart(2, '0');
  const minutes = String(Math.floor(elapsed % 3600 / 60)).padStart(2, '0');
  const seconds = String(elapsed % 60).padStart(2, '0');
  ui.duration.textContent = `${hours}:${minutes}:${seconds}`;
  ui.durationProgress.style.width = `${Math.min(100, elapsed / 36)}%`;

  if (state.wsUrl && ['live', 'replay', 'connecting'].includes(state.mode) &&
      Date.now() - (state.lastSampleAt || state.connectedAt) > STALE_AFTER_MS) {
    setMode('stale');
  }
  if (state.wsUrl && state.lastMetricsAt && Date.now() - state.lastMetricsAt > STALE_AFTER_MS) clearMetrics();
}

function csvEscape(value) {
  const text = typeof value === 'string' && /^[=+@\-\t\r]/.test(value) ? "'" + value : String(value);
  return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function exportCsv() {
  if (!state.exportRows.length) {
    showToast('当前没有可导出的样本');
    return;
  }
  const header = [
    'schema_version', 'source_mode', 'device_id', 'firmware', 'seq', 'timestamp_us',
    'sample_rate_hz', 'raw', 'attention', 'meditation', 'poor_signal', 'session_id', 'sample_index', 'timestamp_kind'
  ];
  const lines = [header.join(',')];
  for (const row of orderedExportRows()) {
    lines.push([
      PROTOCOL_VERSION, row.source, row.deviceId, row.firmware, row.seq, row.timestampUs,
      row.sampleRate || '', row.raw, row.attention, row.meditation, row.poorSignal,
      row.sessionId, row.sampleIndex, row.timestampKind
    ].map(csvEscape).join(','));
  }
  const blob = new Blob([`\uFEFF${lines.join('\n')}`], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = `neuroflow-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  showToast(`已导出 ${state.exportRows.length} 个样本；早期已覆盖 ${state.exportDropped} 个，长期记录请用采集工具`);
}

function setSidebarOpen(open) {
  ui.sidebar.classList.toggle('open', open);
  ui.sidebarOverlay.classList.toggle('show', open);
  ui.menuButton.setAttribute('aria-expanded', String(open));
  const mobile = window.matchMedia('(max-width: 760px)').matches;
  ui.sidebar.setAttribute('aria-hidden', String(mobile && !open));
  if ('inert' in ui.sidebar) ui.sidebar.inert = mobile && !open;
  if (open) {
    const firstLink = ui.sidebar.querySelector('.nav-item');
    if (firstLink) firstLink.focus();
  } else if (document.activeElement && ui.sidebar.contains(document.activeElement)) {
    ui.menuButton.focus();
  }
}

ui.menuButton.addEventListener('click', () => setSidebarOpen(!ui.sidebar.classList.contains('open')));
ui.sidebarOverlay.addEventListener('click', () => setSidebarOpen(false));
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && ui.sidebar.classList.contains('open')) setSidebarOpen(false);
});

document.querySelectorAll('.nav-item').forEach(item => item.addEventListener('click', event => {
  if (item.getAttribute('aria-disabled') === 'true') {
    event.preventDefault();
    showToast(`${item.dataset.page}页面尚未实现`);
    return;
  }
  document.querySelectorAll('.nav-item').forEach(link => link.classList.remove('active'));
  item.classList.add('active');
  setSidebarOpen(false);
}));

ui.sessionButton.addEventListener('click', () => {
  state.displayPaused = !state.displayPaused;
  ui.sessionButton.setAttribute('aria-pressed', String(state.displayPaused));
  ui.sessionButton.innerHTML = state.displayPaused
    ? '<span class="resume-dot"></span>继续显示'
    : '<span></span>暂停显示';
  showToast(state.displayPaused ? '显示已暂停，数据连接保持' : '显示已继续');
  if (!state.displayPaused) scheduleDraw();
});

ui.exportButton.addEventListener('click', exportCsv);
ui.protocolButton.addEventListener('click', () => window.open('PROTOCOL.md', '_blank', 'noopener'));

window.addEventListener('resize', () => {
  resizeCanvas();
  if (!window.matchMedia('(max-width: 760px)').matches) setSidebarOpen(false);
});
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) scheduleDraw();
});
if ('ResizeObserver' in window) new ResizeObserver(resizeCanvas).observe(ui.canvas);

setInterval(updateClockAndDuration, 1000);
setInterval(() => {
  if (state.wsUrl && state.mode === 'stale' && state.socket && state.socket.readyState !== WebSocket.OPEN) {
    scheduleReconnect();
  }
}, 1000);

resizeCanvas();
updateClockAndDuration();
updateMetricUi();
updateTransportUi();
setSidebarOpen(false);

const wsParameter = new URLSearchParams(window.location.search).get('ws');
if (wsParameter) {
  state.wsUrl = validateWebSocketUrl(wsParameter);
  if (state.wsUrl) connectWebSocket();
  else setMode('error', 'ws 参数必须是 ws:// 或 wss:// URL。');
} else {
  startDemo();
}
